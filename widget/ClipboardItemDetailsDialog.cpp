// input: Depends on ClipboardItemDetailsDialog.h, Qt form widgets, and ClipboardItem metadata accessors.
// output: Implements a polished clipboard item inspector dialog aligned with the app's rounded glass-card language.
// pos: Widget-layer details dialog implementation for clipboard debugging and transparency.
// update: If I change, update this header block and my folder README.md.
// note: Added dark theme styling support.
#include "ClipboardItemDetailsDialog.h"

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
constexpr int kDetailsDialogHeight = 640;

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

QString detailsStyleSheet(bool dark) {
    if (dark) {
        return QStringLiteral(R"(
            QFrame#detailsCard {
                background: transparent;
                border: none;
                border-radius: 18px;
            }
            QLabel#detailsTitle {
                color: #E6EDF5;
                font-size: 19px;
                font-weight: 700;
                background: transparent;
            }
            QLabel#sectionLabel {
                color: #8B97A6;
                font-size: 11px;
                font-weight: 700;
                background: transparent;
                letter-spacing: 0.4px;
                padding-bottom: 1px;
            }
            QLabel#valueLabel {
                color: #D6DEE8;
                font-size: 13px;
                background: transparent;
            }
            QLabel#previewVisual, QLabel#previewThumbnail {
                background-color: #1F242D;
                border: 1px solid rgba(116, 154, 214, 40);
                border-radius: 14px;
                color: #9AA7B5;
                padding: 12px;
            }
            QLabel#previewThumbnail {
                padding: 8px;
            }
            QFrame#metaCard, QFrame#previewCard {
                background-color: #1E232B;
                border: 1px solid rgba(116, 154, 214, 40);
                border-radius: 14px;
            }
            QTextEdit {
                background-color: #1F242D;
                border: 1px solid rgba(116, 154, 214, 40);
                border-radius: 12px;
                padding: 10px 12px;
                color: #E6EDF5;
                selection-background-color: rgba(74, 144, 226, 110);
            }
            QTextEdit:focus {
                border-color: rgba(116, 154, 214, 80);
            }
            QToolButton#closeButton {
                background-color: #1F242D;
                border: 1px solid rgba(116, 154, 214, 40);
                border-radius: 14px;
                color: #C9D4E0;
                font-size: 17px;
                font-weight: 700;
                min-width: 28px;
                min-height: 28px;
            }
            QToolButton#closeButton:hover {
                background-color: #27303A;
                border-color: rgba(116, 154, 214, 80);
            }
            QTabWidget::pane {
                border-left: 1px solid rgba(116, 154, 214, 40);
                border-right: 1px solid rgba(116, 154, 214, 40);
                border-bottom: 1px solid rgba(116, 154, 214, 40);
                border-top: none;
                border-radius: 14px;
                background-color: #1E232B;
                margin-top: 10px;
                top: 0px;
            }
            QTabBar::tab {
                background-color: #1F242D;
                border: 1px solid rgba(116, 154, 214, 40);
                color: #9AA7B5;
                border-radius: 12px;
                padding: 6px 12px;
                margin-right: 6px;
                font-weight: 600;
                font-size: 12px;
            }
            QTabBar::tab:selected {
                background-color: #1E232B;
                border-color: rgba(116, 154, 214, 80);
                color: #E6EDF5;
            }
            QTabBar::tab:hover:!selected {
                background-color: #27303A;
            }
        )");
    }

    return QStringLiteral(R"(
        QFrame#detailsCard {
            background: transparent;
            border: none;
            border-radius: 18px;
        }
        QLabel#detailsTitle {
            color: #1E2936;
            font-size: 19px;
            font-weight: 700;
            background: transparent;
        }
        QLabel#sectionLabel {
            color: #6A7A8C;
            font-size: 11px;
            font-weight: 700;
            background: transparent;
            letter-spacing: 0.4px;
            padding-bottom: 1px;
        }
        QLabel#valueLabel {
            color: #1E2936;
            font-size: 13px;
            background: transparent;
        }
        QLabel#previewVisual, QLabel#previewThumbnail {
            background-color: #FFFFFF;
            border: 1px solid rgba(74, 144, 226, 16);
            border-radius: 14px;
            color: #5F7286;
            padding: 12px;
        }
        QLabel#previewThumbnail {
            padding: 8px;
        }
        QFrame#metaCard, QFrame#previewCard {
            background-color: #FFFFFF;
            border: 1px solid rgba(74, 144, 226, 16);
            border-radius: 14px;
        }
        QTextEdit {
            background-color: #FFFFFF;
            border: 1px solid rgba(74, 144, 226, 16);
            border-radius: 12px;
            padding: 10px 12px;
            color: #1E2936;
            selection-background-color: rgba(74, 144, 226, 76);
        }
        QTextEdit:focus {
            border-color: rgba(74, 144, 226, 30);
        }
        QToolButton#closeButton {
            background-color: #FFFFFF;
            border: 1px solid rgba(74, 144, 226, 16);
            border-radius: 14px;
            color: #5E7084;
            font-size: 17px;
            font-weight: 700;
            min-width: 28px;
            min-height: 28px;
        }
        QToolButton#closeButton:hover {
            background-color: #F7FAFF;
            border-color: rgba(74, 144, 226, 24);
        }
        QTabWidget::pane {
            border-left: 1px solid rgba(74, 144, 226, 18);
            border-right: 1px solid rgba(74, 144, 226, 18);
            border-bottom: 1px solid rgba(74, 144, 226, 18);
            border-top: none;
            border-radius: 14px;
            background-color: #FFFFFF;
            margin-top: 10px;
            top: 0px;
        }
        QTabBar::tab {
            background-color: #FFFFFF;
            border: 1px solid rgba(74, 144, 226, 14);
            color: #54687A;
            border-radius: 12px;
            padding: 6px 12px;
            margin-right: 6px;
            font-weight: 600;
            font-size: 12px;
        }
        QTabBar::tab:selected {
            background-color: #FFFFFF;
            border-color: rgba(74, 144, 226, 26);
            color: #17324D;
        }
        QTabBar::tab:hover:!selected {
            background-color: #F7FAFF;
        }
    )");
}
}

