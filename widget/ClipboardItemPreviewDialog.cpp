// input: Depends on ClipboardItemPreviewDialog.h, Qt widgets/layout, and ClipboardItem payload accessors.
// output: Implements a centered read-only preview dialog with selection/copy support for rich text, text, images, and files.
// pos: Widget-layer preview dialog implementation for larger clipboard inspection.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardItemPreviewDialog.h"

#include <QCursor>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace {
constexpr int kPreviewDialogWidth = 980;
constexpr int kPreviewDialogHeight = 760;

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

QString joinUrls(const QList<QUrl> &urls) {
    QStringList lines;
    for (const QUrl &url : urls) {
        lines << (url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded));
    }
    return lines.join(QStringLiteral("\n"));
}

QString firstHtmlImageSource(const QString &html) {
    static const QRegularExpression srcRegex(
        QStringLiteral(R"(<img[^>]+src\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = srcRegex.match(html);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

bool isLocalImageFile(const QString &filePath) {
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    QMimeDatabase mimeDatabase;
    return mimeDatabase.mimeTypeForFile(info).name().startsWith(QStringLiteral("image/"));
}

QPixmap loadPreviewImageFile(const QString &filePath, const QSize &targetSize, qreal devicePixelRatio) {
    if (!targetSize.isValid()) {
        return {};
    }

    QImageReader reader(filePath);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid()) {
        return {};
    }

    const QSize pixelTargetSize = targetSize * devicePixelRatio;
    if (pixelTargetSize.isValid()) {
        const QSize scaledSize = sourceSize.scaled(pixelTargetSize, Qt::KeepAspectRatio);
        if (scaledSize.isValid()) {
            reader.setScaledSize(scaledSize);
        }
    }

    const QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return pixmap;
}

QString unavailableText() {
    return QStringLiteral("该条目暂无可用预览");
}
}

ClipboardItemPreviewDialog::ClipboardItemPreviewDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    setModal(false);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(kPreviewDialogWidth, kPreviewDialogHeight);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(3, 3, 3, 3);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("previewCard"));
    card->setStyleSheet(QStringLiteral(R"(
        QFrame#previewCard {
            background-color: rgba(247, 250, 255, 245);
            border: none;
            border-radius: 18px;
        }
        QLabel#previewTitle {
            color: #1E2936;
            font-size: 19px;
            font-weight: 700;
            background: transparent;
        }
        QLabel#previewSubtitle {
            color: #5E7084;
            font-size: 12px;
            background: transparent;
        }
        QTextBrowser {
            background-color: #FFFFFF;
            border: 1px solid rgba(74, 144, 226, 18);
            border-radius: 14px;
            padding: 12px;
            color: #1E2936;
            selection-background-color: rgba(74, 144, 226, 76);
        }
        QTextBrowser:focus {
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
    )"));
    rootLayout->addWidget(card);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 16);
    cardLayout->setSpacing(12);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);

    auto *titleLayout = new QVBoxLayout;
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(2);

    ui_.titleLabel = new QLabel(card);
    ui_.titleLabel->setObjectName(QStringLiteral("previewTitle"));
    titleLayout->addWidget(ui_.titleLabel);

    ui_.subtitleLabel = new QLabel(card);
    ui_.subtitleLabel->setObjectName(QStringLiteral("previewSubtitle"));
    ui_.subtitleLabel->setWordWrap(true);
    titleLayout->addWidget(ui_.subtitleLabel);

    ui_.closeButton = new QToolButton(card);
    ui_.closeButton->setObjectName(QStringLiteral("closeButton"));
    ui_.closeButton->setText(QStringLiteral("×"));
    ui_.closeButton->setCursor(Qt::PointingHandCursor);
    connect(ui_.closeButton, &QToolButton::clicked, this, &ClipboardItemPreviewDialog::reject);

    headerLayout->addLayout(titleLayout, 1);
    headerLayout->addWidget(ui_.closeButton, 0, Qt::AlignTop);
    cardLayout->addLayout(headerLayout);

    ui_.browser = new QTextBrowser(card);
    ui_.browser->setOpenLinks(false);
    ui_.browser->setOpenExternalLinks(false);
    ui_.browser->setReadOnly(true);
    ui_.browser->setUndoRedoEnabled(false);
    ui_.browser->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    ui_.browser->setContextMenuPolicy(Qt::DefaultContextMenu);
    ui_.browser->installEventFilter(this);
    ui_.browser->viewport()->installEventFilter(this);
    cardLayout->addWidget(ui_.browser, 1);
}

bool ClipboardItemPreviewDialog::supportsPreview(const ClipboardItem &item) {
    switch (item.getContentType()) {
        case ClipboardItem::Text:
        case ClipboardItem::Link:
        case ClipboardItem::RichText:
        case ClipboardItem::Image:
            return true;
        case ClipboardItem::File:
            return !item.getNormalizedUrls().isEmpty();
        default:
            return false;
    }
}

void ClipboardItemPreviewDialog::showItem(const ClipboardItem &item) {
    updatePreviewContent(item);

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
    raise();
    activateWindow();
    ui_.browser->setFocus();
}

