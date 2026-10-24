// input: Depends on QuickPasteStack.h, ClipboardBoardService, PlatformRelated, theme and blur helpers.
// output: Paints and drives the quick paste stack (number keys paste, Tab opens the panel).
// pos: Widget-layer second activation layout implementation.
// update: If I change, update this header block and widget/README.md.
#include "QuickPasteStack.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include "BoardInternalHelpers.h"
#include "SurfacePainter.h"
#include "WindowBlurHelper.h"
#include "cardrenderer/CardBodyRenderer.h"
#include "utils/ClipboardBoardService.h"
#include "utils/PlatformRelated.h"
#include "utils/ThemeManager.h"
#ifdef Q_OS_LINUX
#include "utils/GnomeBridge.h"
#endif

namespace {

constexpr int kWidth = 400;
constexpr int kMargin = 8;
constexpr int kHeader = 44;
constexpr int kRow = 46;
constexpr int kExpanded = 150;
constexpr int kGap = 2;
constexpr int kFooter = 40;
constexpr int kMaxVisible = 9;
constexpr int kLoadCount = 30;
constexpr qreal kRadius = 16.0;

QString zhOr(const char *english, const QString &chinese) {
    return BoardHelpers::prefersChineseUi() ? chinese : QString::fromUtf8(english);
}

QString typeLabel(const ClipboardItem &item) {
    switch (item.getContentType()) {
        case Link: return zhOr("Link", QStringLiteral("链接"));
        case Image: return zhOr("Image", QStringLiteral("图片"));
        case RichText: return zhOr("Rich text", QStringLiteral("富文本"));
        case File: return zhOr("File", QStringLiteral("文件"));
        case Color: return zhOr("Color", QStringLiteral("颜色"));
        case Office: return QStringLiteral("Office");
        default:
            if (CardRenderUtils::singleWebUrl(item.getNormalizedText()).isValid())
                return zhOr("Link", QStringLiteral("链接"));
            return zhOr("Text", QStringLiteral("纯文本"));
    }
}

QString relativeTime(const QDateTime &time) {
    if (!time.isValid()) return QString();
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secs = time.secsTo(now);
    if (secs < 60) return zhOr("just now", QStringLiteral("刚刚"));
    if (secs < 3600) {
        const qint64 minutes = secs / 60;
        return BoardHelpers::prefersChineseUi() ? QStringLiteral("%1 分钟前").arg(minutes)
                                                : QStringLiteral("%1 min ago").arg(minutes);
    }
    const QString clock = time.toString(QStringLiteral("HH:mm"));
    if (time.date() == now.date()) {
        const qint64 hours = secs / 3600;
        return BoardHelpers::prefersChineseUi() ? QStringLiteral("%1 小时前").arg(hours)
                                                : QStringLiteral("%1 h ago").arg(hours);
    }
    if (time.date() == now.date().addDays(-1))
        return zhOr("Yesterday", QStringLiteral("昨天")) + QLatin1Char(' ') + clock;
    return QLocale().toString(time.date(), QLocale::ShortFormat) + QLatin1Char(' ') + clock;
}

// "#4A90E2" copied as text is still a colour.
QColor colorFromText(const QString &text) {
    static const QRegularExpression hex(QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    const QString trimmed = text.trimmed();
    return hex.match(trimmed).hasMatch() ? QColor(trimmed) : QColor();
}

// Only pictures make a readable 30 px tile; a rendered text preview does not.
bool showsThumbnail(const ClipboardItem &item) {
    return !item.thumbnail().isNull()
        && (item.getContentType() == Image || item.getContentType() == Office);
}

QString firstLine(const QString &text) {
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString simplified = line.simplified();
        if (!simplified.isEmpty()) return simplified;
    }
    return QString();
}

QString fileTitle(const QList<QUrl> &urls) {
    if (urls.isEmpty()) return QString();
    const QString first = QFileInfo(urls.first().toLocalFile()).fileName();
    if (urls.size() == 1) return first;
    return BoardHelpers::prefersChineseUi()
        ? QStringLiteral("%1 等 %2 个文件").arg(first).arg(urls.size())
        : QStringLiteral("%1 and %2 more").arg(first).arg(urls.size() - 1);
}

QFont uiFont(int pixelSize, QFont::Weight weight = QFont::Normal) {
    QFont font = QApplication::font();
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    return font;
}

} // namespace

