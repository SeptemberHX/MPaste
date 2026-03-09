// input: Depends on ClipboardItemDetailsDialog.h, Qt form widgets, and ClipboardItem metadata accessors.
// output: Implements a polished clipboard item inspector dialog aligned with the app's rounded glass-card language.
// pos: Widget-layer details dialog implementation for clipboard debugging and transparency.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardItemDetailsDialog.h"

#include <QApplication>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFontDatabase>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
bool looksBrokenTranslation(const QString &text) {
    if (text.isEmpty()) {
        return true;
    }

    int suspiciousCount = 0;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('?') || ch == QChar::ReplacementCharacter) {
            ++suspiciousCount;
        }
    }
    return suspiciousCount >= qMax(2, text.size() / 2);
}

bool preferChineseFallback() {
    const QLocale locale = QLocale::system();
    return locale.language() == QLocale::Chinese
           || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
}

QString zh(const char16_t *text) {
    return QString::fromUtf16(text);
}

QString dashText() {
    return QString::fromUtf16(u"\u2014");
}

QFont preferredEditorFont() {
    QFont font = QApplication::font();
    font.setPointSize(10);

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
}

ClipboardItemDetailsDialog::ClipboardItemDetailsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    setModal(false);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(760, 640);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 14, 14, 14);

    ui_.card = new QFrame(this);
    ui_.card->setObjectName("detailsCard");
    ui_.card->setStyleSheet(QStringLiteral(R"(
        QFrame#detailsCard {
            background-color: rgba(249, 251, 255, 244);
            border: 1px solid rgba(74, 144, 226, 26);
            border-radius: 18px;
        }
        QFrame#heroBar {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 rgba(91, 162, 239, 176),
                                        stop:1 rgba(91, 162, 239, 82));
            border: none;
            border-top-left-radius: 18px;
            border-top-right-radius: 18px;
            min-height: 5px;
            max-height: 5px;
        }
        QLabel#detailsTitle {
            color: #1E2936;
            font-size: 18px;
            font-weight: 700;
            background: transparent;
        }
        QLabel#sectionLabel {
            color: #6A7A8C;
            font-size: 11px;
            font-weight: 700;
            background: transparent;
            letter-spacing: 0.4px;
            padding-bottom: 2px;
        }
        QLabel#valueLabel {
            color: #1E2936;
            font-size: 13px;
            background: transparent;
        }
        QFrame#metaCard {
            background-color: rgba(255, 255, 255, 146);
            border: 1px solid rgba(74, 144, 226, 18);
            border-radius: 14px;
        }
        QTextEdit {
            background-color: rgba(255, 255, 255, 168);
            border: 1px solid rgba(74, 144, 226, 18);
            border-radius: 12px;
            padding: 10px 12px;
            color: #1E2936;
            selection-background-color: rgba(74, 144, 226, 76);
        }
        QTextEdit:focus {
            border-color: rgba(74, 144, 226, 42);
        }
        QToolButton#closeButton {
            background-color: rgba(255, 255, 255, 186);
            border: 1px solid rgba(74, 144, 226, 22);
            border-radius: 14px;
            color: #5E7084;
            font-size: 16px;
            font-weight: 700;
            min-width: 28px;
            min-height: 28px;
        }
        QToolButton#closeButton:hover {
            background-color: rgba(255, 255, 255, 224);
            border-color: rgba(74, 144, 226, 46);
        }
    )"));

    auto *shadow = new QGraphicsDropShadowEffect(ui_.card);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(18, 31, 48, 34));
    ui_.card->setGraphicsEffect(shadow);

    rootLayout->addWidget(ui_.card);

    auto *cardLayout = new QVBoxLayout(ui_.card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    auto *heroBar = new QFrame(ui_.card);
    heroBar->setObjectName("heroBar");
    cardLayout->addWidget(heroBar);

    auto *contentLayout = new QVBoxLayout;
    contentLayout->setContentsMargins(20, 18, 20, 20);
    contentLayout->setSpacing(14);
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
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };

    auto createSectionLabel = [this](const QString &source, const QString &zhFallback) {
        auto *label = new QLabel(uiText(source, zhFallback), ui_.card);
        label->setObjectName("sectionLabel");
        return label;
    };

    ui_.typeValue = createValueLabel();
    ui_.timeValue = createValueLabel();
    ui_.titleValue = createValueLabel();
    ui_.urlValue = createValueLabel();

    auto *metaCard = new QFrame(ui_.card);
    metaCard->setObjectName("metaCard");
    auto *metaGrid = new QGridLayout(metaCard);
    metaGrid->setContentsMargins(14, 12, 14, 12);
    metaGrid->setHorizontalSpacing(14);
    metaGrid->setVerticalSpacing(8);
    metaGrid->addWidget(createSectionLabel(QStringLiteral("Type"), zh(u"类型")), 0, 0);
    metaGrid->addWidget(createSectionLabel(QStringLiteral("Time"), zh(u"时间")), 0, 1);
    metaGrid->addWidget(ui_.typeValue, 1, 0);
    metaGrid->addWidget(ui_.timeValue, 1, 1);
    metaGrid->addWidget(createSectionLabel(QStringLiteral("Title"), zh(u"标题")), 2, 0, 1, 2);
    metaGrid->addWidget(ui_.titleValue, 3, 0, 1, 2);
    metaGrid->addWidget(createSectionLabel(QStringLiteral("URL"), zh(u"链接")), 4, 0, 1, 2);
    metaGrid->addWidget(ui_.urlValue, 5, 0, 1, 2);
    contentLayout->addWidget(metaCard);

    ui_.normalizedTextEdit = createReadOnlyEditor();
    ui_.normalizedTextEdit->setMinimumHeight(124);
    ui_.normalizedUrlsEdit = createReadOnlyEditor();
    ui_.normalizedUrlsEdit->setMinimumHeight(96);
    ui_.mimeFormatsEdit = createReadOnlyEditor();
    ui_.mimeFormatsEdit->setMinimumHeight(96);

    contentLayout->addWidget(createSectionLabel(QStringLiteral("Normalized Text"), zh(u"标准化文本")));
    contentLayout->addWidget(ui_.normalizedTextEdit, 1);
    contentLayout->addWidget(createSectionLabel(QStringLiteral("Normalized URLs"), zh(u"标准化链接")));
    contentLayout->addWidget(ui_.normalizedUrlsEdit, 1);
    contentLayout->addWidget(createSectionLabel(QStringLiteral("Raw MIME Formats"), zh(u"原始 MIME 格式")));
    contentLayout->addWidget(ui_.mimeFormatsEdit, 1);
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
    if (translated == source || looksBrokenTranslation(translated)) {
        return preferChineseFallback() ? zhFallback : source;
    }
    return translated;
}

