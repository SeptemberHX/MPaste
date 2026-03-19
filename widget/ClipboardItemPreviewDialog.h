// input: Depends on ClipboardItem data plus Qt dialog/text browser widgets.
// output: Exposes a centered read-only preview dialog for larger clipboard inspection with image zoom.
// pos: Widget-layer preview dialog used by item context actions and keyboard preview shortcuts.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDITEMPREVIEWDIALOG_H
#define MPASTE_CLIPBOARDITEMPREVIEWDIALOG_H

#include <QDialog>
#include "data/ClipboardItem.h"

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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QString uiText(const QString &source, const QString &zhFallback) const;
    void releasePreviewContent();
    void setImagePreview(const QImage &image);
    void updateImagePreview();
    void adjustImageZoom(qreal factor);
    void resetImageZoom();
    bool handleImageZoomKey(QKeyEvent *event);
    bool isImagePreviewActive() const;

    struct {
        QLabel *titleLabel = nullptr;
        QLabel *subtitleLabel = nullptr;
        QStackedLayout *contentLayout = nullptr;
        QTextBrowser *browser = nullptr;
        QScrollArea *imageScrollArea = nullptr;
        QLabel *imageLabel = nullptr;
        QToolButton *closeButton = nullptr;
    } ui_;

    QPoint dragOffset_;
    bool imageDragActive_ = false;
    QPoint imageDragStartPos_;
    QPoint imageDragStartScroll_;
    QImage imageOriginal_;
    qreal imageZoomFactor_ = 1.0;
    qreal imageFitScale_ = 1.0;
    quint64 previewToken_ = 0;
    bool darkTheme_ = false;
};

#endif // MPASTE_CLIPBOARDITEMPREVIEWDIALOG_H
