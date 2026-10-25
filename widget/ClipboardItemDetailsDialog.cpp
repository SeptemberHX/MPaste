// input: Depends on ClipboardItemDetailsDialog.h, Qt form widgets, and ClipboardItem metadata accessors.
// output: Clipboard inspector with native frosted glass and a readable solid fallback.
// pos: Widget-layer details dialog implementation for clipboard debugging and transparency.
// update: If I change, update this header block and my folder README.md.
// note: Added dark theme styling support.
#include "SurfacePainter.h"
#include "ClipboardItemDetailsDialog.h"
#include <QPushButton>
#include <QClipboard>
#include "ClipboardCardDelegate.h"
#include "AuroraChrome.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QScreen>
#include <QStringList>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include "WindowBlurHelper.h"

#include "utils/ThemeManager.h"
#include "BoardInternalHelpers.h"

namespace {
constexpr int kDetailsDialogWidth = 860;
constexpr int kDetailsDialogHeight = 600;

QString zh(const char16_t *text) {
    return QString::fromUtf16(text);
}

QString dashText() {
    return QString::fromUtf16(u"\u2014");
}

QPixmap scalePixmapForLabel(const QPixmap &pixmap, const QSize &targetSize, qreal devicePixelRatio) {
    if (pixmap.isNull() || !targetSize.isValid()) {
        return pixmap;
    }

    const QSize pixelTargetSize = targetSize * devicePixelRatio;
    if (!pixelTargetSize.isValid()) {
        return pixmap;
    }

    QPixmap scaled = pixmap.scaled(pixelTargetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(devicePixelRatio);
    return scaled;
}

QFont preferredEditorFont() {
    QFont font = QApplication::font();
    font.setPointSize(11);

    const QStringList preferredFamilies = {
        QStringLiteral("Cascadia Mono"),
        QStringLiteral("Cascadia Code"),
        QStringLiteral("Consolas"),
        QStringLiteral("Courier New")
    };
    const QStringList availableFamilies = QFontDatabase::families();
    for (const QString &family : preferredFamilies) {
        if (availableFamilies.contains(family, Qt::CaseInsensitive)) {
            font.setFamily(family);
            break;
        }
    }

    return font;
}

QString detailsAuroraStyle(bool dark) {
    const QString ink = dark ? QStringLiteral("rgba(255,255,255,225)") : QStringLiteral("rgba(28,30,37,225)");
    const QString faint = dark ? QStringLiteral("rgba(255,255,255,115)") : QStringLiteral("rgba(28,30,37,125)");
    const QString well = dark ? QStringLiteral("rgba(0,0,0,82)") : QStringLiteral("rgba(255,255,255,200)");
    const QString edge = dark ? QStringLiteral("rgba(255,255,255,16)") : QStringLiteral("rgba(0,0,0,16)");
    const QString fill = dark ? QStringLiteral("rgba(255,255,255,13)") : QStringLiteral("rgba(0,0,0,11)");
    const QString chosen = dark ? QStringLiteral("rgba(255,255,255,36)") : QStringLiteral("#FFFFFF");
    const QString soft = dark ? QStringLiteral("rgba(255,255,255,150)") : QStringLiteral("rgba(28,30,37,150)");
    return QStringLiteral(
        "QFrame#detailsCard { background: transparent; border: none; }"
        "QLabel#sectionLabel { color: %2; font-size: 11.5px; background: transparent; }"
        "QLabel#valueLabel { color: %1; font-size: 13px; background: transparent; }"
        "QLabel#fingerprintValue { color: %7; font-family: monospace; font-size: 11.5px; background: transparent; }"
        "QLabel#contentWell { background: %3; border: 1px solid %4; border-radius: 14px; padding: 14px 16px;"
        "  color: %1; font-size: 14px; }"
        "QTabWidget::pane { border: none; background: transparent; }"
        "QTabBar { background: transparent; }"
        "QTabBar::tab { min-height: 28px; max-height: 28px; padding: 0 14px; margin-right: 2px; border: none;"
        "  border-radius: 13px; background: transparent; color: %7; font-size: 12.5px; }"
        "QTabBar::tab:first { margin-left: 3px; }"
        "QTabBar::tab:selected { background: %6; color: %1; font-weight: 600; }"
        "QTabBar::tab:hover:!selected { color: %1; background: %5; }"
        "QTextEdit { background: %3; border: 1px solid %4; border-radius: 14px; color: %1; padding: 8px 10px;"
        "  font-family: monospace; font-size: 12.5px; selection-background-color: rgba(91,141,239,150); }")
        .arg(ink, faint, well, edge, fill, chosen, soft);
}
}

ClipboardItemDetailsDialog::ClipboardItemDetailsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    setModal(false);
    setWindowTitle(QStringLiteral("MPaste Details"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kDetailsDialogWidth);
    resize(kDetailsDialogWidth, kDetailsDialogHeight);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);