QuickPasteStack::QuickPasteStack(ClipboardBoardService *service, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                      | Qt::NoDropShadowWindowHint)
    , service_(service) {
#ifdef Q_OS_LINUX
    // Like the panel: X11 must not let the window manager decorate or move it.
    if (!GnomeBridge::isWayland())
        setWindowFlag(Qt::X11BypassWindowManagerHint);
#endif
    // A stable title the GNOME companion places beside the caret.
    setWindowTitle(QStringLiteral("MPaste Quick"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_InputMethodEnabled, false); // digits paste; an IME must not take them
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setProperty("mpasteBlurRadius", kRadius);
    dark_ = ThemeManager::instance()->isDark();

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](bool dark) {
        dark_ = dark;
        if (isVisible()) WindowBlurHelper::enableBlurBehind(this, dark_);
        update();
    });
    if (service_) {
        connect(service_, &ClipboardBoardService::thumbnailReady,
                this, &QuickPasteStack::handleThumbnail);
    }
}

void QuickPasteStack::popup() {
    dark_ = ThemeManager::instance()->isDark();
    reload();
    filter_.clear();
    selected_ = 0;
    hovered_ = -1;
    applyFilter();
#ifdef Q_OS_LINUX
    const bool wayland = GnomeBridge::isWayland();
#else
    const bool wayland = false;
#endif
    // Read the caret before showing: afterwards the stack is the foreground window.
    if (!wayland) placeBesideCaret();
    show();
    raise();
    activateWindow();
    WindowBlurHelper::enableBlurBehind(this, dark_);
    if (!wayland) PlatformRelated::activateWindow(winId());
}

void QuickPasteStack::reload() {
    if (!service_) {
        entries_.clear();
        return;
    }
    // Reuse what is already loaded; only new files are read from disk.
    QHash<QString, Entry> previous;
    for (const Entry &entry : std::as_const(entries_)) previous.insert(entry.filePath, entry);
    entries_.clear();

    const auto &metas = service_->indexedItemsMeta();
    const int count = qMin(kLoadCount, int(metas.size()));
    for (int i = 0; i < count; ++i) {
        const QString &path = metas.at(i).filePath;
        if (previous.contains(path) && previous.value(path).item.getTime() == metas.at(i).time) {
            entries_.append(previous.take(path));
            continue;
        }
        const auto loaded = service_->loadIndexedSlice(i, 1, true);
        if (loaded.isEmpty()) continue;
        Entry entry;
        entry.filePath = loaded.first().first;
        entry.item = loaded.first().second;
        const ClipboardItem &item = entry.item;
        const QString text = item.getNormalizedText();
        const QList<QUrl> urls = item.getNormalizedUrls();
        entry.swatch = colorFromText(text);
        if (!item.getAlias().isEmpty()) entry.title = item.getAlias();
        else if (item.getContentType() == File) entry.title = fileTitle(urls);
        else if (item.getContentType() == Link) entry.title = !item.getTitle().isEmpty() ? item.getTitle()
                                                             : (urls.isEmpty() ? firstLine(text) : urls.first().toString());
        else if (const QUrl web = CardRenderUtils::singleWebUrl(text); web.isValid())
            // "github.com/owner/repo" without the scheme and www.
            entry.title = CardRenderUtils::displayHost(web)
                + web.toDisplayString(QUrl::RemoveScheme | QUrl::RemoveAuthority | QUrl::RemoveFragment | QUrl::StripTrailingSlash);
        else if (item.getContentType() == Image || item.getContentType() == Office) {
            const QSize size = item.getImagePixelSize();
            entry.title = size.isValid() ? QStringLiteral("%1  %2 \u00D7 %3").arg(typeLabel(item)).arg(size.width()).arg(size.height())
                                         : typeLabel(item);
        } else entry.title = firstLine(text);
        if (entry.title.isEmpty()) entry.title = typeLabel(item);
        entry.mono = (item.getContentType() == Text || item.getContentType() == RichText)
            && CardRenderUtils::looksLikeCode(text);
        entry.searchText = (entry.title + QLatin1Char(' ') + text + QLatin1Char(' ') + item.getUrl()
                            + QLatin1Char(' ') + item.getSourceApp()).toLower();
        if (item.hasThumbnailHint() && item.thumbnail().isNull())
            service_->requestThumbnailAsync(item.getName(), entry.filePath);
        entries_.append(entry);
    }
}

