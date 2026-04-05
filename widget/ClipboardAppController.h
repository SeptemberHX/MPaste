#ifndef MPASTE_CLIPBOARD_APP_CONTROLLER_H
#define MPASTE_CLIPBOARD_APP_CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QSet>

#include "data/ClipboardItem.h"

class QDialog;
class ClipboardMonitor;
class ClipboardPasteController;
class CopySoundPlayer;
class OcrService;
class SyncWatcher;
class ScrollItemsWidget;
class ClipboardCardDelegate;

/// Application-level orchestration extracted from MPasteWidget.
///
/// Owns all non-UI service objects (clipboard monitor, paste controller,
/// sound player, OCR service, sync watcher) and the wiring between them
/// and the board widgets.  Does NOT own the board widgets themselves.
class ClipboardAppController : public QObject {
    Q_OBJECT

public:
    explicit ClipboardAppController(
        const QMap<QString, ScrollItemsWidget*> &boardWidgetMap,
        ScrollItemsWidget *clipboardWidget,
        ScrollItemsWidget *staredWidget,
        QWidget *parentWidget,
        QObject *parent = nullptr);
    ~ClipboardAppController() override;

    // --- Accessors for MPasteWidget ---
    ClipboardPasteController *pasteController() const { return pasteController_; }
    bool copiedWhenHide() const { return copiedWhenHide_; }
    void clearCopiedWhenHide() { copiedWhenHide_ = false; }
    SyncWatcher *syncWatcher() const { return syncWatcher_; }
    ClipboardMonitor *monitor() const { return monitor_; }

    // --- Actions ---
    void loadFromSaveDir();
    void syncHistoryBoardsIncremental();
    void onWidgetShown();

    // --- Board signal wiring ---
    void connectBoardSignals(ScrollItemsWidget *boardWidget);

signals:
    /// Emitted when a board action wants to paste (MPasteWidget connects
    /// this to hideAndPaste).
    void pasteRequested();

    /// Forwarded from boards so MPasteWidget can update the status bar.
    void boardItemCountChanged(const QString &category, int count);
    void boardSelectionStateChanged(const QString &category);
    void boardPageStateChanged(const QString &category);

private slots:
    void clipboardActivityObserved(int wId);
    void clipboardUpdated(const ClipboardItem &item, int wId);

private:
    void initClipboard();
    void initSound();
    void setupSyncWatcher();
    void ensureOcrService();
    void showOcrResultDialog(const QString &text);
    void handleOcrRequest(ScrollItemsWidget *boardWidget, const ClipboardItem &item);

    QWidget *parentWidget_;
    QMap<QString, ScrollItemsWidget*> boardWidgetMap_;
    ScrollItemsWidget *clipboardWidget_;
    ScrollItemsWidget *staredWidget_;

    ClipboardMonitor *monitor_ = nullptr;
    ClipboardPasteController *pasteController_ = nullptr;
    CopySoundPlayer *copySoundPlayer_ = nullptr;
    OcrService *ocrService_ = nullptr;
    SyncWatcher *syncWatcher_ = nullptr;

    QPointer<QDialog> ocrLoadingDialog_;
    QSet<QString> manualOcrItems_;
    bool copiedWhenHide_ = false;
};

#endif // MPASTE_CLIPBOARD_APP_CONTROLLER_H
