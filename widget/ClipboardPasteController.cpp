// input: Depends on ClipboardPasteController.h, ClipboardExportService, ClipboardMonitor, PlatformRelated, MPasteSettings.
// output: Implements clipboard write and paste-to-target orchestration.
// pos: Widget-layer helper implementation.
// update: If I change, update this header block.
#include "ClipboardPasteController.h"
#include "utils/ClipboardMonitor.h"
#include "utils/ClipboardExportService.h"
#include "utils/MPasteSettings.h"
#include "utils/PlatformRelated.h"
#include "data/LocalSaver.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// rehydrateClipboardItem logic is now in ClipboardItem::rehydrate().

QString elideLogText(QString text, int maxLen = 48) {
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    if (text.size() > maxLen) {
        text.truncate(maxLen);
        text.append(QStringLiteral("..."));
    }
    return text;
}

QString itemSummary(const ClipboardItem &item) {
    return QStringLiteral("type=%1 fp=%2 text=\"%3\" htmlLen=%4 urlCount=%5")
        .arg(item.getContentType())
        .arg(QString::fromLatin1(item.fingerprint().toHex().left(12)))
        .arg(elideLogText(item.getNormalizedText()))
        .arg(item.getHtml().size())
        .arg(item.getNormalizedUrls().size());
}

} // anonymous namespace

ClipboardPasteController::ClipboardPasteController(ClipboardMonitor *monitor, QObject *parent)
    : QObject(parent)
    , monitor_(monitor)
{
}

QMimeData *ClipboardPasteController::createPlainTextMimeData(const ClipboardItem &item) const {
    QString plainText;
    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();

    if (item.getContentType() == File && !normalizedUrls.isEmpty()) {
        QStringList urls;
        for (const QUrl &url : normalizedUrls) {
            urls << (url.isLocalFile() ? url.toLocalFile() : url.toString());
        }
        plainText = urls.join(QLatin1Char('\n'));
    }

    if (plainText.isEmpty()) {
        plainText = item.getNormalizedText();
    }

    if (plainText.isEmpty() && item.getMimeData() && item.getMimeData()->hasHtml()) {
        plainText = item.getHtml();
        static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
        plainText.replace(tagRe, QString());
        plainText = plainText.trimmed();
    }

    if (plainText.isEmpty() && !normalizedUrls.isEmpty()) {
        QStringList urls;
        for (const QUrl &url : normalizedUrls) {
            urls << (url.isLocalFile() ? url.toLocalFile() : url.toString());
        }
        plainText = urls.join(QLatin1Char('\n'));
    }

    if (plainText.isEmpty() && item.getMimeData() && item.getMimeData()->hasColor()) {
        plainText = item.getColor().name(QColor::HexRgb);
    }

    if (plainText.isEmpty()) {
        return nullptr;
    }

    auto *mimeData = new QMimeData;
    mimeData->setText(plainText);
    mimeData->setData("text/plain;charset=utf-8", plainText.toUtf8());
    return mimeData;
}

