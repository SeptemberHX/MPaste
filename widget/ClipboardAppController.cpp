#include "ClipboardAppController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include "ClipboardCardDelegate.h"
#include "CopySoundPlayer.h"
#include "ScrollItemsWidget.h"
#include "SyncWatcher.h"
#include "utils/ClipboardBoardService.h"
#include "ClipboardPasteController.h"
#include "data/LocalSaver.h"
#include "utils/ClipboardMonitor.h"
#include "utils/MPasteSettings.h"
#include "utils/OcrService.h"

// OcrResultDialog class moved to MPasteWidget.cpp — the controller
// only emits ocrStarted/ocrCompleted/ocrFailed signals.

// ---------------------------------------------------------------------------
// Helper functions (moved from MPasteWidget.cpp)
// ---------------------------------------------------------------------------

namespace {

QString elideClipboardLogText(QString text, int maxLen = 48) {
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    if (text.size() > maxLen) {
        text.truncate(maxLen);
        text.append(QStringLiteral("..."));
    }
    return text;
}

QString widgetItemSummary(const ClipboardItem &item) {
    return QStringLiteral("type=%1 fp=%2 text=\"%3\" htmlLen=%4 urlCount=%5")
        .arg(item.getContentType())
        .arg(QString::fromLatin1(item.fingerprint().toHex().left(12)))
        .arg(elideClipboardLogText(item.getNormalizedText()))
        .arg(item.getHtml().size())
        .arg(item.getNormalizedUrls().size());
}

// rehydrateClipboardItem is now ClipboardItem::rehydrate().

bool hasUsableMimeData(ClipboardItem item) {
    const QMimeData *mimeData = item.getMimeData();
    return mimeData && !mimeData->formats().isEmpty();
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ClipboardAppController::ClipboardAppController(
    const QMap<QString, ScrollItemsWidget*> &boardWidgetMap,
    ScrollItemsWidget *clipboardWidget,
    ScrollItemsWidget *staredWidget,
    QWidget *parentWidget,
    QObject *parent)
    : QObject(parent)
    , parentWidget_(parentWidget)
    , boardWidgetMap_(boardWidgetMap)
    , clipboardWidget_(clipboardWidget)
    , staredWidget_(staredWidget)
{
    initClipboard();
    initSound();

    for (auto *board : boardWidgetMap_.values()) {
        connectBoardSignals(board);
    }

    setupSyncWatcher();
}

ClipboardAppController::~ClipboardAppController() {
    delete monitor_;
}

// ---------------------------------------------------------------------------
// Init helpers
// ---------------------------------------------------------------------------

void ClipboardAppController::initClipboard() {
    monitor_ = new ClipboardMonitor();
    pasteController_ = new ClipboardPasteController(monitor_, this);

    connect(monitor_, &ClipboardMonitor::clipboardActivityObserved,
            this, &ClipboardAppController::clipboardActivityObserved);
    connect(monitor_, &ClipboardMonitor::clipboardUpdated,
            this, &ClipboardAppController::clipboardUpdated);
    connect(monitor_, &ClipboardMonitor::clipboardMimeCompleted,
            this, [this](const QString &itemName, const QMap<QString, QByteArray> &extraFormats) {
        clipboardWidget_->mergeDeferredMimeFormats(itemName, extraFormats);
    });
}

void ClipboardAppController::initSound() {
    copySoundPlayer_ = new CopySoundPlayer(
        MPasteSettings::getInst()->isPlaySound(),
        QUrl(QStringLiteral("qrc:/resources/resources/sound.mp3")),
        this);
}

void ClipboardAppController::setupSyncWatcher() {
    const QString rootDir = QDir::cleanPath(MPasteSettings::getInst()->getSaveDir());
    if (rootDir.isEmpty()) {
        return;
    }

    if (!syncWatcher_) {
        syncWatcher_ = new SyncWatcher(this);
        connect(syncWatcher_, &SyncWatcher::syncRequested, this, [this]() {
            if (!parentWidget_->isVisible()) {
                syncWatcher_->setPendingReload();
                return;
            }
            syncHistoryBoardsIncremental();
        });
    }

    syncWatcher_->setBoardServices(
        clipboardWidget_ ? clipboardWidget_->boardServiceRef() : nullptr,
        staredWidget_ ? staredWidget_->boardServiceRef() : nullptr);

    const QStringList categoryDirs = {
        QDir::cleanPath(rootDir + QDir::separator() + MPasteSettings::CLIPBOARD_CATEGORY_NAME),
        QDir::cleanPath(rootDir + QDir::separator() + MPasteSettings::STAR_CATEGORY_NAME)
    };
    syncWatcher_->setup(rootDir, categoryDirs);
}

// ---------------------------------------------------------------------------
// Board signal wiring
// ---------------------------------------------------------------------------

void ClipboardAppController::connectBoardSignals(ScrollItemsWidget *boardWidget) {
    connect(boardWidget, &ScrollItemsWidget::doubleClicked,
            this, [this](const ClipboardItem &item) {
        if (pasteController_->setClipboard(item)) {
            emit pasteRequested();
        }
    });

    connect(boardWidget, &ScrollItemsWidget::plainTextPasteRequested,
            this, [this](const ClipboardItem &item) {
        if (pasteController_->setClipboard(item, true)) {
            emit pasteRequested();
        }
    });

    connect(boardWidget, &ScrollItemsWidget::ocrRequested,
            this, [this, boardWidget](const ClipboardItem &item) {
        handleOcrRequest(boardWidget, item);
    });

    connect(boardWidget, &ScrollItemsWidget::itemCountChanged,
            this, [this, boardWidget](int count) {
        emit boardItemCountChanged(boardWidget->getCategory(), count);
    });

    connect(boardWidget, &ScrollItemsWidget::selectionStateChanged,
            this, [this, boardWidget]() {
        emit boardSelectionStateChanged(boardWidget->getCategory());
    });

    connect(boardWidget, &ScrollItemsWidget::pageStateChanged,
            this, [this, boardWidget](int, int) {
        emit boardPageStateChanged(boardWidget->getCategory());
    });

    connect(boardWidget, &ScrollItemsWidget::itemStared,
            this, [this](const ClipboardItem &item) {
        ClipboardItem updatedItem(item);
        if (!hasUsableMimeData(updatedItem)) {
            ClipboardItem rehydrated = ClipboardItem::rehydrate(updatedItem);
            if (!rehydrated.getName().isEmpty()) {
                updatedItem = rehydrated;
            }
        }
        staredWidget_->addAndSaveItem(updatedItem);
        clipboardWidget_->setItemFavorite(updatedItem, true);
    });

    connect(boardWidget, &ScrollItemsWidget::itemUnstared,
            this, [this, boardWidget](const ClipboardItem &item) {
        staredWidget_->removeItemByContent(item);
        if (boardWidget == staredWidget_) {
            clipboardWidget_->setItemFavorite(item, false);
        }
    });

    connect(boardWidget, &ScrollItemsWidget::aliasChanged,
            this, [this, boardWidget](const QByteArray &fingerprint, const QString &alias) {
        for (auto *other : boardWidgetMap_.values()) {
            if (other && other != boardWidget) {
                other->syncAlias(fingerprint, alias);
            }
        }
    });

    connect(boardWidget, &ScrollItemsWidget::localPersistenceChanged,
            this, [this]() {
        if (syncWatcher_) {
            syncWatcher_->suppressReloadUntil(QDateTime::currentMSecsSinceEpoch() + 800);
        }
    });
}

// ---------------------------------------------------------------------------
// Clipboard slots
// ---------------------------------------------------------------------------

void ClipboardAppController::clipboardActivityObserved(int wId) {
    if (pasteController_->isPasting()) {
        return;
    }
    copySoundPlayer_->playCopySoundIfNeeded(wId);
}

void ClipboardAppController::clipboardUpdated(const ClipboardItem &nItem, int wId) {
    if (pasteController_->isPasting()) {
        return;
    }

    const QByteArray pastedFp = pasteController_->lastPastedFingerprint();
    if (!pastedFp.isEmpty() && nItem.fingerprint() == pastedFp) {
        pasteController_->clearLastPastedFingerprint();
        qInfo().noquote() << QStringLiteral("[clipboard-widget] clipboardUpdated suppressed echo wId=%1 fp=%2")
            .arg(wId)
            .arg(QString::fromLatin1(pastedFp.toHex().left(12)));
        return;
    }

    const bool added = clipboardWidget_->addAndSaveItem(nItem);
    qInfo().noquote() << QStringLiteral("[clipboard-widget] clipboardUpdated wId=%1 isPasting=%2 added=%3 %4")
        .arg(wId)
        .arg(pasteController_->isPasting())
        .arg(added)
        .arg(widgetItemSummary(nItem));

    if (added) {
        copiedWhenHide_ = true;

        // Auto OCR for image items when enabled.
        const ContentType ct = nItem.getContentType();
        if ((ct == Image || ct == Office)
            && MPasteSettings::getInst()->isAutoOcr()) {
            auto *bs = clipboardWidget_->boardServiceRef();
            const QString fp = bs ? bs->filePathForName(nItem.getName()) : QString();
            if (!fp.isEmpty() && OcrService::readSidecar(fp).status == OcrService::None) {
                ensureOcrService();
                QImage image;
                {
                    ClipboardItem memItem(nItem);
                    memItem.ensureMimeDataLoaded();
                    image = memItem.getImage().toImage();
                }
                if (image.isNull() || (image.width() <= 64 && image.height() <= 64)) {
                    if (QFile::exists(fp)) {
                        LocalSaver saver;
                        ClipboardItem diskItem = saver.loadFromFile(fp);
                        image = diskItem.getImage().toImage();
                    }
                }
                if (!image.isNull()) {
                    qInfo() << "[ocr] auto-ocr for" << nItem.getName();
                    if (auto *d = clipboardWidget_->cardDelegateRef()) {
                        d->markOcrPending(nItem.getName());
                    }
                    ocrService_->requestOcr(nItem.getName(), image);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Loading / sync
// ---------------------------------------------------------------------------

void ClipboardAppController::loadFromSaveDir() {
    // Start the starred board load first.  Once its async index build
    // completes, read the fingerprints and apply them to the clipboard
    // board so favorite markers are always based on the fresh index.
    staredWidget_->loadFromSaveDirDeferred();

    auto *starService = staredWidget_->boardServiceRef();
    if (starService) {
        connect(starService, &ClipboardBoardService::deferredLoadCompleted, this, [this]() {
            clipboardWidget_->setFavoriteFingerprints(staredWidget_->loadAllFingerprints());
        }, Qt::SingleShotConnection);
    }

    clipboardWidget_->loadFromSaveDirDeferred();
}

void ClipboardAppController::syncHistoryBoardsIncremental() {
    clipboardWidget_->syncFromDiskIncremental();
    staredWidget_->syncFromDiskIncremental();
}

void ClipboardAppController::onWidgetShown() {
    if (syncWatcher_ && syncWatcher_->checkPendingReload()) {
        syncHistoryBoardsIncremental();
    }
}

// ---------------------------------------------------------------------------
// Sealed interface delegations
// ---------------------------------------------------------------------------

bool ClipboardAppController::setClipboard(const ClipboardItem &item, bool plainText) {
    return pasteController_->setClipboard(item, plainText);
}

void ClipboardAppController::pasteToTarget(WId targetWindow) {
    pasteController_->pasteToTarget(targetWindow);
}

void ClipboardAppController::primeCurrentClipboard() {
    monitor_->primeCurrentClipboard();
}

// ---------------------------------------------------------------------------
// OCR
// ---------------------------------------------------------------------------

void ClipboardAppController::ensureOcrService() {
    if (ocrService_) {
        return;
    }
    ocrService_ = new OcrService(this);
    connect(ocrService_, &OcrService::ocrFinished,
            this, [this](const QString &itemName, const OcrService::Result &result) {
        qInfo() << "[ocr] finished item=" << itemName
                << "status=" << result.status
                << "textLen=" << result.text.length()
                << "error=" << result.errorMessage;
        if (result.status == OcrService::Ready) {
            bool written = false;
            for (auto *w : boardWidgetMap_.values()) {
                auto *bs = w->boardServiceRef();
                if (!bs) continue;
                const QString filePath = bs->filePathForName(itemName);
                if (QFile::exists(filePath)) {
                    OcrService::writeSidecar(filePath, result);
                    bs->refreshIndexedItemForPath(filePath);
                    written = true;
                }
            }
            if (!written && !result.text.isEmpty()) {
                QTimer::singleShot(2000, this, [this, itemName, result]() {
                    for (auto *w : boardWidgetMap_.values()) {
                        auto *bs = w->boardServiceRef();
                        if (!bs) continue;
                        const QString filePath = bs->filePathForName(itemName);
                        if (QFile::exists(filePath)) {
                            OcrService::writeSidecar(filePath, result);
                            bs->refreshIndexedItemForPath(filePath);
                        }
                    }
                });
            }
        }
        for (auto *w : boardWidgetMap_.values()) {
            if (auto *d = w->cardDelegateRef()) {
                d->clearOcrPending(itemName);
            }
        }
        const bool manual = manualOcrItems_.remove(itemName);
        if (manual) {
            if (result.status != OcrService::Ready || result.text.isEmpty()) {
                const QString msg = result.status == OcrService::Failed
                    ? result.errorMessage
                    : tr("No text recognized in this image.");
                emit ocrFailed(msg);
                return;
            }
            emit ocrCompleted(result.text);
        }
    });
}

void ClipboardAppController::handleOcrRequest(ScrollItemsWidget *boardWidget, const ClipboardItem &item) {
    ensureOcrService();
    // Check sidecar cache.
    {
        auto *bs = boardWidget->boardServiceRef();
        if (bs) {
            const QString fp = bs->filePathForName(item.getName());
            const OcrService::Result cached = OcrService::readSidecar(fp);
            if (cached.status == OcrService::Ready && !cached.text.isEmpty()) {
                emit ocrCompleted(cached.text);
                return;
            }
        }
    }
    // Load image.
    QImage image;
    auto *bs = boardWidget->boardServiceRef();
    if (bs) {
        const QString fp = bs->filePathForName(item.getName());
        if (QFile::exists(fp)) {
            LocalSaver saver;
            ClipboardItem diskItem = saver.loadFromFile(fp);
            image = diskItem.getImage().toImage();
        }
    }
    if (image.isNull() || (image.width() <= 64 && image.height() <= 64)) {
        ClipboardItem memItem(item);
        memItem.ensureMimeDataLoaded();
        QImage memImage = memItem.getImage().toImage();
        if (!memImage.isNull() && memImage.width() > image.width()) {
            image = memImage;
        }
    }
    if (image.isNull()) {
        image = item.thumbnail().toImage();
    }
    if (image.isNull()) {
        emit ocrFailed(tr("No image data available for OCR."));
        return;
    }
    emit ocrStarted();
    manualOcrItems_.insert(item.getName());
    ocrService_->requestOcr(item.getName(), image);
}

// showOcrResultDialog moved to MPasteWidget — the controller only emits signals.