    ui_.card = new QFrame(this);
    ui_.card->setObjectName("detailsCard");
    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ClipboardItemDetailsDialog::applyTheme);

    rootLayout->addWidget(ui_.card);

    auto *cardLayout = new QVBoxLayout(ui_.card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    auto *contentLayout = new QVBoxLayout;
    contentLayout->setContentsMargins(16, 14, 16, 16);
    contentLayout->setSpacing(10);
    cardLayout->addLayout(contentLayout);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(4, 2, 0, 4);
    headerLayout->setSpacing(12);

    ui_.iconLabel = new QLabel(ui_.card);
    ui_.iconLabel->setFixedSize(32, 32);
    headerLayout->addWidget(ui_.iconLabel, 0, Qt::AlignVCenter);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(1);
    ui_.titleLabel = new QLabel(ui_.card);
    ui_.titleLabel->setObjectName("auroraTitle");
    titles->addWidget(ui_.titleLabel);
    ui_.subtitleLabel = new QLabel(ui_.card);
    ui_.subtitleLabel->setObjectName("auroraSubtitle");
    titles->addWidget(ui_.subtitleLabel);
    headerLayout->addLayout(titles, 1);

    ui_.closeButton = new QToolButton(ui_.card);
    ui_.closeButton->setObjectName("auroraClose");
    ui_.closeButton->setText(QStringLiteral("✕"));
    ui_.closeButton->setCursor(Qt::PointingHandCursor);
    connect(ui_.closeButton, &QToolButton::clicked, this, &QDialog::reject);
    headerLayout->addWidget(ui_.closeButton, 0, Qt::AlignVCenter);
    contentLayout->addLayout(headerLayout);

    auto createValueLabel = [this]() {
        auto *label = new QLabel(ui_.card);
        label->setObjectName("valueLabel");
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };

    auto createSectionLabel = [this](const QString &source, const QString &zhFallback) {
        auto *label = new QLabel(uiText(source, zhFallback), ui_.card);
        label->setObjectName("sectionLabel");
        return label;
    };

    ui_.tabs = new QTabWidget(ui_.card);
    ui_.tabs->setDocumentMode(true);
    contentLayout->addWidget(ui_.tabs, 1);

    // Overview: the content itself, the facts in two columns, the fingerprint last.
    auto *overviewTab = new QWidget(ui_.tabs);
    auto *overviewLayout = new QVBoxLayout(overviewTab);
    overviewLayout->setContentsMargins(0, 12, 0, 0);
    overviewLayout->setSpacing(12);

    ui_.previewVisual = new QLabel(overviewTab);
    ui_.previewVisual->setObjectName("contentWell");
    ui_.previewVisual->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    ui_.previewVisual->setMinimumHeight(96);
    ui_.previewVisual->setMaximumHeight(170);
    ui_.previewVisual->setWordWrap(true);
    ui_.previewVisual->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui_.previewVisual->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    overviewLayout->addWidget(ui_.previewVisual);
    // Kept for the code that fills them; the overview no longer shows them.
    ui_.previewThumbnail = new QLabel(overviewTab);
    ui_.previewThumbnail->hide();
    ui_.previewSummaryValue = createValueLabel();
    ui_.previewSummaryValue->hide();
    ui_.sequenceValue = createValueLabel();
    ui_.sequenceValue->hide();
    ui_.nameValue = createValueLabel();
    ui_.nameValue->hide();
    ui_.urlCountValue = createValueLabel();
    ui_.urlCountValue->hide();

    ui_.typeValue = createValueLabel();
    ui_.sourceValue = createValueLabel();
    ui_.timeValue = createValueLabel();
    ui_.titleValue = createValueLabel();
    ui_.urlValue = createValueLabel();
    ui_.fingerprintValue = createValueLabel();
    ui_.fingerprintValue->setObjectName("fingerprintValue");
    ui_.fingerprintValue->setWordWrap(false);
    ui_.formatCountValue = createValueLabel();
    ui_.mimeBytesValue = createValueLabel();
    ui_.textLengthValue = createValueLabel();

    auto *metaCard = new QFrame(overviewTab);
    metaCard->setObjectName("auroraPanel");
    auto *metaGrid = new QGridLayout(metaCard);
    metaGrid->setContentsMargins(16, 14, 16, 14);
    metaGrid->setHorizontalSpacing(24);
    metaGrid->setVerticalSpacing(12);
    metaGrid->setColumnStretch(0, 1);
    metaGrid->setColumnStretch(1, 1);
    auto fact = [&](const QString &source, const QString &zhText, QLabel *value) {
        auto *box = new QWidget(metaCard);
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(0, 0, 0, 0);
        boxLayout->setSpacing(2);
        boxLayout->addWidget(createSectionLabel(source, zhText));
        value->setParent(box);
        boxLayout->addWidget(value);
        return box;
    };
    metaGrid->addWidget(fact(QStringLiteral("Type"), zh(u"类型"), ui_.typeValue), 0, 0);
    metaGrid->addWidget(fact(QStringLiteral("Source"), zh(u"来源"), ui_.sourceValue), 0, 1);
    metaGrid->addWidget(fact(QStringLiteral("Copied"), zh(u"复制时间"), ui_.timeValue), 1, 0);
    metaGrid->addWidget(fact(QStringLiteral("Text Length"), zh(u"文本长度"), ui_.textLengthValue), 1, 1);
    metaGrid->addWidget(fact(QStringLiteral("Formats"), zh(u"格式数"), ui_.formatCountValue), 2, 0);
    metaGrid->addWidget(fact(QStringLiteral("Size"), zh(u"大小"), ui_.mimeBytesValue), 2, 1);
    ui_.titleRow = fact(QStringLiteral("Title"), zh(u"标题"), ui_.titleValue);
    metaGrid->addWidget(ui_.titleRow, 3, 0, 1, 2);
    ui_.urlRow = fact(QStringLiteral("URL"), zh(u"链接"), ui_.urlValue);
    metaGrid->addWidget(ui_.urlRow, 4, 0, 1, 2);
    overviewLayout->addWidget(metaCard);

    auto *fingerprintRow = new QHBoxLayout;
    fingerprintRow->setContentsMargins(4, 0, 0, 0);
    fingerprintRow->setSpacing(10);
    fingerprintRow->addWidget(createSectionLabel(QStringLiteral("Fingerprint"), zh(u"指纹")));
    ui_.fingerprintValue->setParent(overviewTab);
    fingerprintRow->addWidget(ui_.fingerprintValue, 1);
    auto *copyFingerprint = new QPushButton(uiText(QStringLiteral("Copy"), zh(u"复制")), overviewTab);
    copyFingerprint->setObjectName("auroraSecondary");
    copyFingerprint->setCursor(Qt::PointingHandCursor);
    connect(copyFingerprint, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(ui_.fingerprintValue->text());
    });
    fingerprintRow->addWidget(copyFingerprint);
    overviewLayout->addLayout(fingerprintRow);
    overviewLayout->addStretch(1);
    ui_.tabs->addTab(overviewTab, uiText(QStringLiteral("Overview"), zh(u"概览")));

    auto *normalizedTab = new QWidget(ui_.tabs);
    auto *normalizedLayout = new QVBoxLayout(normalizedTab);
    normalizedLayout->setContentsMargins(0, 12, 0, 0);
    normalizedLayout->setSpacing(10);
    ui_.normalizedTextEdit = createReadOnlyEditor(false);
    ui_.normalizedUrlsEdit = createReadOnlyEditor(true);
    ui_.normalizedTextEdit->setMinimumHeight(180);
    ui_.normalizedUrlsEdit->setMinimumHeight(140);
    normalizedLayout->addWidget(createSectionLabel(QStringLiteral("Normalized Text"), zh(u"标准化文本")));
    normalizedLayout->addWidget(ui_.normalizedTextEdit, 1);
    normalizedLayout->addWidget(createSectionLabel(QStringLiteral("Normalized URLs"), zh(u"标准化链接")));
    normalizedLayout->addWidget(ui_.normalizedUrlsEdit, 1);
    ui_.tabs->addTab(normalizedTab, uiText(QStringLiteral("Normalized"), zh(u"标准化")));

    auto *rawTab = new QWidget(ui_.tabs);
    auto *rawLayout = new QVBoxLayout(rawTab);
    rawLayout->setContentsMargins(0, 12, 0, 0);
    rawLayout->setSpacing(10);
    ui_.rawTextEdit = createReadOnlyEditor(false);
    ui_.rawHtmlEdit = createReadOnlyEditor(true);
    ui_.rawUrlsEdit = createReadOnlyEditor(true);
    ui_.rawTextEdit->setMinimumHeight(120);
    ui_.rawHtmlEdit->setMinimumHeight(150);
    ui_.rawUrlsEdit->setMinimumHeight(120);
    rawLayout->addWidget(createSectionLabel(QStringLiteral("Raw Text"), zh(u"原始文本")));
    rawLayout->addWidget(ui_.rawTextEdit, 1);
    rawLayout->addWidget(createSectionLabel(QStringLiteral("Raw HTML"), zh(u"原始 HTML")));
    rawLayout->addWidget(ui_.rawHtmlEdit, 1);
    rawLayout->addWidget(createSectionLabel(QStringLiteral("Raw URLs"), zh(u"原始链接")));
    rawLayout->addWidget(ui_.rawUrlsEdit, 1);
    ui_.tabs->addTab(rawTab, uiText(QStringLiteral("Raw"), zh(u"原始内容")));

    auto *mimeTab = new QWidget(ui_.tabs);
    auto *mimeLayout = new QVBoxLayout(mimeTab);
    mimeLayout->setContentsMargins(0, 12, 0, 0);
    mimeLayout->setSpacing(10);
    ui_.mimeFormatsEdit = createReadOnlyEditor(true);
    ui_.mimeFormatsEdit->setMinimumHeight(360);
    mimeLayout->addWidget(createSectionLabel(QStringLiteral("MIME Formats and Sizes"), zh(u"MIME 格式与大小")));
    mimeLayout->addWidget(ui_.mimeFormatsEdit, 1);
    ui_.tabs->addTab(mimeTab, uiText(QStringLiteral("MIME"), zh(u"MIME")));
}

void ClipboardItemDetailsDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    AuroraChrome::paintWindow(painter, QRectF(rect()).adjusted(10, 10, -10, -10), darkTheme_, accent_);
}

void ClipboardItemDetailsDialog::applyTheme(bool dark) {
    darkTheme_ = dark;
    WindowBlurHelper::enableBlurBehind(this, darkTheme_);
    const QString style = AuroraChrome::styleSheet(darkTheme_) + detailsAuroraStyle(darkTheme_);
    if (ui_.card) {
        ui_.card->setStyleSheet(style);
    } else {
        setStyleSheet(style);
    }
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

QString ClipboardItemDetailsDialog::uiText(const QString &source, const QString &zhFallback) const {
    const QString translated = tr(source.toUtf8().constData());
    if (translated == source || BoardHelpers::looksBrokenTranslation(translated)) {
        return BoardHelpers::prefersChineseUi() ? zhFallback : source;
    }
    return translated;
}

QString ClipboardItemDetailsDialog::contentTypeLabel(ContentType type) const {
    switch (type) {
        case Text:
            return uiText(QStringLiteral("Text"), zh(u"文本"));
        case Link:
            return uiText(QStringLiteral("Link"), zh(u"链接"));
        case Image:
            return uiText(QStringLiteral("Image"), zh(u"图片"));
        case Office:
            return uiText(QStringLiteral("Office Shape"), zh(u"Office Shape"));
        case RichText:
            return uiText(QStringLiteral("Rich Text"), zh(u"富文本"));
        case File:
            return uiText(QStringLiteral("File"), zh(u"文件"));
        case Color:
            return uiText(QStringLiteral("Color"), zh(u"颜色"));
        case All:
        default:
            return uiText(QStringLiteral("All"), zh(u"全部"));
    }
}

QString ClipboardItemDetailsDialog::joinUrls(const QList<QUrl> &urls) const {
    QStringList lines;
    for (const QUrl &url : urls) {
        lines << (url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded));
    }
    return lines.join(QStringLiteral("\n"));
}

