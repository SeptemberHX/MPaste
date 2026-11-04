// input: Depends on ClipboardItemDetailsDialog.h, ZoomableImageView, Aurora chrome and ClipboardItem accessors.
// output: The details dialog: tabs across the top, the content on the left, what it is on the right.
// pos: Widget-layer details dialog implementation for understanding and debugging clipboard items.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardItemDetailsDialog.h"

#include "AuroraChrome.h"
#include "BoardInternalHelpers.h"
#include "ClipboardCardDelegate.h"
#include "ZoomableImageView.h"
#include "cardrenderer/CardBodyRenderer.h"
#include "data/ContentClassifier.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QBuffer>
#include <QCursor>
#include <QIcon>
#include <QImageReader>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace {
// Spacing on a 4px grid: window padding 20, panes 16 apart, 16 inside.
constexpr int kWidth = 900;
constexpr int kHeight = 640;
constexpr int kShadow = 10;      // AuroraChrome paints the window inside this margin
constexpr int kPad = 20;
constexpr int kGap = 16;
constexpr int kInfoWidth = 300;
constexpr int kTabInset = 12;    // a tab's own padding: its text lines up with the panes' edge

QString zh(const char16_t *text) {
    return QString::fromUtf16(text);
}

QString dashText() {
    return QString::fromUtf16(u"—");
}

QString byteLabel(qint64 bytes) {
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    }
    return QStringLiteral("%1 MB").arg(QString::number(bytes / 1024.0 / 1024.0, 'f', 1));
}

QFont monoFont(qreal points) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const QString &family : {QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Mono"),
                                  QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono")}) {
        if (QFontDatabase::hasFamily(family)) {
            font.setFamily(family);
            break;
        }
    }
    font.setPointSizeF(points);
    return font;
}

struct Palette {
    QColor ink, muted, faint, hair, stage, pane, chip, accent, ok, warn;
};

Palette paletteFor(bool dark, const QColor &accent) {
    Palette p;
    p.ink = dark ? QColor(236, 238, 242) : QColor(28, 30, 37);
    p.muted = dark ? QColor(255, 255, 255, 140) : QColor(28, 30, 37, 150);
    p.faint = dark ? QColor(255, 255, 255, 92) : QColor(28, 30, 37, 110);
    p.hair = dark ? QColor(255, 255, 255, 17) : QColor(0, 0, 0, 18);
    p.stage = dark ? QColor(0, 0, 0, 72) : QColor(0, 0, 0, 14);
    p.pane = dark ? QColor(255, 255, 255, 9) : QColor(255, 255, 255, 150);
    p.chip = dark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 12);
    p.accent = accent.isValid() ? accent : QColor(91, 141, 239);
    p.ok = dark ? QColor(115, 192, 143) : QColor(36, 140, 78);
    p.warn = dark ? QColor(240, 150, 140) : QColor(190, 60, 50);
    return p;
}

QString rgba(const QColor &c) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

// The kind of picture its first bytes say it is.
QString pictureKind(const QByteArray &bytes, bool *uncompressed) {
    *uncompressed = false;
    if (bytes.startsWith("\x89PNG")) return QStringLiteral("PNG");
    if (bytes.startsWith("\xFF\xD8")) return QStringLiteral("JPEG");
    if (bytes.startsWith("GIF8")) return QStringLiteral("GIF");
    if (bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP") return QStringLiteral("WebP");
    if (bytes.startsWith("BM")) { *uncompressed = true; return QStringLiteral("BMP"); }
    if (bytes.size() >= 4 && bytes.at(0) == 0x28 && bytes.at(1) == 0 && bytes.at(2) == 0) {
        *uncompressed = true;   // a DIB: BMP without its file header
        return QStringLiteral("DIB");
    }
    return QString();
}

bool isPictureFormat(const QString &format) {
    return format.startsWith(QStringLiteral("image/"))
        || format == QStringLiteral("application/x-qt-image")
        || (format.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))
            && (format.contains(QStringLiteral("PNG")) || format.contains(QStringLiteral("JFIF"))
                || format.contains(QStringLiteral("DIB")) || format.contains(QStringLiteral("Bitmap"))
                || format.contains(QStringLiteral("GIF"))));
}

// Each format with its size, and whether it repeats an earlier one's data.
struct FormatEntry {
    QString name;
    qint64 bytes = 0;
    bool repeat = false;
};

QList<FormatEntry> formatEntries(const QMimeData *mime, qint64 *stored, int *distinct) {
    QList<FormatEntry> entries;
    QList<QByteArray> seen;
    *stored = 0;
    *distinct = 0;
    if (!mime) {
        return entries;
    }
    for (const QString &format : mime->formats()) {
        FormatEntry entry;
        entry.name = format;
        const QByteArray data = mime->data(format);
        entry.bytes = data.size();
        for (const QByteArray &other : seen) {
            if (other.size() == data.size() && (other.constData() == data.constData() || other == data)) {
                entry.repeat = true;
                break;
            }
        }
        if (!entry.repeat) {
            seen << data;
            *stored += data.size();
            ++*distinct;
        }
        entries << entry;
    }
    return entries;
}

QString shortFormat(const QString &format) {
    QString name = format;
    name.remove(QStringLiteral("application/"));
    name.replace(QStringLiteral("x-qt-windows-mime;value=\""), QStringLiteral("windows "));
    name.remove(QLatin1Char('"'));
    return name;
}

// "今天", "昨天", or nothing.
QString dayWord(const QDateTime &time, bool chinese) {
    const qint64 days = time.date().daysTo(QDate::currentDate());
    if (days == 0) return chinese ? zh(u"今天") : QStringLiteral("Today");
    if (days == 1) return chinese ? zh(u"昨天") : QStringLiteral("Yesterday");
    return QString();
}
// The scroll bars sit in containers of their own that fill themselves with
// the window colour; cleared, the pane's own fill shows behind the bar.
void clearScrollChrome(QAbstractScrollArea *area) {
    for (QScrollBar *bar : {area->verticalScrollBar(), area->horizontalScrollBar()}) {
        bar->setAutoFillBackground(false);
        if (QWidget *container = bar->parentWidget(); container && container != area) {
            container->setAutoFillBackground(false);
        }
    }
    area->viewport()->setAutoFillBackground(false);
}
}  // namespace