void QuickPasteStack::applyFilter() {
    visible_.clear();
    const QString needle = filter_.toLower();
    for (int i = 0; i < entries_.size() && visible_.size() < kMaxVisible; ++i) {
        if (needle.isEmpty() || entries_.at(i).searchText.contains(needle)) visible_.append(i);
    }
    selected_ = visible_.isEmpty() ? 0 : qBound(0, selected_, int(visible_.size()) - 1);
    hovered_ = -1;
    updateSize();
    update();
}

void QuickPasteStack::updateSize() {
    const int rows = visible_.isEmpty() ? kRow : kExpanded + (int(visible_.size()) - 1) * (kRow + kGap);
    setFixedSize(kWidth, kHeader + rows + 6 + kFooter);
}

void QuickPasteStack::placeBesideCaret() {
    QRect anchor = PlatformRelated::caretRect();
    if (anchor.isNull()) anchor = QRect(QCursor::pos(), QSize(1, 1));
    QScreen *screen = QGuiApplication::screenAt(anchor.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect area = screen->availableGeometry();
    // Below the caret, else above it, else beside it: never over it.
    constexpr int gap = 6;
    const int below = anchor.bottom() + 1 + gap;
    const int above = anchor.top() - gap - height();
    int x = qBound(area.left(), anchor.left(), area.right() + 1 - width());
    int y;
    if (below + height() <= area.bottom() + 1) {
        y = below;
    } else if (above >= area.top()) {
        y = above;
    } else {
        y = qBound(area.top(), anchor.center().y() - height() / 3, area.bottom() + 1 - height());
        const int right = anchor.right() + 1 + 2 * gap;
        x = right + width() <= area.right() + 1 ? right
                                                 : qMax(area.left(), anchor.left() - 2 * gap - width());
    }
    move(x, y);
}

void QuickPasteStack::paste(int visibleIndex, bool plainText) {
    if (visibleIndex < 0 || visibleIndex >= visible_.size()) return;
    // The receiver publishes the clipboard and then hides the stack: on Wayland a
    // selection is only accepted while one of our surfaces has keyboard focus.
    const ClipboardItem item = entries_.at(visible_.at(visibleIndex)).item;
    emit pasteRequested(item, plainText);
    hide();
}

void QuickPasteStack::handleThumbnail(const QString &name, const QPixmap &thumbnail) {
    for (Entry &entry : entries_) {
        if (entry.item.getName() == name && !thumbnail.isNull()) {
            entry.item.setThumbnail(thumbnail);
            if (isVisible()) update();
            return;
        }
    }
}

QRect QuickPasteStack::rowRect(int visibleIndex) const {
    int y = kHeader;
    for (int i = 0; i < visibleIndex; ++i) y += (i == selected_ ? kExpanded : kRow) + kGap;
    return QRect(kMargin, y, width() - 2 * kMargin, visibleIndex == selected_ ? kExpanded : kRow);
}

int QuickPasteStack::rowAt(const QPoint &pos) const {
    for (int i = 0; i < visible_.size(); ++i) {
        if (rowRect(i).contains(pos)) return i;
    }
    return -1;
}

QString QuickPasteStack::subtitleFor(const Entry &entry) const {
    QStringList parts;
    if (!entry.item.getSourceApp().isEmpty()) parts << entry.item.getSourceApp();
    const QString when = relativeTime(entry.item.getTime());
    if (!when.isEmpty()) parts << when;
    parts << (entry.swatch.isValid() ? zhOr("Color", QStringLiteral("颜色")) : typeLabel(entry.item));
    return parts.join(QStringLiteral(" · "));
}

void QuickPasteStack::paintTile(QPainter &p, const QRect &rect, const Entry &entry) const {
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect), 8, 8);
    p.save();
    p.setClipPath(clip);
    const ClipboardItem &item = entry.item;
    if (entry.swatch.isValid()) {
        p.fillRect(rect, entry.swatch);
    } else if (showsThumbnail(item)) {
        CardRenderUtils::drawCoverPixmap(&p, rect, item.thumbnail());
    } else {
        p.fillRect(rect, dark_ ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 16));
        const QPixmap &icon = item.getIcon();
        if (!CardRenderUtils::isPlaceholderIcon(icon)) {
            const QRect iconRect(rect.center() - QPoint(10, 10), QSize(21, 21));
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawPixmap(iconRect, icon);
        } else {
            p.setPen(dark_ ? QColor(244, 244, 246, 200) : QColor(30, 41, 54, 200));
            p.setFont(uiFont(11, QFont::DemiBold));
            p.drawText(rect, Qt::AlignCenter, typeLabel(item).left(1));
        }
    }
    p.restore();
}