bool ClipboardPasteController::setClipboard(const ClipboardItem &item, bool plainText) {
    qInfo().noquote() << QStringLiteral("[clipboard-widget] setClipboard begin plainText=%1 %2")
        .arg(plainText)
        .arg(itemSummary(item));
    monitor_->disconnectMonitor();

    QMimeData *mimeData = plainText ? createPlainTextMimeData(item)
                                    : ClipboardExportService::buildMimeData(item);
    if (!mimeData && !plainText) {
        const ClipboardItem rehydrated = ClipboardItem::rehydrate(item);
        if (!rehydrated.getName().isEmpty()) {
            mimeData = ClipboardExportService::buildMimeData(rehydrated);
            if (!mimeData) {
                mimeData = createPlainTextMimeData(rehydrated);
            }
        }
    }
    if (mimeData) {
        bool hasPayload = false;
        if (mimeData->hasText() && !mimeData->text().isEmpty()) {
            hasPayload = true;
        } else if (mimeData->hasHtml() && !mimeData->html().isEmpty()) {
            hasPayload = true;
        } else if (mimeData->hasUrls() && !mimeData->urls().isEmpty()) {
            hasPayload = true;
        } else if (mimeData->hasColor()) {
            hasPayload = true;
        } else if (mimeData->hasImage()) {
            const QVariant imageData = mimeData->imageData();
            hasPayload = imageData.isValid() && !imageData.isNull();
        } else {
            for (const QString &format : mimeData->formats()) {
                if (!mimeData->data(format).isEmpty()) {
                    hasPayload = true;
                    break;
                }
            }
        }

        if (!hasPayload) {
            delete mimeData;
            mimeData = createPlainTextMimeData(item);
        }
    }
    if (mimeData && !plainText) {
        const QString normalizedText = item.getNormalizedText();
        const bool hasText = mimeData->hasText() && !mimeData->text().isEmpty();
        const bool hasHtml = mimeData->hasHtml() && !mimeData->html().isEmpty();
        const bool hasUrls = mimeData->hasUrls() && !mimeData->urls().isEmpty();
        if (!normalizedText.isEmpty() && !hasText && !hasHtml && !hasUrls) {
            mimeData->setText(normalizedText);
            mimeData->setData("text/plain;charset=utf-8", normalizedText.toUtf8());
        }
    }
    if (!mimeData) {
        qInfo() << "[clipboard-widget] setClipboard aborted: no mimeData";
        monitor_->connectMonitor();
        return false;
    }

    if (!plainText && item.getContentType() == File) {
        handleUrlsClipboard(mimeData, item);
    }

    lastPastedFingerprint_ = item.fingerprint();
    QGuiApplication::clipboard()->setMimeData(mimeData);
    qInfo() << "[clipboard-widget] setClipboard wrote system clipboard";
    QTimer::singleShot(200, this, [this]() {
        qInfo() << "[clipboard-widget] reconnect monitor after self clipboard write";
        monitor_->connectMonitor();
    });
    return true;
}

void ClipboardPasteController::handleUrlsClipboard(QMimeData *mimeData, const ClipboardItem &item) {
    if (!mimeData) {
        return;
    }

    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();
    if (normalizedUrls.isEmpty()) {
        return;
    }

    bool files = true;
    for (const QUrl &url : normalizedUrls) {
        if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
            files = false;
            break;
        }
    }

    if (files) {
        QByteArray nautilus("x-special/nautilus-clipboard\n");
        QByteArray byteArray("copy\n");
        QStringList plainTextLines;
        for (const QUrl &url : normalizedUrls) {
            byteArray.append(url.toEncoded()).append('\n');
            plainTextLines << url.toLocalFile();
        }
        mimeData->setData("x-special/gnome-copied-files", byteArray);
        nautilus.append(byteArray);
        mimeData->setData("COMPOUND_TEXT", nautilus);
        const QString plainText = plainTextLines.join(QLatin1Char('\n'));
        mimeData->setText(plainText);
        mimeData->setData("text/plain;charset=utf-8", plainText.toUtf8());
    }
    mimeData->setUrls(normalizedUrls);
}

void ClipboardPasteController::pasteToTarget(WId targetWindow) {
    if (!MPasteSettings::getInst()->isAutoPaste()) {
        return;
    }

    isPasting_ = true;
    emit pastingStarted();

    auto finishPaste = [this]() {
        PlatformRelated::triggerPasteShortcut(MPasteSettings::getInst()->getPasteShortcutMode());
        QTimer::singleShot(200, this, [this]() {
            isPasting_ = false;
            emit pastingFinished();
        });
    };

    auto restoreFocusAndPaste = [this, targetWindow, finishPaste]() {
        if (targetWindow) {
            PlatformRelated::activateWindow(targetWindow);
            QTimer::singleShot(100, this, finishPaste);
            return;
        }

        QTimer::singleShot(0, this, finishPaste);
    };

#ifdef Q_OS_WIN
    auto *altReleaseTimer = new QTimer(this);
    int pollCount = 0;
    altReleaseTimer->setInterval(10);
    connect(altReleaseTimer, &QTimer::timeout, this, [altReleaseTimer, pollCount, restoreFocusAndPaste]() mutable {
        const bool altReleased = (GetAsyncKeyState(VK_MENU) & 0x8000) == 0;
        const bool timedOut = pollCount >= 50;
        if (altReleased || timedOut) {
            altReleaseTimer->stop();
            altReleaseTimer->deleteLater();
            restoreFocusAndPaste();
            return;
        }

        ++pollCount;
    });
    altReleaseTimer->start();
#else
    restoreFocusAndPaste();
#endif
}
