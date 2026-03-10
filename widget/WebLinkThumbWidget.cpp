// input: Depends on WebLinkThumbWidget.h, OpenGraph metadata, and image scaling rules for link previews.
// output: Implements a link preview widget whose main image fills the preview area while staying centered, with lighter text insets.
// pos: Widget-layer link preview implementation embedded inside clipboard item cards.
// update: If I change, update this header block and my folder README.md.
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include "WebLinkThumbWidget.h"
#include "ui_WebLinkThumbWidget.h"
#include "utils/MPasteSettings.h"

namespace {
    constexpr int DEFAULT_IMAGE_HEIGHT = 160;
    const QString DEFAULT_STYLE = "QWidget { color: #555555; }";

    QString compactHostLabel(const QUrl &url) {
        QString host = url.host().trimmed();
        if (host.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) {
            host.remove(0, 4);
        }
        if (!host.isEmpty()) {
            return host;
        }

        const QString text = url.toString(QUrl::RemoveScheme | QUrl::RemoveUserInfo | QUrl::RemoveFragment).trimmed();
        return text.isEmpty() ? QStringLiteral("link") : text;
    }

    QString placeholderMonogram(const QUrl &url, const QString &title) {
        auto extractLetters = [](const QString &text) {
            QString token;
            const auto parts = text.split(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")),
                                          Qt::SkipEmptyParts);
            for (const QString &part : parts) {
                if (!part.isEmpty()) {
                    token.append(part.front().toUpper());
                }
                if (token.size() >= 2) {
                    break;
                }
            }
            return token;
        };

        QString token = extractLetters(compactHostLabel(url));
        if (token.isEmpty()) {
            token = extractLetters(title);
        }
        if (token.isEmpty()) {
            token = QStringLiteral("L");
        }
        return token.left(2);
    }

    QPair<QColor, QColor> placeholderPalette(const QString &seed) {
        const uint hash = qHash(seed);
        const int hueA = static_cast<int>(hash % 360);
        const int hueB = static_cast<int>((hueA + 28 + (hash % 71)) % 360);
        return qMakePair(QColor::fromHsl(hueA, 130, 152),
                         QColor::fromHsl(hueB, 122, 170));
    }

    QString fallbackHeadline(const QString &title, const QUrl &url) {
        const QString trimmedTitle = title.trimmed();
        if (!trimmedTitle.isEmpty()) {
            return trimmedTitle;
        }
        return compactHostLabel(url);
    }

    QString fallbackCaption(const QUrl &url) {
        QString host = compactHostLabel(url);
        const QString path = url.path().trimmed();
        if (!path.isEmpty() && path != QStringLiteral("/")) {
            host += QStringLiteral("  ") + path;
        }
        return host;
    }

    QPixmap buildFallbackPreview(const QUrl &url, const QString &title, const QSize &targetSize, qreal devicePixelRatio) {
        if (!targetSize.isValid()) {
            return {};
        }

        const QSize pixelTargetSize = targetSize * devicePixelRatio;
        QPixmap canvas(pixelTargetSize);
        canvas.fill(Qt::transparent);
        canvas.setDevicePixelRatio(devicePixelRatio);

        const QString hostLabel = compactHostLabel(url);
        const QString monogram = placeholderMonogram(url, title);
        const QString headline = fallbackHeadline(title, url);
        const QString caption = fallbackCaption(url);
        const auto palette = placeholderPalette(hostLabel + title);

        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bounds(QPointF(0, 0), QSizeF(targetSize));
        QLinearGradient background(bounds.topLeft(), bounds.bottomRight());
        background.setColorAt(0.0, palette.first.lighter(120));
        background.setColorAt(0.48, palette.second);
        background.setColorAt(1.0, palette.first.darker(116));
        painter.fillRect(bounds, background);

        QRadialGradient glow(bounds.topRight() + QPointF(-bounds.width() * 0.18, bounds.height() * 0.12),
                             bounds.width() * 0.88);
        QColor glowColor(255, 255, 255, 138);
        glow.setColorAt(0.0, glowColor);
        glow.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillRect(bounds, glow);

        const QRectF browserRect = bounds.adjusted(14.0, 12.0, -14.0, -12.0);
        QPainterPath browserPath;
        browserPath.addRoundedRect(browserRect, 18.0, 18.0);
        painter.fillPath(browserPath, QColor(255, 255, 255, 54));
        painter.strokePath(browserPath, QPen(QColor(255, 255, 255, 52), 1.2));

        const QRectF toolbarRect(browserRect.left() + 10.0, browserRect.top() + 10.0,
                                 browserRect.width() - 20.0, 26.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 58));
        painter.drawRoundedRect(toolbarRect, 13.0, 13.0);

