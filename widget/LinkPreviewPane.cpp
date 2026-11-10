// input: Depends on LinkPreviewPane.h, UrlTidy and Qt Widgets.
// output: Implements the link preview page: picture, site, title, address with its parameters, and copy actions.
// pos: Widget-layer implementation of the preview dialog's link page.
// update: If I change, update this header block and my folder README.md.
#include "LinkPreviewPane.h"

#include "utils/UrlTidy.h"

#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpacerItem>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtMath>

namespace {
constexpr int kPictureWidth = 400;   // at the dialog's full width; less when narrower
constexpr int kColumnSpacing = 22;
constexpr int kMinimumHeight = 280;
constexpr qreal kPictureRadius = 14.0;

QString tr2(bool chinese, const char *en, const QString &zh) {
    return chinese ? zh : QString::fromUtf8(en);
}

QFont monospaceFont(int pixelSize) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(pixelSize);
    return font;
}

// What the site's band shows without a picture: its first letter.
QString initialOf(const QString &host) {
    QString name = host;
    if (name.startsWith(QLatin1String("www."))) {
        name = name.mid(4);
    }
    return name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper();
}
} // namespace

// The page's picture fitted whole into a rounded well, or the site's icon and
// name on a band of the source app's colour when the page has none.
class LinkPicture : public QWidget {
public:
    using QWidget::QWidget;

    void setContent(const QPixmap &picture, const QPixmap &favicon, const QString &host, const QColor &accent) {
        picture_ = picture;
        favicon_ = favicon;
        host_ = host;
        accent_ = accent.isValid() ? accent : QColor(QStringLiteral("#5B8DEF"));
        update();
    }
    void clear() { setContent({}, {}, {}, {}); }
    bool hasPicture() const { return !picture_.isNull(); }
    void setDark(bool dark) { dark_ = dark; update(); }

    // The height that shows the picture whole at this width.
    int heightForPictureWidth(int width) const {
        if (picture_.isNull()) {
            return kMinimumHeight;
        }
        const QSizeF size = picture_.deviceIndependentSize();
        return size.width() > 0 ? qRound(width * size.height() / size.width()) : kMinimumHeight;
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(area, kPictureRadius, kPictureRadius);

        if (!picture_.isNull()) {
            p.fillPath(clip, dark_ ? QColor(255, 255, 255, 9) : QColor(0, 0, 0, 8));
            const QSizeF size = picture_.deviceIndependentSize().scaled(area.size(), Qt::KeepAspectRatio);
            const QRectF target(area.center() - QPointF(size.width() / 2, size.height() / 2), size);
            p.save();
            p.setClipPath(clip);
            p.drawPixmap(target, picture_, QRectF(picture_.rect()));
            p.restore();
        } else {
            QColor band = accent_;
            band.setAlphaF(dark_ ? 0.16 : 0.12);
            p.fillPath(clip, band);
            const qreal tile = 76;
            const QRectF tileRect(area.center().x() - tile / 2, area.center().y() - tile / 2 - 18, tile, tile);
            QPainterPath tilePath;
            tilePath.addRoundedRect(tileRect, 20, 20);
            if (!favicon_.isNull()) {
                p.fillPath(tilePath, dark_ ? QColor(255, 255, 255, 230) : QColor(255, 255, 255));
                const QRectF iconRect = tileRect.adjusted(16, 16, -16, -16);
                p.drawPixmap(iconRect, favicon_, QRectF(favicon_.rect()));
            } else {
                p.fillPath(tilePath, accent_);
                QFont font = this->font();
                font.setPixelSize(34);
                font.setWeight(QFont::Bold);
                p.setFont(font);
                p.setPen(Qt::white);
                p.drawText(tileRect, Qt::AlignCenter, initialOf(host_));
            }
            QFont font = this->font();
            font.setPixelSize(14);
            p.setFont(font);
            p.setPen(dark_ ? QColor(255, 255, 255, 170) : QColor(28, 30, 37, 170));
            const QRectF hostRect(area.left() + 16, tileRect.bottom() + 14, area.width() - 32, 22);
            p.drawText(hostRect, Qt::AlignHCenter | Qt::AlignTop,
                       p.fontMetrics().elidedText(host_, Qt::ElideMiddle, int(hostRect.width())));
        }
        QPen edge(dark_ ? QColor(255, 255, 255, 16) : QColor(0, 0, 0, 16));
        edge.setWidthF(1.0);
        p.setPen(edge);
        p.setBrush(Qt::NoBrush);
        p.drawPath(clip);
    }

private:
    QPixmap picture_;
    QPixmap favicon_;
    QString host_;
    QColor accent_;
    bool dark_ = true;
};