// Format names as small chips, wrapping to the width they are given.
class DetailsChips : public QWidget {
public:
    using QWidget::QWidget;
    void setChips(const QStringList &chips) { chips_ = chips; updateGeometry(); update(); }
    void setPalette(const Palette &p) { palette_ = p; update(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return layoutChips(width, nullptr); }
    QSize sizeHint() const override { return QSize(kInfoWidth - 32, heightForWidth(kInfoWidth - 32)); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QList<QRect> rects;
        layoutChips(width(), &rects);
        painter.setFont(monoFont(8.5));
        for (int i = 0; i < rects.size(); ++i) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(palette_.chip);
            painter.drawRoundedRect(QRectF(rects[i]), 6, 6);
            painter.setPen(palette_.muted);
            painter.drawText(rects[i].adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             QFontMetrics(painter.font()).elidedText(chips_[i], Qt::ElideMiddle, rects[i].width() - 16));
        }
    }

private:
    int layoutChips(int width, QList<QRect> *out) const {
        const QFontMetrics metrics(monoFont(8.5));
        const int h = metrics.height() + 10;
        int x = 0, y = 0;
        for (const QString &chip : chips_) {
            const int w = qMin(width, metrics.horizontalAdvance(chip) + 17);
            if (x > 0 && x + w > width) {
                x = 0;
                y += h + 6;
            }
            if (out) {
                out->append(QRect(x, y, w, h));
            }
            x += w + 6;
        }
        return chips_.isEmpty() ? 0 : y + h;
    }

    QStringList chips_;
    Palette palette_ = paletteFor(true, QColor());
};

// The formats as a table: each format's size as a bar, a repeat of the data
// above dashed and marked "same", the real storage summed on top.
class DetailsFormatsView : public QWidget {
public:
    using QWidget::QWidget;
    void setEntries(const QList<FormatEntry> &entries, qint64 stored, int distinct, bool chinese) {
        entries_ = entries;
        stored_ = stored;
        distinct_ = distinct;
        chinese_ = chinese;
        setMinimumHeight(contentHeight());
        update();
    }
    void setPalette(const Palette &p) { palette_ = p; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const int left = kGap, right = width() - kGap;
        int y = kGap;

        // Summary: storage, formats, distinct data.
        QFont big = font();
        big.setPointSizeF(13.5);
        big.setWeight(QFont::Bold);
        QFont small = font();
        small.setPointSizeF(9);
        const QStringList values = {byteLabel(stored_),
                                    QString::number(entries_.size()),
                                    QString::number(distinct_)};
        const QStringList labels = chinese_
            ? QStringList{zh(u"实际存储"), zh(u"种格式"), zh(u"份不同数据")}
            : QStringList{QStringLiteral("stored"), QStringLiteral("formats"), QStringLiteral("distinct data")};
        int x = left;
        for (int i = 0; i < values.size(); ++i) {
            painter.setFont(big);
            painter.setPen(palette_.ink);
            const int bigH = QFontMetrics(big).height();
            painter.drawText(QRect(x, y, 200, bigH), Qt::AlignLeft | Qt::AlignVCenter, values[i]);
            painter.setFont(small);
            painter.setPen(palette_.muted);
            painter.drawText(QRect(x, y + bigH, 200, QFontMetrics(small).height()), Qt::AlignLeft, labels[i]);
            x += qMax(QFontMetrics(big).horizontalAdvance(values[i]), QFontMetrics(small).horizontalAdvance(labels[i])) + 32;
        }
        y += summaryHeight() + 8;

        qint64 largest = 1;
        for (const FormatEntry &e : entries_) {
            largest = qMax(largest, e.bytes);
        }
        const QFont mono = monoFont(9);
        const QFontMetrics monoMetrics(mono);
        const int nameWidth = (right - left) / 2;
        const int numWidth = 72;
        const int barLeft = left + nameWidth + 12;
        const int barRight = right - numWidth - 12;
        for (const FormatEntry &e : entries_) {
            painter.setPen(QPen(palette_.hair, 1));
            painter.drawLine(left, y, right, y);
            const QRect row(left, y, right - left, kRow);
            painter.setFont(mono);
            painter.setPen(palette_.ink);
            painter.drawText(QRect(left, y, nameWidth, kRow), Qt::AlignVCenter | Qt::AlignLeft,
                             monoMetrics.elidedText(e.name, Qt::ElideMiddle, nameWidth));
            const int track = qMax(0, barRight - barLeft);
            const QRectF trackRect(barLeft, row.center().y() - 2.5, track, 5);
            painter.setPen(Qt::NoPen);
            painter.setBrush(palette_.chip);
            painter.drawRoundedRect(trackRect, 2.5, 2.5);
            const qreal w = qMax<qreal>(4.0, track * qreal(e.bytes) / largest);
            const QRectF bar(trackRect.left(), trackRect.top(), qMin<qreal>(w, track), trackRect.height());
            if (e.repeat) {
                QColor dash = palette_.accent;
                dash.setAlpha(120);
                painter.setBrush(dash);
                for (qreal dx = bar.left(); dx < bar.right(); dx += 8) {
                    painter.drawRoundedRect(QRectF(dx, bar.top(), qMin<qreal>(4, bar.right() - dx), bar.height()), 2, 2);
                }
            } else {
                painter.setBrush(palette_.accent);
                painter.drawRoundedRect(bar, 2.5, 2.5);
            }
            painter.setFont(font());
            painter.setPen(e.repeat ? palette_.faint : palette_.ink);
            painter.drawText(QRect(right - numWidth, y, numWidth, kRow), Qt::AlignVCenter | Qt::AlignRight,
                             e.repeat ? (chinese_ ? zh(u"同上") : QStringLiteral("same")) : byteLabel(e.bytes));
            y += kRow;
        }
        bool anyRepeat = false;
        for (const FormatEntry &e : entries_) {
            anyRepeat = anyRepeat || e.repeat;
        }
        if (anyRepeat) {
            painter.setPen(QPen(palette_.hair, 1));
            painter.drawLine(left, y, right, y);
            painter.setFont(small);
            painter.setPen(palette_.faint);
            painter.drawText(QRect(left, y + 10, right - left, 20), Qt::AlignLeft | Qt::AlignTop,
                             chinese_ ? zh(u"虚线、“同上”：和上面一行是同一份数据，只存一次。")
                                      : QStringLiteral("Dashed, “same”: the same data as a row above, stored once."));
        }
    }

private:
    static constexpr int kRow = 38;
    int summaryHeight() const {
        QFont big = font();
        big.setPointSizeF(13.5);
        QFont small = font();
        small.setPointSizeF(9);
        return QFontMetrics(big).height() + QFontMetrics(small).height();
    }
    int contentHeight() const { return kGap + summaryHeight() + 8 + int(entries_.size()) * kRow + 40; }

    QList<FormatEntry> entries_;
    qint64 stored_ = 0;
    int distinct_ = 0;
    bool chinese_ = true;
    Palette palette_ = paletteFor(true, QColor());
};

