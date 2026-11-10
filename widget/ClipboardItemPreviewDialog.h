// input: Depends on ClipboardItem data plus Qt dialog/text browser widgets.
// output: Exposes a centered read-only preview dialog for larger clipboard inspection with image zoom.
// pos: Widget-layer preview dialog used by item context actions and keyboard preview shortcuts.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDITEMPREVIEWDIALOG_H
#define MPASTE_CLIPBOARDITEMPREVIEWDIALOG_H

#include <QDialog>
#include "data/ClipboardItem.h"

class LinkPreviewPane;
class ZoomableImageView;
class QLabel;
class QScrollArea;
class QStackedLayout;
class QTextBrowser;
class QToolButton;

class ClipboardItemPreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClipboardItemPreviewDialog(QWidget *parent = nullptr);
    void showItem(const ClipboardItem &item);
    static bool supportsPreview(const ClipboardItem &item);
    void reject() override;
    void applyTheme(bool dark);

signals:
    // The header's actions on the item shown; the board carries them out.
    void pasteRequested(const ClipboardItem &item);
    void copyRequested(const ClipboardItem &item);
    void ocrRequested(const ClipboardItem &item);
    void saveRequested(const ClipboardItem &item);
    // A link's text forms (its address as Markdown, without tracking, ...):
    // pasted or put on the clipboard as plain text.
    void pasteTextRequested(const QString &text);
    void copyTextRequested(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QString uiText(const QString &source, const QString &zhFallback) const;
    void releasePreviewContent();
    bool handleImageZoomKey(QKeyEvent *event);
    bool isImagePreviewActive() const;
    bool isLinkPreviewActive() const;
    // Links get their own page: picture, site, title, address and parameters.
    bool showLink(const ClipboardItem &item);
    void setFooterHint(bool link);
    // The window height that shows the loaded content whole, within the screen.
    int heightForContent() const;
    // Sizes the window to its content and puts it on screen (centred on first show).
    void revealSizedToContent();

    struct {
        QLabel *titleLabel = nullptr;
        QLabel *subtitleLabel = nullptr;
        QStackedLayout *contentLayout = nullptr;
        QTextBrowser *browser = nullptr;
        ZoomableImageView *imageView = nullptr;
        LinkPreviewPane *linkPane = nullptr;
        QWidget *openButton = nullptr;
        QToolButton *closeButton = nullptr;
        QLabel *iconLabel = nullptr;
        QWidget *actions = nullptr;
        QWidget *ocrButton = nullptr;
        QWidget *saveButton = nullptr;
        QLabel *footerLabel = nullptr;
    } ui_;
    ClipboardItem currentItem_;
    QColor accent_;
    QRect screenArea_;
    bool revealPending_ = false;

    QPoint dragOffset_;
    quint64 previewToken_ = 0;
    bool darkTheme_ = false;
};

#endif // MPASTE_CLIPBOARDITEMPREVIEWDIALOG_H