void QuickPasteStack::paintExpanded(QPainter &p, const QRect &rect, const Entry &entry, int number) const {
    const QColor accent = ThemeManager::instance()->systemPalette().color(QPalette::Highlight);
    const QColor text = dark_ ? QColor(244, 244, 246) : QColor(30, 41, 54);
    const QColor secondary = dark_ ? QColor(244, 244, 246, 165) : QColor(30, 41, 54, 165);
    QColor fill = accent; fill.setAlpha(dark_ ? 52 : 36);
    QColor line = accent; line.setAlpha(140);
    p.setPen(QPen(line, 1));
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);

    const QRect inner = rect.adjusted(14, 12, -14, -12);
    const QRect badge(inner.left(), inner.top(), 20, 20);
    p.setPen(Qt::NoPen);
    p.setBrush(dark_ ? QColor(255, 255, 255, 34) : QColor(0, 0, 0, 22));
    p.drawRoundedRect(badge, 6, 6);
    p.setPen(text);
    p.setFont(uiFont(11, QFont::DemiBold));
    p.drawText(badge, Qt::AlignCenter, QString::number(number));
    CardRenderUtils::drawElidedText(&p, QRect(badge.right() + 10, inner.top(), inner.width() - 30, 20),
                                    subtitleFor(entry), uiFont(12), secondary, Qt::AlignLeft | Qt::AlignVCenter);

    const QRect preview(inner.left(), inner.top() + 30, inner.width(), inner.height() - 30);
    const ClipboardItem &item = entry.item;
    if (entry.swatch.isValid()) {
        const QRect swatch(preview.left(), preview.top(), preview.width(), preview.height() - 26);
        p.setPen(Qt::NoPen);
        p.setBrush(entry.swatch);
        p.drawRoundedRect(swatch, 8, 8);
        CardRenderUtils::drawElidedText(&p, QRect(preview.left(), swatch.bottom() + 6, preview.width(), 20),
                                        item.getNormalizedText().trimmed(), uiFont(14, QFont::Medium), text,
                                        Qt::AlignLeft | Qt::AlignVCenter);
    } else if (showsThumbnail(item)) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(preview), 8, 8);
        p.save();
        p.setClipPath(clip);
        CardRenderUtils::drawCoverPixmap(&p, preview, item.thumbnail());
        p.restore();
    } else if (item.getContentType() == Link || item.getContentType() == File) {
        CardRenderUtils::drawElidedText(&p, QRect(preview.left(), preview.top(), preview.width(), 22),
                                        entry.title, uiFont(14, QFont::DemiBold), text, Qt::AlignLeft | Qt::AlignVCenter);
        QStringList lines;
        for (const QUrl &url : item.getNormalizedUrls())
            lines << (url.isLocalFile() ? url.toLocalFile() : url.toString());
        if (lines.isEmpty()) lines << item.getUrl();
        p.setPen(secondary);
        p.setFont(uiFont(12));
        p.drawText(QRect(preview.left(), preview.top() + 28, preview.width(), preview.height() - 28),
                   Qt::TextWrapAnywhere, lines.join(QLatin1Char('\n')));
    } else {
        p.save();
        p.setClipRect(preview);
        p.setPen(text);
        QFont bodyFont = uiFont(14);
        if (entry.mono) {
            CardRenderUtils::applyMonoFontDefaults(bodyFont);
            bodyFont.setPixelSize(13);
        }
        p.setFont(bodyFont);
        p.drawText(preview, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, item.getNormalizedText().left(600));
        p.restore();
    }
}