ClipboardItemDetailsDialog::ClipboardItemDetailsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    setModal(false);
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
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);

    ui_.titleLabel = new QLabel(ui_.card);
    ui_.titleLabel->setObjectName("detailsTitle");
    ui_.titleLabel->setText(uiText(QStringLiteral("Clipboard Item Details"), zh(u"剪贴板条目详情")));

    ui_.closeButton = new QToolButton(ui_.card);
    ui_.closeButton->setObjectName("closeButton");
    ui_.closeButton->setText(QStringLiteral("×"));
    ui_.closeButton->setCursor(Qt::PointingHandCursor);
    connect(ui_.closeButton, &QToolButton::clicked, this, &QDialog::reject);

    headerLayout->addWidget(ui_.titleLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(ui_.closeButton);
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

    auto *overviewTab = new QWidget(ui_.tabs);
    auto *overviewLayout = new QVBoxLayout(overviewTab);
    overviewLayout->setContentsMargins(14, 14, 14, 14);
    overviewLayout->setSpacing(10);

    auto *overviewTopLayout = new QHBoxLayout;
    overviewTopLayout->setContentsMargins(0, 0, 0, 0);
    overviewTopLayout->setSpacing(10);

    auto *previewCard = new QFrame(overviewTab);
    previewCard->setObjectName("previewCard");
    previewCard->setFixedWidth(230);
    auto *previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(12, 12, 12, 12);
    previewLayout->setSpacing(10);
    ui_.previewVisual = new QLabel(previewCard);
    ui_.previewVisual->setObjectName("previewVisual");
    ui_.previewVisual->setAlignment(Qt::AlignCenter);
    ui_.previewVisual->setMinimumSize(180, 108);
    ui_.previewVisual->setMaximumHeight(144);
    ui_.previewVisual->setScaledContents(false);
    ui_.previewVisual->setWordWrap(true);
    ui_.previewThumbnail = new QLabel(previewCard);
    ui_.previewThumbnail->setObjectName("previewThumbnail");
    ui_.previewThumbnail->setAlignment(Qt::AlignCenter);
    ui_.previewThumbnail->setFixedSize(180, 143);
    ui_.previewThumbnail->setScaledContents(false);
    ui_.previewThumbnail->hide();
    ui_.previewSummaryValue = createValueLabel();
    previewLayout->addWidget(ui_.previewVisual, 0);
    previewLayout->addWidget(ui_.previewThumbnail, 0, Qt::AlignHCenter);
    previewLayout->addWidget(createSectionLabel(QStringLiteral("Preview"), zh(u"预览")));
    previewLayout->addWidget(ui_.previewSummaryValue, 0);
    overviewTopLayout->addWidget(previewCard, 0);

    ui_.sequenceValue = createValueLabel();
    ui_.typeValue = createValueLabel();
    ui_.timeValue = createValueLabel();
    ui_.nameValue = createValueLabel();
    ui_.titleValue = createValueLabel();
    ui_.urlValue = createValueLabel();
    ui_.fingerprintValue = createValueLabel();
    ui_.formatCountValue = createValueLabel();
    ui_.mimeBytesValue = createValueLabel();
    ui_.textLengthValue = createValueLabel();
    ui_.urlCountValue = createValueLabel();

    auto *metaCard = new QFrame(overviewTab);
    metaCard->setObjectName("metaCard");
    auto *metaGrid = new QGridLayout(metaCard);
    metaGrid->setContentsMargins(14, 12, 14, 12);
    metaGrid->setHorizontalSpacing(12);
    metaGrid->setVerticalSpacing(6);
    metaGrid->setColumnStretch(0, 1);
    metaGrid->setColumnStretch(1, 1);

    int row = 0;
    metaGrid->addWidget(createSectionLabel(QStringLiteral("Type"), zh(u"类型")), row, 0);
    metaGrid->addWidget(createSectionLabel(QStringLiteral("Time"), zh(u"时间")), row, 1);
    ++row;
    metaGrid->addWidget(ui_.typeValue, row, 0);
    metaGrid->addWidget(ui_.timeValue, row, 1);
    ++row;

    metaGrid->addWidget(createSectionLabel(QStringLiteral("Sequence"), zh(u"序号")), row, 0);
    metaGrid->addWidget(createSectionLabel(QStringLiteral("Record Name"), zh(u"记录名")), row, 1);
    ++row;
    metaGrid->addWidget(ui_.sequenceValue, row, 0);
    metaGrid->addWidget(ui_.nameValue, row, 1);
    ++row;

    metaGrid->addWidget(createSectionLabel(QStringLiteral("Formats"), zh(u"格式数")), row, 0);
    metaGrid->addWidget(createSectionLabel(QStringLiteral("MIME Bytes"), zh(u"MIME 大小")), row, 1);
    ++row;
    metaGrid->addWidget(ui_.formatCountValue, row, 0);
    metaGrid->addWidget(ui_.mimeBytesValue, row, 1);
    ++row;

    metaGrid->addWidget(createSectionLabel(QStringLiteral("URL Count"), zh(u"链接数量")), row, 0);
    metaGrid->addWidget(createSectionLabel(QStringLiteral("Text Length"), zh(u"文本长度")), row, 1);
    ++row;
    metaGrid->addWidget(ui_.urlCountValue, row, 0);
    metaGrid->addWidget(ui_.textLengthValue, row, 1);
    ++row;

    metaGrid->addWidget(createSectionLabel(QStringLiteral("Title"), zh(u"标题")), row, 0, 1, 2);
    ++row;
    metaGrid->addWidget(ui_.titleValue, row, 0, 1, 2);
    ++row;

    metaGrid->addWidget(createSectionLabel(QStringLiteral("URL"), zh(u"链接")), row, 0, 1, 2);
    ++row;
    metaGrid->addWidget(ui_.urlValue, row, 0, 1, 2);
    ++row;

    metaGrid->addWidget(createSectionLabel(QStringLiteral("Fingerprint"), zh(u"指纹")), row, 0, 1, 2);
    ++row;
    metaGrid->addWidget(ui_.fingerprintValue, row, 0, 1, 2);
    overviewTopLayout->addWidget(metaCard, 1);
    overviewLayout->addLayout(overviewTopLayout);
    ui_.tabs->addTab(overviewTab, uiText(QStringLiteral("Overview"), zh(u"概览")));

    auto *normalizedTab = new QWidget(ui_.tabs);
    auto *normalizedLayout = new QVBoxLayout(normalizedTab);
    normalizedLayout->setContentsMargins(14, 14, 14, 14);
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
    rawLayout->setContentsMargins(14, 14, 14, 14);
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
    mimeLayout->setContentsMargins(14, 14, 14, 14);
    mimeLayout->setSpacing(10);
    ui_.mimeFormatsEdit = createReadOnlyEditor(true);
    ui_.mimeFormatsEdit->setMinimumHeight(360);
    mimeLayout->addWidget(createSectionLabel(QStringLiteral("MIME Formats and Sizes"), zh(u"MIME 格式与大小")));
    mimeLayout->addWidget(ui_.mimeFormatsEdit, 1);
    ui_.tabs->addTab(mimeTab, uiText(QStringLiteral("MIME"), zh(u"MIME")));
}

void ClipboardItemDetailsDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal borderWidth = 3.0;
    const qreal radius = 8.0;
    const QRectF outerRect = QRectF(rect()).adjusted(borderWidth / 2.0,
                                                     borderWidth / 2.0,
                                                     -borderWidth / 2.0,
                                                     -borderWidth / 2.0);

    QConicalGradient gradient(outerRect.center(), 135);
    gradient.setColorAt(0.00, QColor("#4A90E2"));
    gradient.setColorAt(0.25, QColor("#1abc9c"));
    gradient.setColorAt(0.50, QColor("#fc9867"));
    gradient.setColorAt(0.75, QColor("#9B59B6"));
    gradient.setColorAt(1.00, QColor("#4A90E2"));

    painter.setPen(QPen(QBrush(gradient), borderWidth));
    painter.setBrush(darkTheme_ ? QColor(26, 31, 38, 1) : QColor(247, 250, 255, 1));
    painter.drawRoundedRect(outerRect, radius, radius);
}

void ClipboardItemDetailsDialog::applyTheme(bool dark) {
    darkTheme_ = dark;
    WindowBlurHelper::enableBlurBehind(this, darkTheme_);
    if (ui_.card) {
        ui_.card->setStyleSheet(detailsStyleSheet(darkTheme_));
    } else {
        setStyleSheet(detailsStyleSheet(darkTheme_));
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

    if (!previewPixmap.isNull()) {
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

    ui_.titleLabel->setText(uiText(QStringLiteral("Clipboard Item Details"), zh(u"剪贴板条目详情")));
    if (sequence > 0) {
        ui_.sequenceValue->setText(totalCount > 0
            ? QStringLiteral("%1 / %2").arg(sequence).arg(totalCount)
            : QString::number(sequence));
    } else {
        ui_.sequenceValue->setText(dashText());
    }
    ui_.typeValue->setText(contentTypeLabel(item.getContentType()));
    ui_.timeValue->setText(QLocale::system().toString(item.getTime(), QLocale::LongFormat));
    ui_.nameValue->setText(item.getName().isEmpty() ? dashText() : item.getName());
    ui_.titleValue->setText(title.isEmpty() ? dashText() : title);
    ui_.urlValue->setText(url.isEmpty() ? dashText() : url);
    ui_.fingerprintValue->setText(fingerprint.isEmpty() ? dashText() : QString::fromLatin1(fingerprint.toHex()));
    ui_.formatCountValue->setText(mimeData ? QString::number(mimeData->formats().size()) : QStringLiteral("0"));
    ui_.mimeBytesValue->setText(byteCountLabel(totalMimeBytes(mimeData)));
    ui_.textLengthValue->setText(QString::number(normalizedText.size()));
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