ClipboardItemDetailsDialog::ClipboardItemDetailsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    setModal(false);
    setWindowTitle(QStringLiteral("MPaste Details"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kWidth, kHeight);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(kShadow, kShadow, kShadow, kShadow);
    ui_.card = new QFrame(this);
    ui_.card->setObjectName(QStringLiteral("detailsCard"));
    rootLayout->addWidget(ui_.card);

    auto *layout = new QVBoxLayout(ui_.card);
    layout->setContentsMargins(kPad, kPad, kPad, kPad);
    layout->setSpacing(0);

    // Header: the source app, what and when, the actions, close.
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(12);
    ui_.iconLabel = new QLabel(ui_.card);
    ui_.iconLabel->setFixedSize(34, 34);
    header->addWidget(ui_.iconLabel, 0, Qt::AlignVCenter);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    ui_.titleLabel = new QLabel(ui_.card);
    ui_.titleLabel->setObjectName(QStringLiteral("auroraTitle"));
    ui_.subtitleLabel = new QLabel(ui_.card);
    ui_.subtitleLabel->setObjectName(QStringLiteral("auroraSubtitle"));
    titles->addWidget(ui_.titleLabel);
    titles->addWidget(ui_.subtitleLabel);
    header->addLayout(titles, 1);

    ui_.actions = new QWidget(ui_.card);
    ui_.actions->setObjectName(QStringLiteral("auroraSegment"));
    ui_.actions->setAttribute(Qt::WA_StyledBackground, true);
    auto *actionsLayout = new QHBoxLayout(ui_.actions);
    actionsLayout->setContentsMargins(3, 2, 3, 2);
    actionsLayout->setSpacing(2);
    auto addAction = [this, actionsLayout](const QString &text, bool primary, auto handler) {
        auto *button = new QPushButton(text, ui_.actions);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCheckable(true);
        button->setChecked(primary);   // the chosen look marks the main action
        connect(button, &QPushButton::clicked, this, [button, primary, handler]() {
            button->setChecked(primary);
            handler();
        });
        actionsLayout->addWidget(button);
        return button;
    };
    ui_.pasteButton = addAction(uiText(QStringLiteral("Paste"), zh(u"粘贴")), true, [this]() {
        const ClipboardItem item = currentItem_;
        reject();
        emit pasteRequested(item);
    });
    addAction(uiText(QStringLiteral("Copy"), zh(u"复制")), false, [this]() {
        emit copyRequested(currentItem_);
    });
    ui_.ocrButton = addAction(uiText(QStringLiteral("Read text"), zh(u"识别文字")), false, [this]() {
        const ClipboardItem item = currentItem_;
        reject();
        emit ocrRequested(item);
    });
    ui_.saveButton = addAction(uiText(QStringLiteral("Save as"), zh(u"另存为")), false, [this]() {
        emit saveRequested(currentItem_);
    });
    ui_.openButton = addAction(QString(), false, [this]() {
        if (!openTarget_.isEmpty()) {
            QDesktopServices::openUrl(QUrl(openTarget_));
        }
    });
    header->addWidget(ui_.actions, 0, Qt::AlignVCenter);

    ui_.closeButton = new QToolButton(ui_.card);
    ui_.closeButton->setObjectName(QStringLiteral("auroraClose"));
    ui_.closeButton->setText(QStringLiteral("✕"));
    ui_.closeButton->setCursor(Qt::PointingHandCursor);
    ui_.closeButton->setFocusPolicy(Qt::NoFocus);
    connect(ui_.closeButton, &QToolButton::clicked, this, &QDialog::reject);
    header->addWidget(ui_.closeButton, 0, Qt::AlignVCenter);
    layout->addLayout(header);
    layout->addSpacing(kGap);

    // The tabs run above both panes, so the content and the facts start
    // and end on the same lines; a hairline below.
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(2);
    ui_.tabGroup = new QButtonGroup(this);
    ui_.tabGroup->setExclusive(true);
    const QStringList tabNames = {uiText(QStringLiteral("Preview"), zh(u"预览")), uiText(QStringLiteral("Text"), zh(u"文本")),
                                  uiText(QStringLiteral("Raw"), zh(u"原始")), QStringLiteral("HTML"),
                                  uiText(QStringLiteral("Formats"), zh(u"格式"))};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto *tab = new QPushButton(tabNames[i], ui_.card);
        tab->setObjectName(QStringLiteral("detailsTab"));
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFocusPolicy(Qt::NoFocus);
        ui_.tabGroup->addButton(tab, i);
        ui_.tabs[i] = tab;
        toolbar->addWidget(tab, 0, Qt::AlignVCenter);
    }
    connect(ui_.tabGroup, &QButtonGroup::idClicked, this, &ClipboardItemDetailsDialog::showPage);
    toolbar->addStretch(1);
    ui_.hintLabel = new QLabel(ui_.card);
    ui_.hintLabel->setObjectName(QStringLiteral("auroraHint"));
    toolbar->addWidget(ui_.hintLabel, 0, Qt::AlignVCenter);
    layout->addLayout(toolbar);
    layout->addSpacing(12);
    auto *hairline = new QFrame(ui_.card);
    hairline->setObjectName(QStringLiteral("detailsHairline"));
    hairline->setFixedHeight(1);
    layout->addWidget(hairline);
    layout->addSpacing(12);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(kGap);

    // Left: the content, one page per tab.
    auto *stage = new QFrame(ui_.card);
    stage->setObjectName(QStringLiteral("detailsStage"));
    auto *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    ui_.pages = new QStackedWidget(stage);
    stageLayout->addWidget(ui_.pages);

    ui_.previewStack = new QStackedWidget(ui_.pages);
    ui_.imageView = new ZoomableImageView(ui_.previewStack);
    ui_.imageView->setInset(12);
    ui_.previewText = createReader(false);
    ui_.previewText->setObjectName(QStringLiteral("detailsProse"));
    ui_.previewCard = new QLabel(ui_.previewStack);
    ui_.previewCard->setObjectName(QStringLiteral("detailsCenter"));
    ui_.previewCard->setAlignment(Qt::AlignCenter);
    ui_.previewCard->setWordWrap(true);
    ui_.previewCard->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui_.previewCard->setContentsMargins(kGap, kGap, kGap, kGap);
    ui_.previewStack->addWidget(ui_.imageView);
    ui_.previewStack->addWidget(ui_.previewText);
    ui_.previewStack->addWidget(ui_.previewCard);
    ui_.pages->addWidget(ui_.previewStack);

    ui_.textEdit = createReader(false);
    ui_.rawEdit = createReader(false);
    ui_.htmlEdit = createReader(true);
    ui_.pages->addWidget(ui_.textEdit);
    ui_.pages->addWidget(ui_.rawEdit);
    ui_.pages->addWidget(ui_.htmlEdit);

    auto *formatsScroll = new QScrollArea(ui_.pages);
    formatsScroll->setObjectName(QStringLiteral("detailsClear"));
    formatsScroll->setFrameShape(QFrame::NoFrame);
    formatsScroll->setWidgetResizable(true);
    formatsScroll->viewport()->setAutoFillBackground(false);
    ui_.formatsView = new DetailsFormatsView(formatsScroll);
    formatsScroll->setWidget(ui_.formatsView);
    ui_.pages->addWidget(formatsScroll);
    body->addWidget(stage, 1);

    // Right: what it is, in sections split by hairlines, in one pane.
    // The pane is a frame with the scroll area inside it, clear, so its
    // rounded fill shows behind the scroll bar too.
    auto *infoPane = new QFrame(ui_.card);
    infoPane->setObjectName(QStringLiteral("detailsInfo"));
    infoPane->setFixedWidth(kInfoWidth);
    auto *infoPaneLayout = new QVBoxLayout(infoPane);
    infoPaneLayout->setContentsMargins(0, 0, 0, 0);
    auto *infoScroll = new QScrollArea(infoPane);
    infoScroll->setObjectName(QStringLiteral("detailsClear"));
    infoScroll->setFrameShape(QFrame::NoFrame);
    infoScroll->setWidgetResizable(true);
    // Values are cut to a few lines so the facts fit; should they not, the
    // wheel still scrolls, without a bar across the pane's edge.
    infoScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    infoScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    infoPaneLayout->addWidget(infoScroll);
    infoScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    infoScroll->viewport()->setAutoFillBackground(false);
    auto *info = new QWidget(infoScroll);
    info->setObjectName(QStringLiteral("detailsInfoBody"));
    ui_.infoLayout = new QVBoxLayout(info);
    ui_.infoLayout->setContentsMargins(0, 0, 0, 0);
    ui_.infoLayout->setSpacing(0);

    // The technical section stays; the type's own sections go above it.
    ui_.techSection = new QWidget(info);
    auto *techLayout = new QVBoxLayout(ui_.techSection);
    techLayout->setContentsMargins(kGap, kGap, kGap, kGap);
    techLayout->setSpacing(12);
    auto *techTitle = new QLabel(uiText(QStringLiteral("TECHNICAL"), zh(u"技术")), ui_.techSection);
    techTitle->setObjectName(QStringLiteral("detailsSectionTitle"));
    techLayout->addWidget(techTitle);
    ui_.chips = new DetailsChips(ui_.techSection);
    techLayout->addWidget(ui_.chips);
    auto *fingerprintRow = new QHBoxLayout;
    fingerprintRow->setSpacing(8);
    ui_.fingerprintValue = new QLabel(ui_.techSection);
    ui_.fingerprintValue->setObjectName(QStringLiteral("detailsMono"));
    ui_.fingerprintValue->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    fingerprintRow->addWidget(ui_.fingerprintValue, 1);
    auto *copyFingerprint = new QPushButton(uiText(QStringLiteral("Copy"), zh(u"复制")), ui_.techSection);
    copyFingerprint->setObjectName(QStringLiteral("detailsLink"));
    copyFingerprint->setCursor(Qt::PointingHandCursor);
    copyFingerprint->setFocusPolicy(Qt::NoFocus);
    connect(copyFingerprint, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(fingerprintHex_);
    });
    fingerprintRow->addWidget(copyFingerprint);
    techLayout->addLayout(fingerprintRow);
    ui_.infoLayout->addWidget(ui_.techSection);
    ui_.infoLayout->addStretch(1);
    infoScroll->setWidget(info);
    body->addWidget(infoPane);
    for (QAbstractScrollArea *area : {static_cast<QAbstractScrollArea *>(infoScroll),
                                      static_cast<QAbstractScrollArea *>(formatsScroll),
                                      static_cast<QAbstractScrollArea *>(ui_.imageView),
                                      static_cast<QAbstractScrollArea *>(ui_.previewText),
                                      static_cast<QAbstractScrollArea *>(ui_.textEdit),
                                      static_cast<QAbstractScrollArea *>(ui_.rawEdit),
                                      static_cast<QAbstractScrollArea *>(ui_.htmlEdit)}) {
        clearScrollChrome(area);
    }
    layout->addLayout(body, 1);

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ClipboardItemDetailsDialog::applyTheme);
    // Closed, it lets go of the item's data and its picture.
    connect(this, &QDialog::finished, this, [this]() {
        currentItem_ = ClipboardItem();
        ui_.imageView->clear();
        ui_.formatsView->setEntries({}, 0, 0, BoardHelpers::prefersChineseUi());
    });
}