QString ClipboardItemDetailsDialog::byteCountLabel(qint64 bytes) const {
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    }
    return QStringLiteral("%1 MB").arg(QString::number(bytes / 1024.0 / 1024.0, 'f', 2));
}

qint64 ClipboardItemDetailsDialog::totalMimeBytes(const QMimeData *mimeData) const {
    if (!mimeData) {
        return 0;
    }

    qint64 totalBytes = 0;
    for (const QString &format : mimeData->formats()) {
        totalBytes += mimeData->data(format).size();
    }
    return totalBytes;
}

QString ClipboardItemDetailsDialog::mimeFormatsReport(const QMimeData *mimeData) const {
    if (!mimeData) {
        return dashText();
    }

    QStringList lines;
    for (const QString &format : mimeData->formats()) {
        const QByteArray data = mimeData->data(format);
        lines << QStringLiteral("%1\n  %2")
                     .arg(format,
                          byteCountLabel(data.size()));
    }
    return lines.isEmpty() ? dashText() : lines.join(QStringLiteral("\n\n"));
}

QTextEdit *ClipboardItemDetailsDialog::createReadOnlyEditor(bool noWrap) const {
    auto *edit = new QTextEdit;
    edit->setReadOnly(true);
    edit->setAcceptRichText(false);
    edit->setLineWrapMode(noWrap ? QTextEdit::NoWrap : QTextEdit::WidgetWidth);
    edit->setFont(preferredEditorFont());
    return edit;
}

