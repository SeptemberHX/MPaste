// input: Depends on Qt Widgets, AuroraChrome and UrlTidy.
// output: Exposes the link preview: the page's picture on the left; its site, title, address, parameters and copy actions on the right.
// pos: Widget-layer page of ClipboardItemPreviewDialog, shown for links.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_LINKPREVIEWPANE_H
#define MPASTE_LINKPREVIEWPANE_H

#include <QColor>
#include <QPixmap>
#include <QUrl>
#include <QWidget>

class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpacerItem;
class QTextBrowser;
class LinkPicture;

class LinkPreviewPane : public QWidget {
    Q_OBJECT

public:
    explicit LinkPreviewPane(QWidget *parent = nullptr);

    // picture: the page's own (the card's), or none for the site's icon on a band.
    void setLink(const QUrl &url, const QString &title, const QPixmap &favicon,
                 const QPixmap &picture, const QColor &accent, bool chinese);
    void clear();
    void applyTheme(bool dark);

    QUrl url() const { return url_; }
    QString title() const { return title_; }
    bool hasTrackingParameters() const { return hasTracking_; }
    // [title](address), or the address alone without a title.
    QString markdown() const;
    QString cleanAddress() const;

    // The height the pane needs at this width (the dialog sizes itself to it).
    int preferredHeight(int width) const;

signals:
    void copyTextRequested(const QString &text);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildAddress();
    void rebuildParameters();
    void fitAddressHeight();
    void fitAddressHeight(int width);
    static int pictureWidthFor(int width);

    LinkPicture *picture_ = nullptr;
    QSpacerItem *pictureSpace_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QWidget *column_ = nullptr;
    QLabel *site_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *addressHeading_ = nullptr;
    QTextBrowser *address_ = nullptr;
    QWidget *parametersHeader_ = nullptr;
    QLabel *parametersHeading_ = nullptr;
    QLabel *parametersNote_ = nullptr;
    QPushButton *cleanButton_ = nullptr;
    QFrame *parametersPanel_ = nullptr;
    QGridLayout *parametersGrid_ = nullptr;
    QPushButton *copyTitleButton_ = nullptr;

    QUrl url_;
    QString title_;
    bool hasTracking_ = false;
    bool dark_ = true;
    bool chinese_ = true;
};

#endif // MPASTE_LINKPREVIEWPANE_H
