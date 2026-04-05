#include "ClipboardAppController.h"

#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "ClipboardCardDelegate.h"
#include "CopySoundPlayer.h"
#include "ScrollItemsWidget.h"
#include "SyncWatcher.h"
#include "utils/ClipboardBoardService.h"
#include "ClipboardPasteController.h"
#include "WindowBlurHelper.h"
#include "data/LocalSaver.h"
#include "utils/ClipboardMonitor.h"
#include "utils/MPasteSettings.h"
#include "utils/OcrService.h"

// ---------------------------------------------------------------------------
// OcrResultDialog — frosted-glass dialog for manual OCR results
// ---------------------------------------------------------------------------

class OcrResultDialog : public QDialog {
public:
    bool dark = false;
    QPoint dragOffset_;
    using QDialog::QDialog;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal r = 8.0;
        QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(dark ? QColor(255,255,255,40) : QColor(0,0,0,25), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect, r, r);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 1));
        p.drawRoundedRect(rect, r, r);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            dragOffset_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
            e->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->buttons() & Qt::LeftButton) {
            move(e->globalPosition().toPoint() - dragOffset_);
            e->accept();
        }
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) { close(); return; }
        QDialog::keyPressEvent(e);
    }
};

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

ClipboardItem rehydrateClipboardItem(const ClipboardItem &item) {
    if (item.getName().isEmpty()) {
        return {};
    }
    LocalSaver saver;
    const QString sourceFilePath = item.sourceFilePath();
    if (!sourceFilePath.isEmpty() && QFileInfo::exists(sourceFilePath)) {
        ClipboardItem loaded = saver.loadFromFile(sourceFilePath);
        if (!loaded.getName().isEmpty()) {
            return loaded;
        }
    }
    const QString rootDir = QDir::cleanPath(MPasteSettings::getInst()->getSaveDir());
    if (rootDir.isEmpty()) {
        return {};
    }
    const QStringList categories = {
        MPasteSettings::STAR_CATEGORY_NAME,
        MPasteSettings::CLIPBOARD_CATEGORY_NAME
    };
    for (const QString &category : categories) {
        const QString filePath = QDir::cleanPath(rootDir + QDir::separator()
                                                 + category + QDir::separator()
                                                 + item.getName() + ".mpaste");
        if (!QFileInfo::exists(filePath)) {
            continue;
        }
        ClipboardItem loaded = saver.loadFromFile(filePath);
        if (!loaded.getName().isEmpty()) {
            return loaded;
        }
    }
    return {};
}

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
            ClipboardItem rehydrated = rehydrateClipboardItem(updatedItem);
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
            if (ocrLoadingDialog_) {
                ocrLoadingDialog_->close();
                ocrLoadingDialog_ = nullptr;
            }
            if (result.status != OcrService::Ready || result.text.isEmpty()) {
                const QString msg = result.status == OcrService::Failed
                    ? result.errorMessage
                    : tr("No text recognized in this image.");
                QMessageBox::warning(parentWidget_, tr("OCR"), msg);
                return;
            }
            showOcrResultDialog(result.text);
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
                showOcrResultDialog(cached.text);
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
        QMessageBox::warning(parentWidget_, tr("OCR"), tr("No image data available for OCR."));
        return;
    }
    // Show loading dialog.
    if (ocrLoadingDialog_) {
        ocrLoadingDialog_->close();
    }
    {
        const bool dark = MPasteSettings::getInst()->isDarkTheme();
        auto *dlg = new OcrResultDialog(parentWidget_);
        dlg->dark = dark;
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setAttribute(Qt::WA_TranslucentBackground);
        dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
        dlg->setModal(false);
        dlg->resize(320, 120);

        auto *lay = new QVBoxLayout(dlg);
        lay->setAlignment(Qt::AlignCenter);
        auto *label = new QLabel(tr("Recognizing..."), dlg);
        label->setAlignment(Qt::AlignCenter);
        const QString color = dark ? QStringLiteral("#E6EDF5") : QStringLiteral("#1E2936");
        label->setStyleSheet(QStringLiteral("color: %1; font-size: 18px; background: transparent;").arg(color));
        lay->addWidget(label);

        QScreen *screen = nullptr;
        if (auto *w = parentWidget_->window()->windowHandle()) screen = w->screen();
        if (!screen) screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect geo = screen->availableGeometry();
            dlg->move(geo.center() - QPoint(dlg->width() / 2, dlg->height() / 2));
        }
        dlg->show();
        WindowBlurHelper::enableBlurBehind(dlg, dark);
        dlg->raise();
        ocrLoadingDialog_ = dlg;
    }
    manualOcrItems_.insert(item.getName());
    ocrService_->requestOcr(item.getName(), image);
}

