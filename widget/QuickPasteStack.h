// input: Depends on ClipboardBoardService (recent items), ClipboardItem, theme and blur helpers.
// output: The quick paste stack: recent clipboard items beside the text caret, pasted by number.
// pos: Widget-layer second activation layout ("MPaste Quick"); the GNOME companion places it on Wayland.
// update: If I change, update this header block and widget/README.md.
#ifndef MPASTE_QUICKPASTESTACK_H
#define MPASTE_QUICKPASTESTACK_H

#include <QHash>
#include <QList>
#include <QPointer>
#include <QWidget>

#include "data/ClipboardItem.h"

class ClipboardBoardService;

class QuickPasteStack : public QWidget {
    Q_OBJECT

public:
    explicit QuickPasteStack(ClipboardBoardService *service, QWidget *parent = nullptr);

    // Reloads the most recent items and shows the stack beside the caret.
    void popup();

signals:
    void pasteRequested(const ClipboardItem &item, bool plainText);
    void expandRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Entry {
        QString filePath;
        ClipboardItem item;
        QString title;
        QString searchText;
        QColor swatch;      // valid for colour items
        bool mono = false;  // code or a shell command: set in a monospace face
    };

    void reload();
    void applyFilter();
    void updateSize();
    void placeBesideCaret();
    void paste(int visibleIndex, bool plainText);
    void handleThumbnail(const QString &name, const QPixmap &thumbnail);

    QRect rowRect(int visibleIndex) const;
    int rowAt(const QPoint &pos) const;
    QString subtitleFor(const Entry &entry) const;
    void paintTile(QPainter &p, const QRect &rect, const Entry &entry) const;
    void paintExpanded(QPainter &p, const QRect &rect, const Entry &entry, int number) const;
    void paintRow(QPainter &p, const QRect &rect, const Entry &entry, int number, bool hovered) const;

    QPointer<ClipboardBoardService> service_;
    QList<Entry> entries_;          // most recent first
    QList<int> visible_;            // indexes into entries_, at most 9
    QString filter_;
    int selected_ = 0;              // index into visible_
    int hovered_ = -1;
    bool dark_ = true;
};

#endif // MPASTE_QUICKPASTESTACK_H
