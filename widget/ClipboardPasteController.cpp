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
#include <ole2.h>
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
    // Arm the self-paste echo guard on the ACTUAL mimeData we're about
    // to write.  Both sides use computeEchoSignature() on live
    // QMimeData, guaranteeing the signature matches when the monitor
    // sees the echo.
    monitor_->setSelfPasteGuardFromMimeData(mimeData);
    QGuiApplication::clipboard()->setMimeData(mimeData);
    qInfo() << "[clipboard-widget] setClipboard wrote system clipboard";

#ifdef Q_OS_WIN
    // Replace Qt's lossy CF_HTML with the raw bytes captured from the
    // original source.  Qt's QWindowsMimeHtml only stores the fragment
    // between StartFragment/EndFragment, losing the <head><style> block
    // that defines CSS classes for centering, fonts, lists, etc.
    // After OleFlushClipboard materializes Qt's formats to the system
    // clipboard, we overwrite CF_HTML with the faithful raw bytes.
    // Approach borrowed from Ditto clipboard manager.
    static const QString cfHtmlMime =
        QStringLiteral("application/x-qt-windows-mime;value=\"HTML Format\"");
    const QMimeData *sourceMime = item.getMimeData();
    if (!plainText && sourceMime && sourceMime->hasFormat(cfHtmlMime)) {
        const QByteArray rawCfHtml = sourceMime->data(cfHtmlMime);
        if (!rawCfHtml.isEmpty()) {
            HRESULT hr = OleFlushClipboard();
            qInfo().noquote() << QStringLiteral("[clipboard-widget] OleFlushClipboard hr=0x%1")
                .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
            if (SUCCEEDED(hr)) {
                static UINT cfHtml = RegisterClipboardFormatW(L"HTML Format");
                if (OpenClipboard(nullptr)) {
                    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, rawCfHtml.size());
                    if (hg) {
                        memcpy(GlobalLock(hg), rawCfHtml.constData(), rawCfHtml.size());
                        GlobalUnlock(hg);
                        SetClipboardData(cfHtml, hg);
                        qInfo().noquote() << QStringLiteral("[clipboard-widget] injected raw CF_HTML %1 bytes")
                            .arg(rawCfHtml.size());
                    }
                    CloseClipboard();
                }
            }
        }
    }
#endif
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
    qInfo().noquote() << QStringLiteral("[paste-controller] pasteToTarget targetWindow=%1 isAutoPaste=%2")
        .arg(reinterpret_cast<quintptr>(targetWindow))
        .arg(MPasteSettings::getInst()->isAutoPaste());
    if (!MPasteSettings::getInst()->isAutoPaste()) {
        qInfo() << "[paste-controller] auto-paste disabled, abort";
        return;
    }

    isPasting_ = true;
    emit pastingStarted();

    auto finishPaste = [this]() {
        qInfo() << "[paste-controller] finishPaste -> triggerPasteShortcut";
        PlatformRelated::triggerPasteShortcut(MPasteSettings::getInst()->getPasteShortcutMode());
        QTimer::singleShot(200, this, [this]() {
            isPasting_ = false;
            emit pastingFinished();
        });
    };

    auto restoreFocusAndPaste = [this, targetWindow, finishPaste]() {
        qInfo().noquote() << QStringLiteral("[paste-controller] restoreFocusAndPaste targetWindow=%1")
            .arg(reinterpret_cast<quintptr>(targetWindow));
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
            qInfo().noquote() << QStringLiteral("[paste-controller] alt poll done released=%1 timedOut=%2 polls=%3")
                .arg(altReleased).arg(timedOut).arg(pollCount);
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