        const qreal dotY = toolbarRect.center().y();
        const qreal firstDotX = toolbarRect.left() + 12.0;
        const QColor dotColors[3] = {
            QColor(255, 120, 120, 200),
            QColor(255, 214, 102, 190),
            QColor(107, 203, 119, 190)
        };
        for (int i = 0; i < 3; ++i) {
            painter.setBrush(dotColors[i]);
            painter.drawEllipse(QPointF(firstDotX + i * 10.0, dotY), 2.7, 2.7);
        }

        QFont chipFont = painter.font();
        chipFont.setBold(true);
        chipFont.setPointSizeF(qMax(8.0, bounds.height() * 0.048));
        painter.setFont(chipFont);
        const QString chipText = hostLabel.toUpper();
        QFontMetricsF chipMetrics(chipFont);
        const qreal chipPaddingX = 10.0;
        const qreal chipHeight = chipMetrics.height() + 8.0;
        const qreal chipWidth = qMin(toolbarRect.width() - 54.0, chipMetrics.horizontalAdvance(chipText) + chipPaddingX * 2.0);
        const QRectF chipRect(toolbarRect.right() - chipWidth - 10.0,
                              toolbarRect.center().y() - chipHeight / 2.0,
                              chipWidth, chipHeight);
        painter.setBrush(QColor(255, 255, 255, 64));
        painter.drawRoundedRect(chipRect, chipHeight / 2.0, chipHeight / 2.0);
        painter.setPen(QColor(255, 255, 255, 220));
        painter.drawText(chipRect, Qt::AlignCenter, chipMetrics.elidedText(chipText, Qt::ElideRight, chipRect.width() - 10.0));

        const QRectF heroRect(browserRect.left() + 18.0, toolbarRect.bottom() + 16.0,
                              browserRect.width() - 36.0, browserRect.height() - toolbarRect.height() - 34.0);

        QRadialGradient heroGlow(heroRect.center() + QPointF(0.0, -heroRect.height() * 0.12),
                                 qMin(heroRect.width(), heroRect.height()) * 0.52);
        heroGlow.setColorAt(0.0, QColor(255, 255, 255, 116));
        heroGlow.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillRect(heroRect, heroGlow);

        const qreal badgeSize = qMin(heroRect.width(), heroRect.height()) * 0.38;
        const QRectF badgeRect(heroRect.center().x() - badgeSize / 2.0,
                               heroRect.top() + heroRect.height() * 0.10,
                               badgeSize, badgeSize);
        painter.setBrush(QColor(255, 255, 255, 214));
        painter.drawEllipse(badgeRect);