void ClipboardItemDetailsDialog::updatePreviewVisual(const ClipboardItem &item) {
    QPixmap previewPixmap;
    QPixmap thumbnailPixmap;
    QString summary;

    switch (item.getContentType()) {
        case Image:
        case Office: {
            previewPixmap = item.getImage();
            if (item.hasThumbnail()) {
                thumbnailPixmap = item.thumbnail();
            }
            if (!previewPixmap.isNull()) {
                summary = QStringLiteral("%1 × %2 px")
                              .arg(previewPixmap.width())
                              .arg(previewPixmap.height());
                if (!thumbnailPixmap.isNull()) {
                    summary += QStringLiteral(" | %1: %2 × %3 px")
                                   .arg(uiText(QStringLiteral("Thumbnail"), zh(u"缩略图")))
                                   .arg(thumbnailPixmap.width())
                                   .arg(thumbnailPixmap.height());
                }
            }
            break;
        }
        case RichText: {
            if (item.hasThumbnail()) {
                previewPixmap = item.thumbnail();
                thumbnailPixmap = item.thumbnail();
                summary = uiText(QStringLiteral("Rich Text Snapshot"), zh(u"富文本快照"));
                summary += QStringLiteral(" | %1: %2 × %3 px")
                               .arg(uiText(QStringLiteral("Thumbnail"), zh(u"缩略图")))
                               .arg(thumbnailPixmap.width())
                               .arg(thumbnailPixmap.height());
            } else {
                summary = uiText(QStringLiteral("Rich Text"), zh(u"富文本"));
            }
            break;
        }
        case Color: {
            const QColor color = item.getColor();
            if (color.isValid()) {
                QPixmap swatch(180, 120);
                swatch.fill(Qt::transparent);
                QPainter painter(&swatch);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(QPen(QColor(255, 255, 255, 180), 2));
                painter.setBrush(color);
                painter.drawRoundedRect(QRectF(6, 6, 168, 108), 18, 18);
                previewPixmap = swatch;
                summary = color.name(QColor::HexArgb).toUpper();
            }
            break;
        }
        case Link:
        case Text:
        case File:
            if (item.hasThumbnail()) {
                previewPixmap = item.thumbnail();
                thumbnailPixmap = item.thumbnail();
                summary = QStringLiteral("%1 | %2: %3 × %4 px")
                              .arg(contentTypeLabel(item.getContentType()))
                              .arg(uiText(QStringLiteral("Thumbnail"), zh(u"缩略图")))
                              .arg(thumbnailPixmap.width())
                              .arg(thumbnailPixmap.height());
            } else if (!item.getFavicon().isNull()) {
                previewPixmap = item.getFavicon();
                summary = uiText(QStringLiteral("Favicon Preview"), zh(u"站点图标预览"));
            } else {
                previewPixmap = item.getIcon();
                summary = uiText(QStringLiteral("Item Icon"), zh(u"条目标识"));
            }
            break;
        default:
            if (!item.getFavicon().isNull()) {
                previewPixmap = item.getFavicon();
                summary = uiText(QStringLiteral("Favicon Preview"), zh(u"站点图标预览"));
            } else {
                previewPixmap = item.getIcon();
                summary = uiText(QStringLiteral("Item Icon"), zh(u"条目标识"));
            }
            break;
    }

    // Words show as words; only pictures show as pictures.
    const ContentType type = item.getContentType();
    const bool showsText = (type == Text || type == Link || type == File
                            || (type == RichText && !item.hasThumbnail()))
        && !item.getNormalizedText().trimmed().isEmpty();
    if (showsText) {
        ui_.previewVisual->setPixmap(QPixmap());
        ui_.previewVisual->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        ui_.previewVisual->setText(item.getNormalizedText().trimmed().left(600).toHtmlEscaped()
                                       .replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    } else if (!previewPixmap.isNull()) {
        ui_.previewVisual->setAlignment(Qt::AlignCenter);
        ui_.previewVisual->setPixmap(scalePixmapForLabel(previewPixmap,
                                                        ui_.previewVisual->size(),
                                                        ui_.previewVisual->devicePixelRatioF()));
        ui_.previewVisual->setText(QString());
    } else {
        ui_.previewVisual->setPixmap(QPixmap());
        ui_.previewVisual->setText(uiText(QStringLiteral("No visual preview available"), zh(u"暂无可视预览")));
    }

    if (!thumbnailPixmap.isNull()) {
        ui_.previewThumbnail->setPixmap(scalePixmapForLabel(thumbnailPixmap,
                                                           ui_.previewThumbnail->size(),
                                                           ui_.previewThumbnail->devicePixelRatioF()));
        ui_.previewThumbnail->show();
    } else {
        ui_.previewThumbnail->setPixmap(QPixmap());
        ui_.previewThumbnail->hide();
    }

    ui_.previewSummaryValue->setText(summary.isEmpty() ? dashText() : summary);
}

void ClipboardItemDetailsDialog::showItem(const ClipboardItem &item, int sequence, int totalCount) {
    // Ensure full MIME data is loaded before showing details
    const_cast<ClipboardItem&>(item).ensureMimeDataLoaded();
    const QMimeData *mimeData = item.getMimeData();
    const QString title = item.getTitle().trimmed();
    const QString url = item.getUrl().trimmed();
    const QString normalizedText = item.getNormalizedText().trimmed();
    const QList<QUrl> normalizedUrlsList = item.getNormalizedUrls();
    const QString normalizedUrls = joinUrls(normalizedUrlsList).trimmed();
    const QString rawText = mimeData ? mimeData->text().trimmed() : QString();
    const QString rawHtml = mimeData ? mimeData->html().trimmed() : QString();
    const QString rawUrls = mimeData ? joinUrls(mimeData->urls()).trimmed() : QString();
    const QByteArray fingerprint = item.fingerprint();

    const QString app = item.getSourceApp();
    accent_ = ClipboardCardDelegate::appAccentColor(item.getIcon());
    ui_.iconLabel->setPixmap(ClipboardCardDelegate::appIconTile(item.getIcon(), app.isEmpty() ? QStringLiteral("M") : app,
                                                                32, devicePixelRatioF()));
    ui_.titleLabel->setText(app.isEmpty() ? contentTypeLabel(item.getContentType())
                                          : app + QStringLiteral(" \u00B7 ") + contentTypeLabel(item.getContentType()));
    {
        QStringList parts;
        if (sequence > 0) {
            parts << (totalCount > 0 ? uiText(QStringLiteral("Item %1 of %2"), zh(u"第 %1 / %2 条")).arg(sequence).arg(totalCount)
                                     : QString::number(sequence));
        }
        parts << QLocale::system().toString(item.getTime(), QLocale::ShortFormat);
        ui_.subtitleLabel->setText(parts.join(QStringLiteral(" \u00B7 ")));
    }
    ui_.sourceValue->setText(app.isEmpty() ? dashText() : app);
    ui_.titleRow->setVisible(!title.isEmpty());
    ui_.urlRow->setVisible(!url.isEmpty());
    update();
    if (sequence > 0) {
        ui_.sequenceValue->setText(totalCount > 0
            ? QStringLiteral("%1 / %2").arg(sequence).arg(totalCount)
            : QString::number(sequence));
    } else {
        ui_.sequenceValue->setText(dashText());
    }
    ui_.typeValue->setText(contentTypeLabel(item.getContentType()));
    ui_.timeValue->setText(QLocale::system().toString(item.getTime(), QStringLiteral("yyyy/MM/dd HH:mm:ss")));
    ui_.nameValue->setText(item.getName().isEmpty() ? dashText() : item.getName());
    ui_.titleValue->setText(title.isEmpty() ? dashText() : title);
    ui_.urlValue->setText(url.isEmpty() ? dashText() : url);
    ui_.fingerprintValue->setText(fingerprint.isEmpty() ? dashText() : QString::fromLatin1(fingerprint.toHex()));
    ui_.formatCountValue->setText(mimeData ? QString::number(mimeData->formats().size()) : QStringLiteral("0"));
    ui_.mimeBytesValue->setText(byteCountLabel(totalMimeBytes(mimeData)));
    ui_.textLengthValue->setText(uiText(QStringLiteral("%1 characters"), zh(u"%1 字符")).arg(normalizedText.size()));
    ui_.urlCountValue->setText(QString::number(normalizedUrlsList.size()));

    ui_.normalizedTextEdit->setPlainText(normalizedText.isEmpty() ? dashText() : normalizedText);
    ui_.normalizedUrlsEdit->setPlainText(normalizedUrls.isEmpty() ? dashText() : normalizedUrls);
    ui_.rawTextEdit->setPlainText(rawText.isEmpty() ? dashText() : rawText);
    ui_.rawHtmlEdit->setPlainText(rawHtml.isEmpty() ? dashText() : rawHtml);
    ui_.rawUrlsEdit->setPlainText(rawUrls.isEmpty() ? dashText() : rawUrls);
    ui_.mimeFormatsEdit->setPlainText(mimeFormatsReport(mimeData));

    updatePreviewVisual(item);
    ui_.tabs->setCurrentIndex(0);

    QScreen *targetScreen = nullptr;
    if (QWidget *anchor = parentWidget()) {
        if (QWindow *windowHandle = anchor->window()->windowHandle()) {
            targetScreen = windowHandle->screen();
        }
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (targetScreen) {
        const QRect target = targetScreen->availableGeometry();
        move(target.center() - QPoint(width() / 2, height() / 2));
    }

    show();
    WindowBlurHelper::enableBlurBehind(this, darkTheme_);
    raise();
    activateWindow();
}