LinkPreviewPane::LinkPreviewPane(QWidget *parent)
    : QWidget(parent) {
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kColumnSpacing);

    // Placed by hand (resizeEvent), so its height never holds the window up:
    // the layout only keeps its width free.
    picture_ = new LinkPicture(this);
    // The gap after it is part of its space: a box layout adds no spacing next to a spacer.
    pictureSpace_ = new QSpacerItem(kPictureWidth + kColumnSpacing, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    row->addItem(pictureSpace_);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("linkScroll"));
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFocusPolicy(Qt::NoFocus);
    column_ = new QWidget(scroll_);
    column_->setObjectName(QStringLiteral("linkColumn"));
    auto *column = new QVBoxLayout(column_);
    column->setContentsMargins(0, 2, 10, 2);
    column->setSpacing(10);
    // As wide as the space beside the picture, never wider: a long title or
    // the button row would otherwise push the column past the window's edge.
    column->setSizeConstraint(QLayout::SetNoConstraint);
    column_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    scroll_->setWidget(column_);
    row->addWidget(scroll_, 1);

    site_ = new QLabel(column_);
    site_->setObjectName(QStringLiteral("linkHost"));
    site_->setTextFormat(Qt::PlainText);
    column->addWidget(site_);

    titleLabel_ = new QLabel(column_);
    titleLabel_->setObjectName(QStringLiteral("linkTitle"));
    titleLabel_->setTextFormat(Qt::PlainText);
    titleLabel_->setWordWrap(true);
    titleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(titleLabel_);
    column->addSpacing(6);

    addressHeading_ = new QLabel(column_);
    addressHeading_->setObjectName(QStringLiteral("auroraSection"));
    column->addWidget(addressHeading_);

    // A text view rather than a label: an address has no spaces to wrap at.
    address_ = new QTextBrowser(column_);
    address_->setObjectName(QStringLiteral("auroraWell"));
    address_->setFont(monospaceFont(13));
    address_->document()->setDefaultFont(monospaceFont(13));
    address_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    address_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    address_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    address_->setFocusPolicy(Qt::NoFocus);
    address_->setOpenLinks(false);
    address_->setCursorWidth(0);
    address_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    address_->viewport()->setCursor(Qt::IBeamCursor);
    column->addWidget(address_);
    column->addSpacing(6);

    // "Parameters N" and the clean-link button on one line, what the
    // tracking ones are on the next.
    parametersHeader_ = new QWidget(column_);
    auto *headerBlock = new QVBoxLayout(parametersHeader_);
    headerBlock->setContentsMargins(0, 0, 0, 0);
    headerBlock->setSpacing(4);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(10);
    parametersHeading_ = new QLabel(parametersHeader_);
    parametersHeading_->setObjectName(QStringLiteral("auroraSection"));
    header->addWidget(parametersHeading_);
    header->addStretch(1);
    cleanButton_ = new QPushButton(parametersHeader_);
    cleanButton_->setObjectName(QStringLiteral("auroraPrimary"));
    cleanButton_->setCursor(Qt::PointingHandCursor);
    cleanButton_->setFocusPolicy(Qt::NoFocus);
    connect(cleanButton_, &QPushButton::clicked, this, [this]() { emit copyTextRequested(cleanAddress()); });
    header->addWidget(cleanButton_, 0, Qt::AlignVCenter);
    headerBlock->addLayout(header);
    parametersNote_ = new QLabel(parametersHeader_);
    parametersNote_->setObjectName(QStringLiteral("auroraHint"));
    parametersNote_->setWordWrap(true);
    parametersNote_->setContentsMargins(4, 0, 0, 0);
    headerBlock->addWidget(parametersNote_);
    column->addWidget(parametersHeader_);

    parametersPanel_ = new QFrame(column_);
    parametersPanel_->setObjectName(QStringLiteral("auroraPanel"));
    parametersGrid_ = new QGridLayout(parametersPanel_);
    parametersGrid_->setContentsMargins(14, 10, 14, 10);
    parametersGrid_->setHorizontalSpacing(18);
    parametersGrid_->setVerticalSpacing(8);
    parametersGrid_->setColumnStretch(1, 1);
    column->addWidget(parametersPanel_);

    column->addStretch(1);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 8, 0, 0);
    buttons->setSpacing(8);
    auto addButton = [this, buttons]() {
        auto *button = new QPushButton(column_);
        button->setObjectName(QStringLiteral("auroraSecondary"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(button);
        return button;
    };
    QPushButton *copyAddress = addButton();
    copyAddress->setProperty("linkRole", QStringLiteral("address"));
    connect(copyAddress, &QPushButton::clicked, this, [this]() { emit copyTextRequested(url_.toString(QUrl::FullyEncoded)); });
    QPushButton *copyMarkdown = addButton();
    copyMarkdown->setProperty("linkRole", QStringLiteral("markdown"));
    connect(copyMarkdown, &QPushButton::clicked, this, [this]() { emit copyTextRequested(markdown()); });
    copyTitleButton_ = addButton();
    copyTitleButton_->setProperty("linkRole", QStringLiteral("title"));
    connect(copyTitleButton_, &QPushButton::clicked, this, [this]() { emit copyTextRequested(title_); });
    buttons->addStretch(1);
    column->addLayout(buttons);

    applyTheme(true);
}

void LinkPreviewPane::setLink(const QUrl &url, const QString &title, const QPixmap &favicon,
                              const QPixmap &picture, const QColor &accent, bool chinese) {
    url_ = url;
    title_ = title.simplified();
    chinese_ = chinese;
    hasTracking_ = UrlTidy::hasTrackingParameters(url_);
    const QString host = url_.host(QUrl::FullyDecoded);

    picture_->setContent(picture, favicon, host, accent);
    site_->setText(host);
    site_->setVisible(!title_.isEmpty());   // without a title the site is the title
    titleLabel_->setText(title_.isEmpty() ? host : title_);
    addressHeading_->setText(tr2(chinese_, "Address", QStringLiteral("网址")));
    cleanButton_->setText(tr2(chinese_, "Copy clean link", QStringLiteral("复制干净链接")));
    for (QPushButton *button : column_->findChildren<QPushButton *>()) {
        const QString role = button->property("linkRole").toString();
        if (role == QLatin1String("address")) {
            button->setText(tr2(chinese_, "Copy address", QStringLiteral("复制网址")));
        } else if (role == QLatin1String("markdown")) {
            button->setText(tr2(chinese_, "Copy as Markdown", QStringLiteral("复制为 Markdown")));
        } else if (role == QLatin1String("title")) {
            button->setText(tr2(chinese_, "Copy title", QStringLiteral("复制标题")));
        }
    }
    copyTitleButton_->setVisible(!title_.isEmpty());

    rebuildAddress();
    rebuildParameters();
    scroll_->verticalScrollBar()->setValue(0);
}

void LinkPreviewPane::clear() {
    url_.clear();
    title_.clear();
    hasTracking_ = false;
    picture_->clear();
    address_->clear();
}

QString LinkPreviewPane::markdown() const {
    const QString address = url_.toString(QUrl::FullyEncoded);
    if (title_.isEmpty()) {
        return address;
    }
    QString text = title_;
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('['), QStringLiteral("\\["));
    text.replace(QLatin1Char(']'), QStringLiteral("\\]"));
    return QStringLiteral("[%1](%2)").arg(text, address);
}