        QPen ringPen(QColor(72, 92, 110, 44));
        ringPen.setWidthF(1.4);
        painter.setPen(ringPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(badgeRect.adjusted(-4.0, -4.0, 4.0, 4.0));

        QFont monoFont = painter.font();
        monoFont.setBold(true);
        monoFont.setPointSizeF(qMax(18.0, badgeSize * 0.26));
        monoFont.setLetterSpacing(QFont::PercentageSpacing, 105);
        painter.setFont(monoFont);
        painter.setPen(QColor(58, 72, 86));
        painter.drawText(badgeRect, Qt::AlignCenter, monogram);

        const QRectF headlineRect(heroRect.left(), badgeRect.bottom() + 16.0, heroRect.width(), 34.0);
        QFont headlineFont = painter.font();
        headlineFont.setBold(true);
        headlineFont.setPointSizeF(qMax(11.0, bounds.height() * 0.074));
        painter.setFont(headlineFont);
        painter.setPen(QColor(255, 255, 255, 235));
        const QFontMetricsF headlineMetrics(headlineFont);
        painter.drawText(headlineRect, Qt::AlignCenter,
                         headlineMetrics.elidedText(headline, Qt::ElideRight, headlineRect.width()));

        const QRectF captionRect(heroRect.left() + 16.0, headlineRect.bottom() + 6.0,
                                 heroRect.width() - 32.0, 22.0);
        QFont captionFont = painter.font();
        captionFont.setBold(false);
        captionFont.setPointSizeF(qMax(8.5, bounds.height() * 0.05));
        painter.setFont(captionFont);
        painter.setPen(QColor(255, 255, 255, 190));
        const QFontMetricsF captionMetrics(captionFont);
        painter.drawText(captionRect, Qt::AlignCenter,
                         captionMetrics.elidedText(caption, Qt::ElideMiddle, captionRect.width()));

        painter.setBrush(QColor(255, 255, 255, 48));
        painter.setPen(Qt::NoPen);
        const qreal lineWidth = heroRect.width() * 0.42;
        painter.drawRoundedRect(QRectF(heroRect.center().x() - lineWidth / 2.0,
                                       captionRect.bottom() + 14.0,
                                       lineWidth, 6.0), 3.0, 3.0);
        painter.drawRoundedRect(QRectF(heroRect.center().x() - lineWidth * 0.34,
                                       captionRect.bottom() + 26.0,
                                       lineWidth * 0.68, 6.0), 3.0, 3.0);

        return canvas;
    }

    QPixmap scaleToFillHeightCropWidth(const QPixmap &pixmap, const QSize &targetSize, qreal devicePixelRatio) {
        if (pixmap.isNull() || !targetSize.isValid()) {
            return pixmap;
        }

        const QSize pixelTargetSize = targetSize * devicePixelRatio;
        if (!pixelTargetSize.isValid()) {
            return pixmap;
        }

        QPixmap scaled = pixmap.scaledToHeight(pixelTargetSize.height(), Qt::SmoothTransformation);
        if (scaled.width() > pixelTargetSize.width()) {
            const int x = (scaled.width() - pixelTargetSize.width()) / 2;
            scaled = scaled.copy(x, 0, pixelTargetSize.width(), pixelTargetSize.height());
        }

        scaled.setDevicePixelRatio(devicePixelRatio);
        return scaled;
    }
}

WebLinkThumbWidget::WebLinkThumbWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WebLinkThumbWidget)
{
    ui->setupUi(this);

    int scale = MPasteSettings::getInst()->getItemScale();

    // Scale titleLabel height
    int titleH = 20 * scale / 100;
    ui->titleLabel->setMinimumHeight(titleH);
    ui->titleLabel->setMaximumHeight(titleH);

    // Scale fonts
    auto scaleFont = [scale](QWidget *w, int basePt) {
        QFont f = w->font();
        f.setPointSize(basePt * scale / 100);
        w->setFont(f);
    };
    scaleFont(ui->titleLabel, 10);
    scaleFont(ui->urlLabel, 10);

    ui->titleLabel->setIndent(8);
    ui->titleLabel->setContentsMargins(8, 0, 8, 0);
    ui->urlLabel->setContentsMargins(8, 0, 8, 8);
    ui->urlLabel->setMargin(0);

    setDefaultStyle();
}