void QuickPasteStack::paintRow(QPainter &p, const QRect &rect, const Entry &entry, int number, bool hovered) const {
    const QColor text = dark_ ? QColor(244, 244, 246) : QColor(30, 41, 54);
    const QColor secondary = dark_ ? QColor(244, 244, 246, 150) : QColor(30, 41, 54, 150);
    if (hovered) {
        p.setPen(Qt::NoPen);
        p.setBrush(dark_ ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 12));
        p.drawRoundedRect(rect, 10, 10);
    }
    const QRect badge(rect.left() + 14, rect.center().y() - 9, 20, 20);
    p.setPen(Qt::NoPen);
    p.setBrush(dark_ ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 18));
    p.drawRoundedRect(badge, 6, 6);
    p.setPen(text);
    p.setFont(uiFont(11, QFont::DemiBold));
    p.drawText(badge, Qt::AlignCenter, QString::number(number));

    const QRect tile(badge.right() + 12, rect.center().y() - 14, 30, 30);
    paintTile(p, tile, entry);

    const int textLeft = tile.right() + 12;
    const int textWidth = rect.right() - 14 - textLeft;
    QFont titleFont = uiFont(13);
    if (entry.swatch.isValid() || entry.mono) {
        CardRenderUtils::applyMonoFontDefaults(titleFont);
        titleFont.setPixelSize(12);
    }
    CardRenderUtils::drawElidedText(&p, QRect(textLeft, rect.top() + 7, textWidth, 19), entry.title,
                                    titleFont, text, Qt::AlignLeft | Qt::AlignVCenter);
    CardRenderUtils::drawElidedText(&p, QRect(textLeft, rect.top() + 26, textWidth, 17), subtitleFor(entry),
                                    uiFont(11), secondary, Qt::AlignLeft | Qt::AlignVCenter);
}