QString LinkPreviewPane::cleanAddress() const {
    return UrlTidy::withoutTracking(url_).toString(QUrl::FullyEncoded);
}

void LinkPreviewPane::rebuildAddress() {
    const QString ink = dark_ ? QStringLiteral("#ECEDF2") : QStringLiteral("#1C1E25");
    const QString soft = dark_ ? QStringLiteral("#B9BDC9") : QStringLiteral("#4A4F5C");
    const QString faint = dark_ ? QStringLiteral("#757A87") : QStringLiteral("#9096A3");
    auto span = [](const QString &color, const QString &text, const QString &extra = QString()) {
        return QStringLiteral("<span style=\"color:%1;%3\">%2</span>").arg(color, text.toHtmlEscaped(), extra);
    };

    QString html;
    if (!url_.scheme().isEmpty()) {
        html += span(faint, url_.scheme() + QStringLiteral("://"));
    }
    html += span(ink, url_.host(QUrl::FullyDecoded), QStringLiteral("font-weight:600;"));
    if (url_.port() != -1) {
        html += span(faint, QStringLiteral(":%1").arg(url_.port()));
    }
    html += span(soft, url_.path(QUrl::PrettyDecoded));
    const QList<UrlTidy::Parameter> parameters = UrlTidy::parameters(url_);
    for (int i = 0; i < parameters.size(); ++i) {
        const UrlTidy::Parameter &parameter = parameters.at(i);
        html += span(faint, i == 0 ? QStringLiteral("?") : QStringLiteral("&"));
        const QString pair = parameter.value.isNull() ? parameter.key : parameter.key + QLatin1Char('=') + parameter.value;
        html += parameter.tracking ? span(faint, pair, QStringLiteral("text-decoration:line-through;"))
                                   : span(soft, pair);
    }
    if (url_.hasFragment()) {
        html += span(faint, QLatin1Char('#') + url_.fragment(QUrl::PrettyDecoded));
    }
    address_->setHtml(QStringLiteral("<div style=\"line-height:135%;\">%1</div>").arg(html));
    fitAddressHeight();
}

