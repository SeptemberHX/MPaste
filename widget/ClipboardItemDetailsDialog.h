// input: Depends on Qt Widgets and ClipboardItem data to inspect normalized and raw clipboard content.
// output: Exposes a lightweight details dialog for one clipboard item.
// pos: Widget-layer inspector dialog declaration used to debug and understand clipboard items.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDITEMDETAILSDIALOG_H
#define MPASTE_CLIPBOARDITEMDETAILSDIALOG_H

#include <QDialog>
#include <QPoint>

#include "data/ClipboardItem.h"

class QFrame;
class QMimeData;
class QMouseEvent;
class QTextEdit;
class QLabel;
class QToolButton;
class QTabWidget;

class ClipboardItemDetailsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClipboardItemDetailsDialog(QWidget *parent = nullptr);
    void showItem(const ClipboardItem &item);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QString uiText(const QString &source, const QString &zhFallback) const;
    QString contentTypeLabel(ClipboardItem::ContentType type) const;
    QString joinUrls(const QList<QUrl> &urls) const;
    QString byteCountLabel(qint64 bytes) const;
    QString mimeFormatsReport(const QMimeData *mimeData) const;
    qint64 totalMimeBytes(const QMimeData *mimeData) const;
    QTextEdit *createReadOnlyEditor(bool noWrap = false) const;
    void updatePreviewVisual(const ClipboardItem &item);

    struct {
        QFrame *card = nullptr;
        QLabel *titleLabel = nullptr;
        QToolButton *closeButton = nullptr;
        QTabWidget *tabs = nullptr;

        QLabel *previewVisual = nullptr;
        QLabel *previewSummaryValue = nullptr;
        QLabel *typeValue = nullptr;
        QLabel *timeValue = nullptr;
        QLabel *nameValue = nullptr;
        QLabel *titleValue = nullptr;
        QLabel *urlValue = nullptr;
        QLabel *fingerprintValue = nullptr;
        QLabel *formatCountValue = nullptr;
        QLabel *mimeBytesValue = nullptr;
        QLabel *textLengthValue = nullptr;
        QLabel *urlCountValue = nullptr;

        QTextEdit *normalizedTextEdit = nullptr;
        QTextEdit *normalizedUrlsEdit = nullptr;
        QTextEdit *rawTextEdit = nullptr;
        QTextEdit *rawHtmlEdit = nullptr;
        QTextEdit *rawUrlsEdit = nullptr;
        QTextEdit *mimeFormatsEdit = nullptr;
    } ui_;

    QPoint dragOffset_;
};

#endif // MPASTE_CLIPBOARDITEMDETAILSDIALOG_H