void QuickPasteStack::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    if (WindowBlurHelper::useOpaqueFallback() || WindowBlurHelper::hasNativeBlur()) {
        SurfacePainter::paintPanel(p, QRectF(rect()), kRadius, dark_, 1);
    } else {
        // Windows acrylic supplies the body; draw only the edge.
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(rect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QPen(SurfacePainter::edgeColor(dark_, false, 45), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    }

    const QColor text = dark_ ? QColor(244, 244, 246) : QColor(30, 41, 54);
    const QColor secondary = dark_ ? QColor(244, 244, 246, 150) : QColor(30, 41, 54, 150);
    const QColor keyFill = dark_ ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 16);

    auto drawKey = [&](int x, int centerY, const QString &label) {
        const QFont font = uiFont(11);
        const int w = qMax(20, QFontMetrics(font).horizontalAdvance(label) + 10);
        const QRect r(x, centerY - 10, w, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(keyFill);
        p.drawRoundedRect(r, 5, 5);
        p.setPen(text);
        p.setFont(font);
        p.drawText(r, Qt::AlignCenter, label);
        return r.right();
    };

    // Header: title, filter state and the way to the full panel.
    const QFont titleFont = uiFont(13, QFont::DemiBold);
    p.setPen(text);
    p.setFont(titleFont);
    const QString title = zhOr("Recent", QStringLiteral("最近复制"));
    const int titleWidth = QFontMetrics(titleFont).horizontalAdvance(title);
    p.drawText(QRect(16, 0, titleWidth + 2, kHeader), Qt::AlignLeft | Qt::AlignVCenter, title);
    const QString hint = filter_.isEmpty()
        ? zhOr("· type to filter", QStringLiteral("· 输入继续筛选"))
        : zhOr("Filter: ", QStringLiteral("筛选：")) + filter_;
    const QString panelLabel = zhOr("Full panel", QStringLiteral("完整面板"));
    const QFont small = uiFont(12);
    const int panelWidth = QFontMetrics(small).horizontalAdvance(panelLabel);
    const int keyX = width() - 16 - 34;
    CardRenderUtils::drawElidedText(&p, QRect(16 + titleWidth + 8, 0, keyX - panelWidth - 16 - (16 + titleWidth + 8), kHeader),
                                    hint, small, filter_.isEmpty() ? secondary : text, Qt::AlignLeft | Qt::AlignVCenter);
    p.setPen(secondary);
    p.setFont(small);
    p.drawText(QRect(keyX - panelWidth - 6, 0, panelWidth, kHeader), Qt::AlignRight | Qt::AlignVCenter, panelLabel);
    drawKey(keyX, kHeader / 2, QStringLiteral("Tab"));

    if (visible_.isEmpty()) {
        p.setPen(secondary);
        p.setFont(uiFont(13));
        p.drawText(QRect(kMargin, kHeader, width() - 2 * kMargin, kRow), Qt::AlignCenter,
                   entries_.isEmpty() ? zhOr("Nothing copied yet", QStringLiteral("还没有复制过内容"))
                                      : zhOr("No match", QStringLiteral("没有匹配的条目")));
    }
    for (int i = 0; i < visible_.size(); ++i) {
        const Entry &entry = entries_.at(visible_.at(i));
        if (i == selected_) paintExpanded(p, rowRect(i), entry, i + 1);
        else paintRow(p, rowRect(i), entry, i + 1, i == hovered_);
    }

    // Footer: the keys.
    const int footerTop = height() - kFooter;
    p.setPen(QPen(dark_ ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 16), 1));
    p.drawLine(QPointF(0, footerTop + 0.5), QPointF(width(), footerTop + 0.5));
    const int cy = footerTop + kFooter / 2;
    int x = 16;
    auto drawHint = [&](const QString &key, const QString &label) {
        x = drawKey(x, cy, key) + 6;
        p.setPen(secondary);
        p.setFont(small);
        const int w = QFontMetrics(small).horizontalAdvance(label);
        p.drawText(QRect(x, footerTop, w + 2, kFooter), Qt::AlignLeft | Qt::AlignVCenter, label);
        x += w + 16;
    };
    drawHint(QStringLiteral("1–9"), zhOr("paste", QStringLiteral("粘贴")));
    drawHint(QStringLiteral("↑↓"), zhOr("select", QStringLiteral("选择")));
    drawHint(QStringLiteral("⇧↵"), zhOr("plain text", QStringLiteral("纯文本")));
}

void QuickPasteStack::keyPressEvent(QKeyEvent *event) {
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    switch (event->key()) {
        case Qt::Key_Escape:
            hide();
            return;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            // Show the panel first so the application never looks inactive in between.
            emit expandRequested();
            QTimer::singleShot(200, this, &QWidget::hide);
            return;
        case Qt::Key_Up:
            if (selected_ > 0) { --selected_; updateSize(); update(); }
            return;
        case Qt::Key_Down:
            if (selected_ + 1 < visible_.size()) { ++selected_; updateSize(); update(); }
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            paste(selected_, shift);
            return;
        case Qt::Key_Backspace:
            if (!filter_.isEmpty()) { filter_.chop(1); selected_ = 0; applyFilter(); }
            return;
        default:
            break;
    }
    // Digits paste, with or without the activation shortcut's Alt still held.
    const int index = BoardHelpers::shortcutIndexForKey(event->key());
    if (index >= 0 && index < kMaxVisible) {
        if (!event->isAutoRepeat()) paste(index, shift);
        return;
    }
    const QString typed = event->text();
    if (!typed.isEmpty() && typed.at(0).isPrint()
        && !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        filter_ += typed;
        selected_ = 0;
        applyFilter();
        return;
    }
    QWidget::keyPressEvent(event);
}

void QuickPasteStack::mouseMoveEvent(QMouseEvent *event) {
    const int row = rowAt(event->position().toPoint());
    if (row != hovered_) {
        hovered_ = row;
        update();
    }
}

void QuickPasteStack::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        const int row = rowAt(event->position().toPoint());
        if (row >= 0) paste(row, event->modifiers().testFlag(Qt::ShiftModifier));
    }
}

void QuickPasteStack::leaveEvent(QEvent *) {
    if (hovered_ != -1) {
        hovered_ = -1;
        update();
    }
}
