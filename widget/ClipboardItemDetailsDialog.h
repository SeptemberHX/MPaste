// input: Depends on Qt Widgets and ClipboardItem data to inspect normalized and raw clipboard content.
// output: Exposes the details dialog for one clipboard item: its content on the left, what it is on the right.
// pos: Widget-layer inspector dialog declaration used to understand and debug clipboard items.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDITEMDETAILSDIALOG_H
#define MPASTE_CLIPBOARDITEMDETAILSDIALOG_H

#include <QDialog>
#include <QPoint>

#include "data/ClipboardItem.h"

class QFrame;
class QGridLayout;
class QLabel;
class QMimeData;
class QMouseEvent;
class QPushButton;
class QStackedWidget;
class QButtonGroup;
class QTextEdit;
class QToolButton;
class QVBoxLayout;
class DetailsChips;
class DetailsFormatsView;
class ZoomableImageView;

class ClipboardItemDetailsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClipboardItemDetailsDialog(QWidget *parent = nullptr);
    void showItem(const ClipboardItem &item, int sequence = -1, int totalCount = -1);
    void applyTheme(bool dark);

signals:
    // The header's actions on the item shown; the board carries them out.
    void pasteRequested(const ClipboardItem &item);
    void copyRequested(const ClipboardItem &item);
    void ocrRequested(const ClipboardItem &item);
    void saveRequested(const ClipboardItem &item);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    enum Page { PreviewPage = 0, TextPage, RawPage, HtmlPage, FormatsPage };

    QString uiText(const QString &source, const QString &zhFallback) const;
    QString contentTypeLabel(ContentType type) const;
    QTextEdit *createReader(bool noWrap) const;
    void fillPreview(const ClipboardItem &item, const ClipboardItem &full);
    void fillInfo(const ClipboardItem &item, const QMimeData *mimeData, int sequence, int totalCount);
    void selectTab(int index);
    void addSection(const QString &title, const QList<QPair<QString, QString>> &rows);
    void clearInfo();
    void showPage(int index);

    struct {
        QFrame *card = nullptr;
        QLabel *iconLabel = nullptr;
        QLabel *titleLabel = nullptr;
        QLabel *subtitleLabel = nullptr;
        QWidget *actions = nullptr;
        QPushButton *pasteButton = nullptr;
        QPushButton *ocrButton = nullptr;
        QPushButton *saveButton = nullptr;
        QPushButton *openButton = nullptr;
        QToolButton *closeButton = nullptr;

        QButtonGroup *tabGroup = nullptr;
        QPushButton *tabs[5] = {};
        QLabel *hintLabel = nullptr;
        QStackedWidget *pages = nullptr;
        QStackedWidget *previewStack = nullptr;
        ZoomableImageView *imageView = nullptr;
        QTextEdit *previewText = nullptr;
        QLabel *previewCard = nullptr;
        QTextEdit *textEdit = nullptr;
        QTextEdit *rawEdit = nullptr;
        QTextEdit *htmlEdit = nullptr;
        DetailsFormatsView *formatsView = nullptr;

        QVBoxLayout *infoLayout = nullptr;
        QWidget *techSection = nullptr;
        DetailsChips *chips = nullptr;
        QLabel *fingerprintValue = nullptr;
    } ui_;
    ClipboardItem currentItem_;
    QString fingerprintHex_;
    QString openTarget_;
    QColor accent_;

    QPoint dragOffset_;
    bool darkTheme_ = false;
};

#endif // MPASTE_CLIPBOARDITEMDETAILSDIALOG_H