WebLinkThumbWidget::~WebLinkThumbWidget() = default;

void WebLinkThumbWidget::showWebLink(const QUrl &url, const ClipboardItem &item) {
    currentItem = item;
    currentUrl = url;

    setupInitialPreview(url);

    if (item.getUrl().isEmpty()) {
        ogFetcher.reset(new OpenGraphFetcher(url));
        connect(ogFetcher.data(), &OpenGraphFetcher::finished,
                this, &WebLinkThumbWidget::handleOpenGraphData);
        ogFetcher->handle();
    } else {
        updatePreviewFromItem(item);
    }
}

void WebLinkThumbWidget::setupInitialPreview(const QUrl &url) {
    setElidedText(ui->titleLabel, fallbackHeadline(QString(), url));
    setElidedText(ui->urlLabel, url.toString());

    ui->imageLabel->setPixmap(buildFallbackPreview(url, QString(), ui->imageLabel->size().isValid()
        ? ui->imageLabel->size()
        : QSize(width(), DEFAULT_IMAGE_HEIGHT * MPasteSettings::getInst()->getItemScale() / 100),
        devicePixelRatioF()));
}

void WebLinkThumbWidget::handleOpenGraphData(const OpenGraphItem &ogItem) {
    if (!ogItem.getImage().isNull()) {
        currentItem.setFavicon(ogItem.getImage());
    }

    currentItem.setTitle(ogItem.getTitle());
    currentItem.setUrl(currentUrl.toString());

    updatePreviewFromItem(currentItem);
    Q_EMIT itemNeedToSave(currentItem);
}

void WebLinkThumbWidget::updatePreviewFromItem(const ClipboardItem &item) {
    if (!item.getFavicon().isNull()) {
        ui->imageLabel->setPixmap(processImage(item.getFavicon()));
    } else {
        QSize targetSize = ui->imageLabel->size();
        if (!targetSize.isValid()) {
            const int scale = MPasteSettings::getInst()->getItemScale();
            targetSize = QSize(width(), DEFAULT_IMAGE_HEIGHT * scale / 100);
        }
        ui->imageLabel->setPixmap(buildFallbackPreview(currentUrl, item.getTitle(), targetSize, devicePixelRatioF()));
    }

    setElidedText(ui->titleLabel, fallbackHeadline(item.getTitle(), currentUrl));
    setElidedText(ui->urlLabel, item.getUrl().isEmpty() ? currentUrl.toString() : item.getUrl());
}

QPixmap WebLinkThumbWidget::processImage(const QPixmap &originalPixmap) const {
    QPixmap processedPixmap = originalPixmap;
    processedPixmap.setDevicePixelRatio(devicePixelRatioF());

    QSize targetSize = ui->imageLabel->size();
    if (!targetSize.isValid() || targetSize.height() < DEFAULT_IMAGE_HEIGHT / 2) {
        const int scale = MPasteSettings::getInst()->getItemScale();
        targetSize = QSize(width(), DEFAULT_IMAGE_HEIGHT * scale / 100);
    }

    return scaleToFillHeightCropWidth(processedPixmap, targetSize, devicePixelRatioF());
}

void WebLinkThumbWidget::setElidedText(QLabel *label, const QString &text) {
    if (!label || text.isEmpty()) return;

    QFontMetrics fm(label->font());
    QString elidedText = text;

    if (fm.boundingRect(text).width() > label->width()) {
        label->setAlignment(Qt::AlignLeft);
        elidedText = fm.elidedText(text, Qt::ElideRight, label->width());
    } else {
        label->setAlignment(Qt::AlignCenter);
    }

    label->setText(elidedText);
}

void WebLinkThumbWidget::setDefaultStyle() {
    ui->titleLabel->setStyleSheet(DEFAULT_STYLE);
    ui->urlLabel->setStyleSheet(DEFAULT_STYLE);
    ui->imageLabel->setMargin(0);
}