void ClipboardItemPreviewDialog::reject() {
    releasePreviewContent();
    QDialog::reject();
}

bool ClipboardItemPreviewDialog::eventFilter(QObject *watched, QEvent *event) {
    if ((watched == ui_.browser || watched == ui_.browser->viewport())
        && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->modifiers() == Qt::NoModifier
            && (keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_Escape)) {
            reject();
            keyEvent->accept();
            return true;
        }
    }

    return QDialog::eventFilter(watched, event);
}

void ClipboardItemPreviewDialog::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers() == Qt::NoModifier
        && (event->key() == Qt::Key_Space || event->key() == Qt::Key_Escape)) {
        reject();
        event->accept();
        return;
    }

    QDialog::keyPressEvent(event);
}

void ClipboardItemPreviewDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal borderWidth = 3.0;
    const qreal radius = 18.0;
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
    painter.setBrush(QColor(247, 250, 255, 245));
    painter.drawRoundedRect(outerRect, radius, radius);
}

void ClipboardItemPreviewDialog::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
    QDialog::mousePressEvent(event);
}

void ClipboardItemPreviewDialog::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - dragOffset_);
        event->accept();
    }
    QDialog::mouseMoveEvent(event);
}

QString ClipboardItemPreviewDialog::uiText(const QString &source, const QString &zhFallback) const {
    const QString translated = tr(source.toUtf8().constData());
    if (translated == source || looksBrokenTranslation(translated)) {
        return preferChineseFallback() ? zhFallback : source;
    }
    return translated;
}

void ClipboardItemPreviewDialog::updatePreviewContent(const ClipboardItem &item) {
    const_cast<ClipboardItem &>(item).ensureMimeDataLoaded();
    const QMimeData *mimeData = item.getMimeData();
    const QString html = mimeData ? mimeData->html() : QString();
    const QString normalizedText = item.getNormalizedText();
    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();

    ui_.titleLabel->setText(uiText(QStringLiteral("Clipboard Preview"), QStringLiteral("剪贴板预览")));
    ui_.subtitleLabel->setText(item.getName().isEmpty()
        ? uiText(QStringLiteral("Read-only preview"), QStringLiteral("只读预览"))
        : QStringLiteral("%1 | %2")
              .arg(uiText(QStringLiteral("Read-only preview"), QStringLiteral("只读预览")))
              .arg(item.getName()));

    ui_.browser->clear();
    ui_.browser->document()->clear();

    if (item.getContentType() == ClipboardItem::Text || item.getContentType() == ClipboardItem::Link) {
        ui_.browser->setPlainText(normalizedText.isEmpty() ? unavailableText() : normalizedText);
        return;
    }

    if (item.getContentType() == ClipboardItem::Image) {
        const QPixmap pixmap = item.getImage();
        if (!pixmap.isNull()) {
            const QString imageUrl = QStringLiteral("preview-image://clipboard-item");
            ui_.browser->document()->addResource(QTextDocument::ImageResource, QUrl(imageUrl), pixmap.toImage());
            ui_.browser->setHtml(QStringLiteral("<div style=\"text-align:center;\"><img src=\"%1\"></div>").arg(imageUrl));
            return;
        }
    }

    if (item.getContentType() == ClipboardItem::File) {
        if (normalizedUrls.size() == 1 && normalizedUrls.first().isLocalFile()) {
            const QString filePath = normalizedUrls.first().toLocalFile();
            if (isLocalImageFile(filePath)) {
                const QPixmap pixmap = loadPreviewImageFile(
                    filePath,
                    QSize(kPreviewDialogWidth - 120, kPreviewDialogHeight - 180),
                    devicePixelRatioF());
                if (!pixmap.isNull()) {
                    const QString imageUrl = QStringLiteral("preview-image://local-file");
                    ui_.browser->document()->addResource(QTextDocument::ImageResource, QUrl(imageUrl), pixmap.toImage());
                    ui_.browser->setHtml(QStringLiteral("<div style=\"text-align:center;\"><img src=\"%1\"></div>").arg(imageUrl));
                    return;
                }
            }
        }

        const QString pathsText = joinUrls(normalizedUrls);
        ui_.browser->setPlainText(pathsText.isEmpty() ? unavailableText() : pathsText);
        return;
    }

    if (!html.isEmpty()) {
        const QString imageSource = firstHtmlImageSource(html);
        const QByteArray imageBytes = item.imagePayloadBytesFast();
        if (!imageSource.isEmpty() && !imageBytes.isEmpty()) {
            QImage image;
            if (image.loadFromData(imageBytes)) {
                ui_.browser->document()->addResource(QTextDocument::ImageResource, QUrl(imageSource), image);
            }
        }
        ui_.browser->setHtml(html);
        return;
    }

    ui_.browser->setPlainText(unavailableText());
}

void ClipboardItemPreviewDialog::releasePreviewContent() {
    if (!ui_.browser) {
        return;
    }

    auto *oldDocument = ui_.browser->document();
    auto *newDocument = new QTextDocument(ui_.browser);
    ui_.browser->setDocument(newDocument);
    if (oldDocument && oldDocument->parent() == ui_.browser) {
        oldDocument->deleteLater();
    }
}