void LinkPreviewPane::rebuildParameters() {
    while (QLayoutItem *item = parametersGrid_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<UrlTidy::Parameter> parameters = UrlTidy::parameters(url_);
    parametersHeader_->setVisible(!parameters.isEmpty());
    parametersPanel_->setVisible(!parameters.isEmpty());
    if (parameters.isEmpty()) {
        return;
    }

    int tracking = 0;
    for (int row = 0; row < parameters.size(); ++row) {
        const UrlTidy::Parameter &parameter = parameters.at(row);
        tracking += parameter.tracking ? 1 : 0;
        auto *key = new QLabel(parameter.key, parametersPanel_);
        key->setObjectName(parameter.tracking ? QStringLiteral("linkKeyTracking") : QStringLiteral("linkKey"));
        key->setTextFormat(Qt::PlainText);
        key->setFont(monospaceFont(13));
        key->setTextInteractionFlags(Qt::TextSelectableByMouse);
        parametersGrid_->addWidget(key, row, 0, Qt::AlignTop);

        auto *valueRow = new QWidget(parametersPanel_);
        auto *valueLayout = new QHBoxLayout(valueRow);
        valueLayout->setContentsMargins(0, 0, 0, 0);
        valueLayout->setSpacing(8);
        auto *value = new QLabel(parameter.value, valueRow);
        value->setObjectName(parameter.tracking ? QStringLiteral("linkKeyTracking") : QStringLiteral("linkKey"));
        value->setTextFormat(Qt::PlainText);
        value->setFont(monospaceFont(13));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        valueLayout->addWidget(value, 1);
        if (parameter.tracking) {
            auto *chip = new QLabel(tr2(chinese_, "tracking", QStringLiteral("跟踪")), valueRow);
            chip->setObjectName(QStringLiteral("linkChip"));
            valueLayout->addWidget(chip, 0, Qt::AlignTop);
        }
        parametersGrid_->addWidget(valueRow, row, 1);
    }

    parametersHeading_->setText(tr2(chinese_, "Parameters %1", QStringLiteral("参数 %1")).arg(parameters.size()));
    if (tracking > 0) {
        parametersNote_->setText(chinese_
            ? QStringLiteral("其中 %1 个是分享跟踪参数，去掉后打开的还是同一个网页").arg(tracking)
            : QStringLiteral("%1 only track the share; the page opens the same without them").arg(tracking));
    } else {
        parametersNote_->clear();
    }
    parametersNote_->setVisible(tracking > 0);
    cleanButton_->setVisible(tracking > 0);
}

void LinkPreviewPane::fitAddressHeight() {
    fitAddressHeight(address_->width() > 40 ? address_->width() : 400);
}

// The view's frame and stylesheet padding: what it has around its viewport
// once laid out, else the auroraWell padding (8 px 10 px) and a 1 px edge.
void LinkPreviewPane::fitAddressHeight(int width) {
    const bool laidOut = address_->viewport()->width() > 0 && address_->isVisible();
    const int chromeWidth = laidOut ? address_->width() - address_->viewport()->width() : 22;
    const int chromeHeight = laidOut ? address_->height() - address_->viewport()->height() : 18;
    QTextDocument *document = address_->document();
    document->setTextWidth(qMax(40, width - chromeWidth));
    address_->setFixedHeight(qCeil(document->size().height()) + chromeHeight + 2);
}

int LinkPreviewPane::pictureWidthFor(int width) {
    return qBound(200, qRound(width * 0.42), kPictureWidth);
}

int LinkPreviewPane::preferredHeight(int width) const {
    const int pictureWidth = pictureWidthFor(width);
    const int columnWidth = qMax(200, width - pictureWidth - kColumnSpacing);
    // The address at this width, then the column's layout around it.
    const_cast<LinkPreviewPane *>(this)->fitAddressHeight(columnWidth - 4);
    const int columnHeight = column_->layout()->totalHeightForWidth(columnWidth);
    return qMax(kMinimumHeight, qMax(columnHeight, picture_->heightForPictureWidth(pictureWidth)));
}

void LinkPreviewPane::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    // The picture whole at its width, from the top; the site's band the full height.
    const int pictureWidth = pictureWidthFor(width());
    if (pictureSpace_->sizeHint().width() != pictureWidth + kColumnSpacing) {
        pictureSpace_->changeSize(pictureWidth + kColumnSpacing, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        layout()->invalidate();
    }
    picture_->setGeometry(0, 0, pictureWidth, picture_->hasPicture()
        ? qMin(height(), picture_->heightForPictureWidth(pictureWidth)) : height());
    fitAddressHeight();
}

void LinkPreviewPane::applyTheme(bool dark) {
    dark_ = dark;
    picture_->setDark(dark);
    const QString ink = dark ? QStringLiteral("#ECEDF2") : QStringLiteral("#1C1E25");
    const QString faint = dark ? QStringLiteral("rgba(255,255,255,110)") : QStringLiteral("rgba(28,30,37,120)");
    const QString soft = dark ? QStringLiteral("rgba(255,255,255,150)") : QStringLiteral("rgba(28,30,37,160)");
    const QString fill = dark ? QStringLiteral("rgba(255,255,255,18)") : QStringLiteral("rgba(0,0,0,13)");
    setStyleSheet(QStringLiteral(
        "QScrollArea#linkScroll, QWidget#linkColumn { background: transparent; border: none; }"
        "QLabel#linkHost { color: %2; font-size: 13px; font-weight: 500; background: transparent; }"
        "QLabel#linkTitle { color: %1; font-size: 21px; font-weight: 700; background: transparent; }"
        "QLabel#linkKey { color: %1; background: transparent; }"
        "QLabel#linkKeyTracking { color: %3; background: transparent; }"
        "QLabel#linkChip { color: %2; background: %4; border-radius: 6px; padding: 1px 7px; font-size: 11px; }"
        "QScrollArea#linkScroll QScrollBar:vertical { width: 6px; background: transparent; margin: 0; }"
        "QScrollArea#linkScroll QScrollBar::handle:vertical { background: %3; border-radius: 3px; min-height: 32px; }"
        "QScrollArea#linkScroll QScrollBar::add-line:vertical, QScrollArea#linkScroll QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollArea#linkScroll QScrollBar::add-page:vertical, QScrollArea#linkScroll QScrollBar::sub-page:vertical { background: transparent; }")
        .arg(ink, soft, faint, fill));
    if (!url_.isEmpty()) {
        rebuildAddress();
    }
}