void ClipboardAppController::showOcrResultDialog(const QString &text) {
    const bool dark = MPasteSettings::getInst()->isDarkTheme();

    auto *dialog = new OcrResultDialog(parentWidget_);
    dialog->dark = dark;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAttribute(Qt::WA_TranslucentBackground);
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    dialog->setModal(false);
    dialog->resize(720, 520);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(12);

    auto *headerLayout = new QHBoxLayout;
    auto *titleLabel = new QLabel(tr("Recognized Text"), dialog);
    titleLabel->setObjectName(QStringLiteral("previewTitle"));
    headerLayout->addWidget(titleLabel, 1);

    auto *copyBtn = new QToolButton(dialog);
    copyBtn->setObjectName(QStringLiteral("closeButton"));
    copyBtn->setText(QStringLiteral("\u2398"));
    copyBtn->setToolTip(tr("Copy to Clipboard"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QToolButton::clicked, dialog, [text, copyBtn]() {
        QGuiApplication::clipboard()->setText(text);
        copyBtn->setText(QStringLiteral("\u2713"));
        QTimer::singleShot(1200, copyBtn, [copyBtn]() {
            if (copyBtn) copyBtn->setText(QStringLiteral("\u2398"));
        });
    });
    headerLayout->addWidget(copyBtn, 0, Qt::AlignTop);

    auto *closeBtn = new QToolButton(dialog);
    closeBtn->setObjectName(QStringLiteral("closeButton"));
    closeBtn->setText(QStringLiteral("\u00D7"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, dialog, &QDialog::close);
    headerLayout->addWidget(closeBtn, 0, Qt::AlignTop);

    layout->addLayout(headerLayout);

    auto *textEdit = new QTextEdit(dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(text);
    textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    textEdit->setFrameShape(QFrame::NoFrame);
    textEdit->setObjectName(QStringLiteral("ocrTextEdit"));
    layout->addWidget(textEdit, 1);

    const QString textColor = dark ? QStringLiteral("#E6EDF5") : QStringLiteral("#1E2936");
    const QString subtextColor = dark ? QStringLiteral("#9AA7B5") : QStringLiteral("#5E7084");
    const QString borderColor = dark ? QStringLiteral("rgba(255,255,255,25)")
                                     : QStringLiteral("rgba(0,0,0,18)");
    const QString btnBg = dark ? QStringLiteral("rgba(255,255,255,14)")
                               : QStringLiteral("rgba(0,0,0,8)");
    const QString btnHoverBg = dark ? QStringLiteral("rgba(255,255,255,28)")
                                    : QStringLiteral("rgba(0,0,0,16)");
    const QString selBg = dark ? QStringLiteral("rgba(74,144,226,110)")
                               : QStringLiteral("rgba(74,144,226,76)");

    dialog->setStyleSheet(QStringLiteral(
        "QLabel#previewTitle {"
        "  color: %1; font-size: 22px; font-weight: 700; background: transparent;"
        "}"
        "QTextEdit#ocrTextEdit {"
        "  background-color: transparent; border: none;"
        "  color: %1; font-size: 15px;"
        "  selection-background-color: %2;"
        "  padding: 8px;"
        "}"
        "QToolButton#closeButton {"
        "  background-color: %3; border: 1px solid %4;"
        "  border-radius: 14px; color: %5;"
        "  font-size: 17px; font-weight: 700;"
        "  min-width: 28px; min-height: 28px;"
        "}"
        "QToolButton#closeButton:hover {"
        "  background-color: %6; border-color: %4;"
        "}"
    ).arg(textColor, selBg, btnBg, borderColor, subtextColor, btnHoverBg));

    QScreen *screen = nullptr;
    if (auto *w = parentWidget_->window()->windowHandle()) {
        screen = w->screen();
    }
    if (!screen) screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect geo = screen->availableGeometry();
        dialog->move(geo.center() - QPoint(dialog->width() / 2, dialog->height() / 2));
    }

    dialog->show();
    WindowBlurHelper::enableBlurBehind(dialog, dark);
    dialog->raise();
    dialog->activateWindow();
}
