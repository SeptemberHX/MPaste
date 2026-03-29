// input: Depends on ClipboardItem, QMimeData, and platform paste helpers.
// output: Provides clipboard paste orchestration (set clipboard, trigger paste).
// pos: Widget-layer helper owned by MPasteWidget.
// update: If I change, update this header block.
#ifndef CLIPBOARDPASTECONTROLLER_H
#define CLIPBOARDPASTECONTROLLER_H

#include <QObject>
#include <QMimeData>
#include <QWindow>   // for WId
#include "data/ClipboardItem.h"

class ClipboardMonitor;

class ClipboardPasteController : public QObject {
    Q_OBJECT

public:
    explicit ClipboardPasteController(ClipboardMonitor *monitor, QObject *parent = nullptr);
    ~ClipboardPasteController() override = default;

    /// Write the item's data to the system clipboard.  Returns true on success.
    bool setClipboard(const ClipboardItem &item, bool plainText = false);

    /// Paste to the previously-active window (hide, restore focus, Ctrl+V).
    void pasteToTarget(WId targetWindow);

    bool isPasting() const { return isPasting_; }
    QByteArray lastPastedFingerprint() const { return lastPastedFingerprint_; }
    void clearLastPastedFingerprint() { lastPastedFingerprint_.clear(); }

signals:
    void pastingStarted();
    void pastingFinished();

private:
    QMimeData *createPlainTextMimeData(const ClipboardItem &item) const;
    void handleUrlsClipboard(QMimeData *mimeData, const ClipboardItem &item);

    ClipboardMonitor *monitor_ = nullptr;
    bool isPasting_ = false;
    QByteArray lastPastedFingerprint_;
};

#endif // CLIPBOARDPASTECONTROLLER_H