QTextEdit *ClipboardItemDetailsDialog::createReader(bool noWrap) const {
    auto *edit = new QTextEdit;
    edit->setObjectName(QStringLiteral("detailsReader"));
    edit->setReadOnly(true);
    edit->setAcceptRichText(false);
    edit->setFrameShape(QFrame::NoFrame);
    edit->setLineWrapMode(noWrap ? QTextEdit::NoWrap : QTextEdit::WidgetWidth);
    edit->setFont(monoFont(10));
    edit->document()->setDocumentMargin(kGap);
    edit->viewport()->setAutoFillBackground(false);
    return edit;
}

void ClipboardItemDetailsDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    AuroraChrome::paintWindow(painter, QRectF(rect()).adjusted(kShadow, kShadow, -kShadow, -kShadow), darkTheme_, accent_);
}

void ClipboardItemDetailsDialog::applyTheme(bool dark) {
    darkTheme_ = dark;
    AuroraChrome::attachBackdrop(this, darkTheme_, kShadow);
    const Palette p = paletteFor(dark, accent_);
    const QString style = AuroraChrome::styleSheet(dark) + QStringLiteral(
        "QFrame#detailsCard { background: transparent; border: none; }"
        "QFrame#detailsHairline { background: %4; border: none; }"
        "QFrame#detailsStage { background: %5; border: none; border-radius: 14px; }"
        "QFrame#detailsInfo { background: %6; border: none; border-radius: 14px; }"
        "QScrollArea#detailsClear, QScrollArea#detailsClear > QWidget > QWidget { background: transparent; border: none; }"
        "QWidget#detailsInfoBody, QScrollArea#detailsClear, QStackedWidget { background: transparent; }"
        "QTextEdit#detailsReader { background: transparent; border: none; color: %1;"
        "  selection-background-color: rgba(91,141,239,150); }"
        "QTextEdit#detailsProse { font-size: 14px; }"
        "QLabel#detailsCenter { background: transparent; color: %1; font-size: 13px; }"
        "QLabel#detailsSectionTitle { color: %3; font-size: 11px; font-weight: 700; letter-spacing: 1px; background: transparent; }"
        "QLabel#detailsKey { color: %2; font-size: 13px; background: transparent; }"
        "QLabel#detailsValue { color: %1; font-size: 13px; background: transparent; }"
        "QLabel#detailsMono { color: %2; font-family: monospace; font-size: 11.5px; background: transparent; }"
        "QFrame#detailsSectionLine { background: %4; border: none; }"
        "QScrollBar:vertical { width: 8px; background: transparent; margin: 4px 2px 4px 0; border: none; }"
        "QScrollBar:horizontal { height: 8px; background: transparent; margin: 0 4px 2px 4px; border: none; }"
        "QScrollBar::handle { background: %3; border-radius: 3px; min-height: 24px; min-width: 24px; }"
        "QScrollBar::handle:hover { background: %2; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: transparent; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; border: none; }"
        // The scroll bars sit in containers of their own, filled by default.
        "QWidget#qt_scrollarea_vcontainer, QWidget#qt_scrollarea_hcontainer { background: transparent; }"
        "QPushButton#detailsLink { border: none; background: transparent; color: %7; font-size: 12px; padding: 0; }"
        "QPushButton#detailsLink:hover { text-decoration: underline; }"
        "QPushButton#detailsTab { min-height: 30px; max-height: 30px; padding: 0 12px; border: none;"
        "  border-radius: 8px; background: transparent; color: %2; font-size: 13px; }"
        "QPushButton#detailsTab:checked { background: %8; color: %1; font-weight: 600; }"
        "QPushButton#detailsTab:hover:!checked:enabled { color: %1; }"
        "QPushButton#detailsTab:disabled { color: %3; }")
        .arg(rgba(p.ink), rgba(p.muted), rgba(p.faint), rgba(p.hair), rgba(p.stage), rgba(p.pane),
             rgba(p.accent), dark ? QStringLiteral("rgba(255,255,255,26)") : QStringLiteral("rgba(0,0,0,16)"));
    ui_.card->setStyleSheet(style);
    if (ui_.chips) ui_.chips->setPalette(p);
    if (ui_.formatsView) ui_.formatsView->setPalette(p);
    update();
}