QString ClipboardItemDetailsDialog::contentTypeLabel(ClipboardItem::ContentType type) const {
    switch (type) {
        case ClipboardItem::Text:
            return uiText(QStringLiteral("Text"), zh(u"文本"));
        case ClipboardItem::Link:
            return uiText(QStringLiteral("Link"), zh(u"链接"));
        case ClipboardItem::Image:
            return uiText(QStringLiteral("Image"), zh(u"图片"));
        case ClipboardItem::RichText:
            return uiText(QStringLiteral("Rich Text"), zh(u"富文本"));
        case ClipboardItem::File:
            return uiText(QStringLiteral("File"), zh(u"文件"));
        case ClipboardItem::Color:
            return uiText(QStringLiteral("Color"), zh(u"颜色"));
        case ClipboardItem::All:
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

QTextEdit *ClipboardItemDetailsDialog::createReadOnlyEditor() const {
    auto *edit = new QTextEdit;
    edit->setReadOnly(true);
    edit->setAcceptRichText(false);
    edit->setLineWrapMode(QTextEdit::NoWrap);
    edit->setFont(preferredEditorFont());
    return edit;
}

void ClipboardItemDetailsDialog::showItem(const ClipboardItem &item) {
    const QString title = item.getTitle().trimmed();
    const QString url = item.getUrl().trimmed();
    const QString normalizedText = item.getNormalizedText().trimmed();
    const QString normalizedUrls = joinUrls(item.getNormalizedUrls()).trimmed();

    QStringList formats;
    if (const QMimeData *mimeData = item.getMimeData()) {
        formats = mimeData->formats();
    }

    ui_.titleLabel->setText(uiText(QStringLiteral("Clipboard Item Details"), zh(u"剪贴板条目详情")));
    ui_.typeValue->setText(contentTypeLabel(item.getContentType()));
    ui_.timeValue->setText(QLocale::system().toString(item.getTime(), QLocale::LongFormat));
    ui_.titleValue->setText(title.isEmpty() ? dashText() : title);
    ui_.urlValue->setText(url.isEmpty() ? dashText() : url);
    ui_.normalizedTextEdit->setPlainText(normalizedText.isEmpty() ? dashText() : normalizedText);
    ui_.normalizedUrlsEdit->setPlainText(normalizedUrls.isEmpty() ? dashText() : normalizedUrls);
    ui_.mimeFormatsEdit->setPlainText(formats.isEmpty() ? dashText() : formats.join(QStringLiteral("\n")));

    if (QWidget *anchor = parentWidget()) {
        QWidget *window = anchor->window();
        const QRect target = window->geometry();
        move(target.center() - rect().center());
    }

    show();
    raise();
    activateWindow();
}
