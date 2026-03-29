#include "FileCardBody.h"

#include <QCache>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontMetrics>
#include <QIcon>
#include <QUrl>

#include "widget/CardTheme.h"

namespace {

QPixmap filePixmap(const QSize &size) {
    static QCache<QString, QPixmap> cache(1024);
    const QString cacheKey = QStringLiteral("file:%1x%2").arg(size.width()).arg(size.height());
    if (QPixmap *cached = cache.object(cacheKey)) {
        return *cached;
    }

    const QPixmap pixmap = QIcon(QStringLiteral(":/resources/resources/files.svg")).pixmap(size);
    if (!pixmap.isNull()) {
        const int cacheCost = qMax(1, (pixmap.width() * pixmap.height() * 4) / 1024);
        cache.insert(cacheKey, new QPixmap(pixmap), cacheCost);
    }
    return pixmap;
}

QPixmap loadLocalFileIconSync(const QString &filePath, const QSize &targetLogicalSize, qreal targetDpr) {
    if (filePath.isEmpty() || !targetLogicalSize.isValid()) {
        return {};
    }

    const QFileInfo info(filePath);
    if (!info.exists()) {
        return {};
    }

    const QSize pixelTargetSize = targetLogicalSize * qMax<qreal>(1.0, targetDpr);
    if (!pixelTargetSize.isValid()) {
        return {};
    }

    QFileIconProvider provider;
    QIcon icon = provider.icon(info);
    QPixmap pixmap = icon.pixmap(pixelTargetSize);
    if (pixmap.isNull()) {
        return {};
    }
    pixmap.setDevicePixelRatio(qMax<qreal>(1.0, targetDpr));
    return pixmap;
}

} // anonymous namespace

void FileCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    const QRect imagePreviewRect = ctx.bodyRect;

    if (ctx.card.normalizedUrls.size() == 1 && ctx.card.normalizedUrls.first().isLocalFile()) {
        const QString filePath = ctx.card.normalizedUrls.first().toLocalFile();
        const QPixmap thumb = ctx.delegate->localImageThumbnail(filePath, imagePreviewRect.size(), ctx.paintDpr, ctx.index);
        if (!thumb.isNull()) {
            CardRenderUtils::drawCoverPixmap(painter, imagePreviewRect, thumb, ctx.card.name, QSize());
            return;
        }
    }

    if (ctx.card.normalizedUrls.size() == 1) {
        const QString filePath = ctx.card.normalizedUrls.first().isLocalFile()
            ? ctx.card.normalizedUrls.first().toLocalFile()
            : QString();
        const QFileInfo fileInfo(filePath);
        const QString fileName = fileInfo.fileName();

        // Reserve space for filename below icon.
        QFont nameFont = painter->font();
        CardRenderUtils::applyUiFontDefaults(nameFont);
        nameFont.setPointSize(qMax(8, 9 * ctx.scale / 100));
        const QFontMetrics nameMetrics(nameFont);
        const int nameHeight = nameMetrics.height() + qMax(4, 6 * ctx.scale / 100);
        const int nameTopGap = qMax(4, 6 * ctx.scale / 100);

        const int iconPadding = qMax(10, 12 * ctx.scale / 100);
        const int availableHeight = ctx.previewRect.height() - nameHeight - nameTopGap;
        const int iconSize = qMax(24, qMin(ctx.previewRect.width() - iconPadding * 2,
                                           availableHeight - iconPadding * 2));
        const QRect iconArea(ctx.previewRect.left(), ctx.previewRect.top(),
                             ctx.previewRect.width(), availableHeight);
        const QRect iconRect(iconArea.left() + (iconArea.width() - iconSize) / 2,
                             iconArea.top() + (iconArea.height() - iconSize) / 2,
                             iconSize, iconSize);
        QPixmap iconPixmap = filePath.isEmpty()
            ? QPixmap()
            : ctx.delegate->localFileIcon(filePath, iconRect.size(), ctx.paintDpr, ctx.index);
        if (iconPixmap.isNull()) {
            iconPixmap = filePixmap(iconRect.size());
        }
        QPoint iconPos = iconRect.topLeft();
        const qreal iconDpr = qMax<qreal>(1.0, iconPixmap.devicePixelRatio());
        const QSize iconLogicalSize(qRound(iconPixmap.width() / iconDpr),
                                    qRound(iconPixmap.height() / iconDpr));
        if (iconLogicalSize.isValid() && iconLogicalSize != iconRect.size()) {
            iconPos.setX(iconRect.left() + (iconRect.width() - iconLogicalSize.width()) / 2);
            iconPos.setY(iconRect.top() + (iconRect.height() - iconLogicalSize.height()) / 2);
        }
        painter->drawPixmap(iconPos, iconPixmap);

        // Draw filename below icon.
        if (!fileName.isEmpty()) {
            const int namePadding = qMax(6, 8 * ctx.scale / 100);
            const QRect nameRect(ctx.previewRect.left() + namePadding,
                                 iconArea.bottom() + nameTopGap,
                                 ctx.previewRect.width() - namePadding * 2,
                                 nameHeight);
            CardRenderUtils::drawElidedText(painter, nameRect, fileName, nameFont, ctx.bodyTextColor,
                                            Qt::AlignHCenter | Qt::AlignTop, Qt::ElideMiddle);
        }
        return;
    }

    if (ctx.card.normalizedUrls.size() > 1) {
        // File chips: rounded-rect rows with icon + filename.
        const int padding = qMax(4, 5 * ctx.scale / 100);
        const int iconGap = qMax(4, 5 * ctx.scale / 100);
        const int chipPadX = qMax(2, 3 * ctx.scale / 100);
        const int chipPadY = qMax(5, 6 * ctx.scale / 100);
        const int chipRadius = qMax(4, 6 * ctx.scale / 100);
        const int chipGap = qMax(2, 3 * ctx.scale / 100);

        QFont listFont = painter->font();
        CardRenderUtils::applyUiFontDefaults(listFont);
        listFont.setPointSize(qMax(8, 9 * ctx.scale / 100));
        const QFontMetrics listMetrics(listFont);
        const int chipH = listMetrics.height() * 4 / 3 + chipPadY * 2;
        const int iconSize = qMax(8, chipH - chipPadY * 2);
        const int availH = ctx.previewRect.height() - padding * 2;
        const int maxChips = qMax(1, (availH + chipGap) / (chipH + chipGap));
        const int actualChips = qMin(ctx.card.normalizedUrls.size(), maxChips);
        const int totalH = actualChips * chipH + (actualChips - 1) * chipGap;
        const int startY = ctx.previewRect.top() + padding + qMax(0, (availH - totalH) / 2);
        const int chipLeft = ctx.previewRect.left() + padding;
        const int chipW = ctx.previewRect.width() - padding * 2;
        const QColor chipBg = ctx.theme.fileChipBgColor;
        const int textLeft = chipLeft + chipPadX + iconSize + iconGap;
        const int textW = chipW - chipPadX * 2 - iconSize - iconGap;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setFont(listFont);

        for (int i = 0; i < actualChips; ++i) {
            const int y = startY + i * (chipH + chipGap);
            const QRect chipRect(chipLeft, y, chipW, chipH);

            // Chip background.
            painter->setPen(Qt::NoPen);
            painter->setBrush(chipBg);
            painter->drawRoundedRect(chipRect, chipRadius, chipRadius);

            if (i == maxChips - 1 && ctx.card.normalizedUrls.size() > maxChips) {
                painter->setPen(ctx.bodyTextColor);
                painter->drawText(chipRect, Qt::AlignCenter,
                                  QStringLiteral("... +%1").arg(ctx.card.normalizedUrls.size() - i));
                break;
            }

            const QUrl &url = ctx.card.normalizedUrls.at(i);
            const QString path = url.isLocalFile() ? url.toLocalFile() : QString();
            const QString name = url.isLocalFile()
                ? QFileInfo(path).fileName()
                : url.toDisplayString(QUrl::PreferLocalFile);

            // File type icon (synchronous), forced to exact size.
            QPixmap ico;
            if (!path.isEmpty()) {
                ico = loadLocalFileIconSync(path, QSize(iconSize, iconSize), ctx.paintDpr);
            }
            if (ico.isNull()) {
                ico = filePixmap(QSize(iconSize, iconSize));
            }
            if (!ico.isNull()) {
                const QSize targetPx = QSize(iconSize, iconSize) * ctx.paintDpr;
                if (ico.size() != targetPx) {
                    ico = ico.scaled(targetPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    ico.setDevicePixelRatio(ctx.paintDpr);
                }
            }
            const int iconY = y + (chipH - iconSize) / 2;
            painter->drawPixmap(QPoint(chipLeft + chipPadX, iconY), ico);

            // File name.
            painter->setPen(ctx.bodyTextColor);
            const QRect textRect(textLeft, y, textW, chipH);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                              listMetrics.elidedText(name, Qt::ElideMiddle, textW));
        }
        painter->restore();
    }
}