void ClipboardItemDetailsDialog::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
    QDialog::mousePressEvent(event);
}

void ClipboardItemDetailsDialog::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - dragOffset_);
        event->accept();
    }
    QDialog::mouseMoveEvent(event);
}

void ClipboardItemDetailsDialog::keyPressEvent(QKeyEvent *event) {
    // 1-5 pick a tab; on the picture, + - 0 zoom as in the Space preview.
    if (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier) {
        const int key = event->key();
        if (ui_.pages->currentIndex() == PreviewPage && ui_.previewStack->currentWidget() == ui_.imageView) {
            if (key == Qt::Key_Plus || key == Qt::Key_Equal) { ui_.imageView->zoomBy(1.15); return; }
            if (key == Qt::Key_Minus) { ui_.imageView->zoomBy(1.0 / 1.15); return; }
            if (key == Qt::Key_0) { ui_.imageView->resetZoom(); return; }
        }
        if (key >= Qt::Key_1 && key <= Qt::Key_5) {
            const int index = key - Qt::Key_1;
            selectTab(index);
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

QString ClipboardItemDetailsDialog::uiText(const QString &source, const QString &zhFallback) const {
    const QString translated = tr(source.toUtf8().constData());
    if (translated == source || BoardHelpers::looksBrokenTranslation(translated)) {
        return BoardHelpers::prefersChineseUi() ? zhFallback : source;
    }
    return translated;
}

QString ClipboardItemDetailsDialog::contentTypeLabel(ContentType type) const {
    switch (type) {
        case Text: return uiText(QStringLiteral("Text"), zh(u"文本"));
        case Link: return uiText(QStringLiteral("Link"), zh(u"链接"));
        case Image: return uiText(QStringLiteral("Image"), zh(u"图片"));
        case Office: return uiText(QStringLiteral("Office Shape"), zh(u"Office 图形"));
        case RichText: return uiText(QStringLiteral("Rich Text"), zh(u"富文本"));
        case File: return uiText(QStringLiteral("File"), zh(u"文件"));
        case Color: return uiText(QStringLiteral("Color"), zh(u"颜色"));
        case All:
        default: return uiText(QStringLiteral("Item"), zh(u"条目"));
    }
}

void ClipboardItemDetailsDialog::selectTab(int index) {
    if (index >= 0 && index < 5 && ui_.tabs[index]->isEnabled() && !ui_.tabs[index]->isHidden()) {
        ui_.tabs[index]->setChecked(true);
        showPage(index);
    }
}

void ClipboardItemDetailsDialog::showPage(int index) {
    if (index >= 0 && index < ui_.pages->count()) {
        ui_.pages->setCurrentIndex(index);
    }
    const bool picture = index == PreviewPage && ui_.previewStack->currentWidget() == ui_.imageView
        && ui_.imageView->hasImage();
    ui_.hintLabel->setText(picture ? uiText(QStringLiteral("Wheel to zoom · drag to move · double-click to fit"),
                                            zh(u"滚轮缩放 · 拖动平移 · 双击适合窗口"))
                                   : QString());
}

void ClipboardItemDetailsDialog::clearInfo() {
    while (ui_.infoLayout->count() > 0) {
        QLayoutItem *item = ui_.infoLayout->itemAt(0);
        if (item->widget() == ui_.techSection) {
            break;
        }
        item = ui_.infoLayout->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void ClipboardItemDetailsDialog::addSection(const QString &title, const QList<QPair<QString, QString>> &rows) {
    if (rows.isEmpty()) {
        return;
    }
    auto *section = new QWidget;
    auto *outer = new QVBoxLayout(section);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *inner = new QVBoxLayout;
    inner->setContentsMargins(kGap, kGap, kGap, kGap);
    inner->setSpacing(12);
    auto *heading = new QLabel(title, section);
    heading->setObjectName(QStringLiteral("detailsSectionTitle"));
    inner->addWidget(heading);
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->setColumnMinimumWidth(0, 56);
    grid->setColumnStretch(1, 1);
    int row = 0;
    for (const auto &pair : rows) {
        auto *key = new QLabel(pair.first, section);
        key->setObjectName(QStringLiteral("detailsKey"));
        auto *value = new QLabel(pair.second, section);
        value->setObjectName(QStringLiteral("detailsValue"));
        value->setTextFormat(Qt::RichText);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        grid->addWidget(key, row, 0, Qt::AlignTop);
        grid->addWidget(value, row, 1, Qt::AlignTop);
        ++row;
    }
    inner->addLayout(grid);
    outer->addLayout(inner);
    // Sections are split by a hairline below each.
    auto *line = new QFrame(section);
    line->setObjectName(QStringLiteral("detailsSectionLine"));
    line->setFixedHeight(1);
    outer->addWidget(line);
    const int techIndex = ui_.infoLayout->indexOf(ui_.techSection);
    ui_.infoLayout->insertWidget(techIndex, section);
}

void ClipboardItemDetailsDialog::fillPreview(const ClipboardItem &item, const ClipboardItem &full) {
    const ContentType type = item.getContentType();
    const Palette p = paletteFor(darkTheme_, accent_);
    ui_.imageView->clear();
    ui_.previewCard->clear();

    auto showPicture = [this](const QPixmap &image) {
        if (image.isNull()) {
            return false;
        }
        // A long screenshot reads from its top, as wide as the pane.
        ui_.imageView->setFitWidth(image.height() > image.width() * 2.5);
        ui_.imageView->setPixmap(image);
        ui_.previewStack->setCurrentWidget(ui_.imageView);
        return true;
    };
    auto showCard = [this](const QString &html) {
        ui_.previewCard->setText(html);
        ui_.previewStack->setCurrentWidget(ui_.previewCard);
    };
    auto showText = [this](const QString &text) {
        ui_.previewText->setPlainText(text);
        ui_.previewStack->setCurrentWidget(ui_.previewText);
    };
    auto small = [&p](const QString &text) {
        return QStringLiteral("<span style=\"color:%1; font-size:12px\">%2</span>").arg(rgba(p.muted), text.toHtmlEscaped());
    };

    switch (type) {
        case Image:
        case Office: {
            if (showPicture(full.getImage()) || showPicture(item.thumbnail())) {
                return;
            }
            break;
        }
        case RichText:
            if (item.hasThumbnail() && showPicture(item.thumbnail())) {
                return;
            }
            break;
        case Link: {
            const QString url = item.getUrl().isEmpty() ? item.getNormalizedText().trimmed() : item.getUrl();
            const QString title = item.getTitle().trimmed();
            // The page as a card: its picture (the card's, already cut to
            // the card's shape, so shown small and whole), its title, where.
            QString picture;
            if (item.hasThumbnail()) {
                QByteArray png;
                QBuffer buffer(&png);
                buffer.open(QIODevice::WriteOnly);
                item.thumbnail().toImage().save(&buffer, "PNG");
                const QSize logical = item.thumbnail().deviceIndependentSize().toSize();
                const int w = qMin(360, logical.width());
                picture = QStringLiteral("<img width=\"%1\" height=\"%2\" src=\"data:image/png;base64,%3\"><br><br>")
                              .arg(w).arg(qRound(w * logical.height() / qMax(1.0, qreal(logical.width()))))
                              .arg(QString::fromLatin1(png.toBase64()));
            }
            showCard(picture + QStringLiteral("<div style=\"font-size:15px; font-weight:600\">%1</div><br><div>%2</div>")
                                   .arg((title.isEmpty() ? CardRenderUtils::displayHost(QUrl(url))
                                                            : QFontMetrics(font()).elidedText(title, Qt::ElideRight, 1400)).toHtmlEscaped(),
                                        small(url)));
            return;
        }
        case File: {
            const QList<QUrl> urls = item.getNormalizedUrls();
            if (!urls.isEmpty() && urls.first().isLocalFile()) {
                const QFileInfo info(urls.first().toLocalFile());
                const bool picture = info.exists() && QImageReader::imageFormat(info.filePath()).size() > 0;
                if (picture && urls.size() == 1) {
                    QImageReader reader(info.filePath());
                    reader.setAutoTransform(true);
                    if (showPicture(QPixmap::fromImage(reader.read()))) {
                        return;
                    }
                }
                const QIcon icon = QFileIconProvider().icon(info);
                const QPixmap pixmap = icon.pixmap(64, 64);
                QString iconHtml;
                if (!pixmap.isNull()) {
                    QByteArray png;
                    QBuffer buffer(&png);
                    buffer.open(QIODevice::WriteOnly);
                    pixmap.save(&buffer, "PNG");
                    iconHtml = QStringLiteral("<img width=\"64\" height=\"64\" src=\"data:image/png;base64,%1\"><br><br>")
                                   .arg(QString::fromLatin1(png.toBase64()));
                }
                const QString more = urls.size() > 1
                    ? uiText(QStringLiteral("and %1 more"), zh(u"等 %1 个")).arg(urls.size()) : QString();
                showCard(iconHtml + QStringLiteral("<div style=\"font-size:14px\">%1</div><div>%2</div>")
                                        .arg(info.fileName().toHtmlEscaped(), small(more.isEmpty() ? info.absolutePath() : more)));
                return;
            }
            break;
        }
        case Color: {
            const QColor color = item.getColor();
            if (color.isValid()) {
                showCard(QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" align=\"center\"><tr><td width=\"200\" height=\"120\" "
                                        "style=\"background:%1\"></td></tr></table><br><div style=\"font-size:15px\">%2</div>")
                             .arg(color.name(QColor::HexArgb), color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb).toUpper()));
                return;
            }
            break;
        }
        default:
            break;
    }
    const QString text = item.getNormalizedText();
    if (!text.trimmed().isEmpty()) {
        showText(text);
    } else {
        showCard(small(uiText(QStringLiteral("Nothing to show"), zh(u"没有可显示的内容"))));
    }
}

void ClipboardItemDetailsDialog::fillInfo(const ClipboardItem &item, const QMimeData *mimeData,
                                          int sequence, int totalCount) {
    clearInfo();
    const bool chinese = BoardHelpers::prefersChineseUi();
    const Palette p = paletteFor(darkTheme_, accent_);
    auto sub = [&p](const QString &main, const QString &note) {
        return note.isEmpty() ? main.toHtmlEscaped()
                              : QStringLiteral("%1<br><span style=\"color:%2; font-size:12px\">%3</span>")
                                    .arg(main.toHtmlEscaped(), rgba(p.faint), note.toHtmlEscaped());
    };
    // An address or a path breaks after its separators, and inside a long
    // run of letters, instead of running out of the pane.
    auto mono = [](const QString &text) {
        QString out;
        int run = 0;
        for (const QChar c : text) {
            out += c;
            ++run;
            if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char('?') || c == QLatin1Char('&')
                || c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char('.') || c.isSpace() || run >= 18) {
                out += QChar(0x200B);
                run = 0;
            }
        }
        return QStringLiteral("<span style=\"font-family:monospace; font-size:12px\">%1</span>").arg(out.toHtmlEscaped());
    };
    // A long title, name or path is cut to a few lines here (the middle of a
    // path, where it says least); the whole is on the Text tab.
    const int valueWidth = kInfoWidth - 2 * kGap - 56 - 12;
    auto clamp = [valueWidth](const QString &text, int lines, Qt::TextElideMode mode, bool monospace) {
        QFont font = QApplication::font();
        if (monospace) {
            font = monoFont(9);
        } else {
            font.setPixelSize(13);
        }
        return QFontMetrics(font).elidedText(text, mode, valueWidth * lines - QFontMetrics(font).averageCharWidth() * 3 * lines);
    };

    const ContentType type = item.getContentType();
    QList<QPair<QString, QString>> rows;
    switch (type) {
        case Image:
        case Office: {
            const QSize size = item.getImagePixelSize();
            if (size.isValid()) {
                const qreal ratio = qMax(size.width(), size.height()) / qreal(qMax(1, qMin(size.width(), size.height())));
                rows << qMakePair(uiText(QStringLiteral("Size"), zh(u"尺寸")),
                                  sub(QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()),
                                      ratio >= 2.5 ? uiText(QStringLiteral("long, about 1 : %1"), zh(u"长图，约 1 : %1"))
                                                         .arg(QString::number(ratio, 'f', 1))
                                                   : QString()));
            }
            QByteArray picture;
            if (mimeData) {
                for (const QString &format : mimeData->formats()) {
                    if (isPictureFormat(format)) {
                        const QByteArray data = mimeData->data(format);
                        if (data.size() > picture.size()) {
                            picture = data;
                        }
                    }
                }
            }
            if (!picture.isEmpty()) {
                bool uncompressed = false;
                const QString kind = pictureKind(picture, &uncompressed);
                QString note = kind;
                if (uncompressed) {
                    note += uiText(QStringLiteral(", uncompressed"), zh(u"，未压缩"));
                }
                rows << qMakePair(uiText(QStringLiteral("Original"), zh(u"原图")), sub(byteLabel(picture.size()), note));
            }
            break;
        }
        case Text:
        case RichText: {
            const QString text = item.getNormalizedText();
            const int lines = text.isEmpty() ? 0 : int(text.count(QLatin1Char('\n'))) + 1;
            rows << qMakePair(uiText(QStringLiteral("Length"), zh(u"长度")),
                              sub(uiText(QStringLiteral("%1 characters"), zh(u"%1 字")).arg(text.size()),
                                  uiText(QStringLiteral("%1 lines"), zh(u"%1 行")).arg(lines)));
            if (type == RichText && mimeData && mimeData->hasHtml()) {
                rows << qMakePair(QStringLiteral("HTML"), byteLabel(mimeData->html().toUtf8().size()).toHtmlEscaped());
            }
            break;
        }
        case Link: {
            const QString url = item.getUrl().isEmpty() ? item.getNormalizedText().trimmed() : item.getUrl();
            if (!item.getTitle().trimmed().isEmpty()) {
                rows << qMakePair(uiText(QStringLiteral("Title"), zh(u"标题")),
                                  clamp(item.getTitle().trimmed(), 4, Qt::ElideRight, false).toHtmlEscaped());
            }
            rows << qMakePair(uiText(QStringLiteral("Address"), zh(u"地址")), mono(clamp(url, 3, Qt::ElideMiddle, true)));
            const QString host = CardRenderUtils::displayHost(QUrl(url));
            if (!host.isEmpty()) {
                rows << qMakePair(uiText(QStringLiteral("Site"), zh(u"网站")), host.toHtmlEscaped());
            }
            break;
        }
        case File: {
            const QList<QUrl> urls = item.getNormalizedUrls();
            if (!urls.isEmpty()) {
                const QString path = urls.first().isLocalFile() ? urls.first().toLocalFile() : urls.first().toString();
                const QFileInfo info(path);
                const QString name = clamp(info.fileName(), 2, Qt::ElideMiddle, false);
                rows << qMakePair(uiText(QStringLiteral("Name"), zh(u"名称")),
                                  urls.size() > 1 ? sub(name, uiText(QStringLiteral("and %1 more"), zh(u"等 %1 个")).arg(urls.size()))
                                                  : name.toHtmlEscaped());
                // The folder it is in; its name is the row above.
                rows << qMakePair(uiText(QStringLiteral("Folder"), zh(u"文件夹")),
                                  mono(clamp(QDir::toNativeSeparators(urls.first().isLocalFile() ? info.absolutePath() : path),
                                             3, Qt::ElideMiddle, true)));
                if (urls.first().isLocalFile()) {
                    const QString state = info.exists()
                        ? QStringLiteral("<span style=\"color:%1\">%2</span>").arg(rgba(p.ok), uiText(QStringLiteral("Still there"), zh(u"仍在原处")))
                        : QStringLiteral("<span style=\"color:%1\">%2</span>").arg(rgba(p.warn), uiText(QStringLiteral("No longer there"), zh(u"已不存在")));
                    const QString note = info.exists()
                        ? (info.isDir() ? uiText(QStringLiteral("folder"), zh(u"文件夹")) : byteLabel(info.size()))
                        : QString();
                    rows << qMakePair(uiText(QStringLiteral("State"), zh(u"状态")),
                                      note.isEmpty() ? state : QStringLiteral("%1<br><span style=\"color:%2; font-size:12px\">%3</span>")
                                                                   .arg(state, rgba(p.faint), note));
                }
            }
            break;
        }
        case Color: {
            const QColor color = item.getColor();
            if (color.isValid()) {
                rows << qMakePair(uiText(QStringLiteral("Value"), zh(u"色值")), mono(color.name(QColor::HexRgb).toUpper()));
                rows << qMakePair(QStringLiteral("RGB"), mono(QStringLiteral("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue())));
                if (color.alpha() < 255) {
                    rows << qMakePair(uiText(QStringLiteral("Opacity"), zh(u"不透明度")), QStringLiteral("%1%").arg(qRound(color.alphaF() * 100)));
                }
            }
            break;
        }
        default:
            break;
    }
    addSection(contentTypeLabel(type), rows);

    // Where it came from.
    QList<QPair<QString, QString>> source;
    if (!item.getSourceApp().isEmpty()) {
        source << qMakePair(uiText(QStringLiteral("App"), zh(u"应用")), item.getSourceApp().toHtmlEscaped());
    }
    source << qMakePair(uiText(QStringLiteral("Copied"), zh(u"复制于")),
                        sub(QLocale::system().toString(item.getTime(), QStringLiteral("yyyy/MM/dd HH:mm:ss")),
                            dayWord(item.getTime(), chinese)));
    if (sequence > 0) {
        source << qMakePair(uiText(QStringLiteral("Position"), zh(u"位置")),
                            (totalCount > 0 ? uiText(QStringLiteral("%1 of %2"), zh(u"第 %1 / %2 条")).arg(sequence).arg(totalCount)
                                            : QString::number(sequence)).toHtmlEscaped());
    }
    addSection(uiText(QStringLiteral("SOURCE"), zh(u"来源")), source);

    // The technical section: the formats and the fingerprint.
    QStringList chips;
    if (mimeData) {
        for (const QString &format : mimeData->formats()) {
            chips << shortFormat(format);
        }
    }
    ui_.chips->setChips(chips);
    ui_.chips->setVisible(!chips.isEmpty());
    const QByteArray fingerprint = item.fingerprint();
    fingerprintHex_ = QString::fromLatin1(fingerprint.toHex());
    ui_.fingerprintValue->setText(fingerprintHex_.isEmpty() ? dashText()
                                  : uiText(QStringLiteral("Fingerprint %1"), zh(u"指纹 %1")).arg(fingerprintHex_.left(24) + QStringLiteral("…")));
    ui_.fingerprintValue->setToolTip(fingerprintHex_);
}

void ClipboardItemDetailsDialog::showItem(const ClipboardItem &item, int sequence, int totalCount) {
    // What it is comes from the item as the board has it; loading its data
    // would classify it again from the formats alone, and some keep their
    // text or address only in the saved header.
    currentItem_ = item;
    const ContentType type = currentItem_.getContentType();
    // Details read every format: a copy brings the data in from disk, and
    // goes when this returns.
    ClipboardItem full = item;
    full.ensureMimeDataLoaded();
    const QMimeData *mimeData = full.getMimeData();

    const QString app = currentItem_.getSourceApp();
    accent_ = ClipboardCardDelegate::appAccentColor(currentItem_.getIcon());
    applyTheme(darkTheme_);
    ui_.iconLabel->setPixmap(ClipboardCardDelegate::appIconTile(currentItem_.getIcon(), app.isEmpty() ? QStringLiteral("M") : app,
                                                                34, devicePixelRatioF()));
    ui_.titleLabel->setText(app.isEmpty() ? contentTypeLabel(type)
                                          : app + QStringLiteral(" · ") + contentTypeLabel(type));
    {
        QStringList parts;
        if (sequence > 0) {
            parts << (totalCount > 0 ? uiText(QStringLiteral("Item %1 of %2"), zh(u"第 %1 / %2 条")).arg(sequence).arg(totalCount)
                                     : QString::number(sequence));
        }
        const QString day = dayWord(currentItem_.getTime(), BoardHelpers::prefersChineseUi());
        parts << (day.isEmpty() ? QLocale::system().toString(currentItem_.getTime(), QLocale::ShortFormat)
                                : day + QLatin1Char(' ') + currentItem_.getTime().toString(QStringLiteral("HH:mm")));
        ui_.subtitleLabel->setText(parts.join(QStringLiteral(" · ")));
    }

    // Actions for what it is.
    ui_.ocrButton->setVisible(type == Image || type == Office);
    ui_.saveButton->setVisible(type != File);
    openTarget_.clear();
    if (type == Link) {
        const QString url = currentItem_.getUrl().isEmpty() ? currentItem_.getNormalizedText().trimmed() : currentItem_.getUrl();
        if (ContentClassifier::isWebLinkText(url)) {
            openTarget_ = url;
            ui_.openButton->setText(uiText(QStringLiteral("Open in Browser"), zh(u"在浏览器中打开")));
        }
    } else if (type == File) {
        const QList<QUrl> urls = currentItem_.getNormalizedUrls();
        if (!urls.isEmpty() && urls.first().isLocalFile() && QFileInfo::exists(urls.first().toLocalFile())) {
            openTarget_ = QUrl::fromLocalFile(QFileInfo(urls.first().toLocalFile()).absolutePath()).toString();
            ui_.openButton->setText(uiText(QStringLiteral("Show in Folder"), zh(u"打开所在文件夹")));
        }
    }
    ui_.openButton->setVisible(!openTarget_.isEmpty());

    // The pages; a tab with nothing behind it is dimmed.
    const QString normalizedText = currentItem_.getNormalizedText();
    QStringList urlLines;
    for (const QUrl &url : currentItem_.getNormalizedUrls()) {
        urlLines << (url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toString(QUrl::FullyEncoded));
    }
    QString textPage = normalizedText.trimmed();
    if (!urlLines.isEmpty() && type == File) {
        textPage = urlLines.join(QLatin1Char('\n'));
    }
    QString rawPage;
    if (mimeData) {
        rawPage = mimeData->text();
        QStringList rawUrls;
        for (const QUrl &url : mimeData->urls()) {
            rawUrls << url.toString(QUrl::FullyEncoded);
        }
        if (!rawUrls.isEmpty()) {
            rawPage += (rawPage.isEmpty() ? QString() : QStringLiteral("\n\n")) + rawUrls.join(QLatin1Char('\n'));
        }
    }
    const QString htmlPage = mimeData ? mimeData->html() : QString();
    ui_.textEdit->setPlainText(textPage);
    ui_.rawEdit->setPlainText(rawPage);
    ui_.htmlEdit->setPlainText(htmlPage);

    fillPreview(currentItem_, full);
    // Text is its own preview: the first tab then reads 文本, and no second one.
    const bool textIsPreview = ui_.previewStack->currentWidget() == ui_.previewText;
    ui_.tabs[PreviewPage]->setText(textIsPreview ? uiText(QStringLiteral("Text"), zh(u"文本"))
                                                 : uiText(QStringLiteral("Preview"), zh(u"预览")));
    ui_.tabs[TextPage]->setVisible(!textIsPreview);
    ui_.tabs[TextPage]->setEnabled(!textPage.trimmed().isEmpty());
    ui_.tabs[RawPage]->setEnabled(!rawPage.trimmed().isEmpty());
    ui_.tabs[HtmlPage]->setEnabled(!htmlPage.trimmed().isEmpty());

    qint64 stored = 0;
    int distinct = 0;
    const QList<FormatEntry> entries = formatEntries(mimeData, &stored, &distinct);
    ui_.formatsView->setEntries(entries, stored, distinct, BoardHelpers::prefersChineseUi());
    ui_.tabs[FormatsPage]->setText(entries.isEmpty() ? uiText(QStringLiteral("Formats"), zh(u"格式"))
                                                     : uiText(QStringLiteral("Formats %1"), zh(u"格式 %1")).arg(entries.size()));
    ui_.tabs[FormatsPage]->setEnabled(!entries.isEmpty());

    fillInfo(currentItem_, mimeData, sequence, totalCount);
    ui_.tabs[PreviewPage]->setChecked(true);
    showPage(PreviewPage);

    QScreen *targetScreen = nullptr;
    if (QWidget *anchor = parentWidget()) {
        if (QWindow *windowHandle = anchor->window()->windowHandle()) {
            targetScreen = windowHandle->screen();
        }
    }
    if (!targetScreen) targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) targetScreen = QGuiApplication::primaryScreen();
    if (targetScreen) {
        move(targetScreen->availableGeometry().center() - QPoint(width() / 2, height() / 2));
    }

    show();
    AuroraChrome::attachBackdrop(this, darkTheme_, kShadow);
    raise();
    activateWindow();
    ui_.imageView->resetZoom();
}
