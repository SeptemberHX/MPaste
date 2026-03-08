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
class QTextEdit;
class QLabel;
class QToolButton;

class ClipboardItemDetailsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClipboardItemDetailsDialog(QWidget *parent = nullptr);
    void showItem(const ClipboardItem &item);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QString uiText(const QString &source, const QString &zhFallback) const;
    QString contentTypeLabel(ClipboardItem::ContentType type) const;
    QString joinUrls(const QList<QUrl> &urls) const;
    QTextEdit *createReadOnlyEditor() const;

    struct {
        QFrame *card = nullptr;
        QLabel *titleLabel = nullptr;
        QToolButton *closeButton = nullptr;
        QLabel *typeValue = nullptr;
        QLabel *timeValue = nullptr;
        QLabel *titleValue = nullptr;
        QLabel *urlValue = nullptr;
        QTextEdit *normalizedTextEdit = nullptr;
        QTextEdit *normalizedUrlsEdit = nullptr;
        QTextEdit *mimeFormatsEdit = nullptr;
    } ui_;

    QPoint dragOffset_;
};

#endif // MPASTE_CLIPBOARDITEMDETAILSDIALOG_H
