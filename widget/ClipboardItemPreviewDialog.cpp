// input: Depends on ClipboardItemPreviewDialog.h, Qt widgets/layout, and ClipboardItem payload accessors.
// output: Implements a centered read-only preview dialog with selection/copy support for rich text, text, images, and files.
// pos: Widget-layer preview dialog implementation for larger clipboard inspection.
// update: If I change, update this header block and my folder README.md (tuned preview font size + hidden caret/focus).
#include "ClipboardItemPreviewDialog.h"

#include <QCursor>
#include <QBuffer>
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
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QPointer>

#include "data/LocalSaver.h"
#include "utils/ThemeManager.h"
namespace {
constexpr int kPreviewDialogWidth = 980;
constexpr int kPreviewDialogHeight = 760;
constexpr int kPreviewBodyFontSize = 16;

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

QString unavailableText() {
    return QStringLiteral("该条目暂无可用预览");
}

QImage loadPreviewImageFromBytes(const QByteArray &imageBytes, const QSize &targetSize, qreal devicePixelRatio) {
    if (imageBytes.isEmpty()) {
        return {};
    }

    QBuffer buffer;
    buffer.setData(imageBytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QImageReader reader(&buffer);
    QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }

    if (targetSize.isValid()) {
        const QSize pixelTargetSize = targetSize * devicePixelRatio;
        if (pixelTargetSize.isValid()) {
            image = image.scaled(pixelTargetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    if (devicePixelRatio > 0.0) {
        image.setDevicePixelRatio(devicePixelRatio);
    }
    return image;
}

QImage loadPreviewImageFileBytes(const QString &filePath, const QSize &targetSize, qreal devicePixelRatio) {
    if (!targetSize.isValid()) {
        return {};
    }

    QImageReader reader(filePath);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        const QSize pixelTargetSize = targetSize * devicePixelRatio;
        if (pixelTargetSize.isValid()) {
            const QSize scaledSize = sourceSize.scaled(pixelTargetSize, Qt::KeepAspectRatio);
            if (scaledSize.isValid()) {
                reader.setScaledSize(scaledSize);
            }
        }
    }

    QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }

    if (devicePixelRatio > 0.0) {
        image.setDevicePixelRatio(devicePixelRatio);
    }
    return image;
}

enum class PreviewKind {
    PlainText,
    Html,
    Image,
    Unavailable
};

struct PreviewPayload {
    PreviewKind kind = PreviewKind::Unavailable;
    QString text;
    QString html;
    QImage image;
    QString imageUrl;
};

QString previewStyleSheet(bool dark) {
    if (dark) {
        return QStringLiteral(R"(
            QFrame#previewCard {
                background-color: rgba(26, 31, 38, 245);
                border: none;
                border-radius: 18px;
            }
            QLabel#previewTitle {
                color: #E6EDF5;
                font-size: 22px;
                font-weight: 700;
                background: transparent;
            }
            QLabel#previewSubtitle {
                color: #9AA7B5;
                font-size: 14px;
                background: transparent;
            }
            QTextBrowser {
                background-color: #1F242D;
                border: 1px solid rgba(116, 154, 214, 40);
                border-radius: 14px;
                padding: 12px;
                color: #E6EDF5;
                font-size: 16px;
                selection-background-color: rgba(74, 144, 226, 110);
            }
            QTextBrowser:focus {
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
        )");
    }

    return QStringLiteral(R"(
        QFrame#previewCard {
            background-color: rgba(247, 250, 255, 245);
            border: none;
            border-radius: 18px;
        }
        QLabel#previewTitle {
            color: #1E2936;
            font-size: 22px;
            font-weight: 700;
            background: transparent;
        }
        QLabel#previewSubtitle {
            color: #5E7084;
            font-size: 14px;
            background: transparent;
        }
        QTextBrowser {
            background-color: #FFFFFF;
            border: 1px solid rgba(74, 144, 226, 18);
            border-radius: 14px;
            padding: 12px;
            color: #1E2936;
            font-size: 16px;
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
    )");
}

PreviewPayload buildPreviewPayload(ClipboardItem::ContentType contentType,
                                   const QString &normalizedText,
                                   const QList<QUrl> &normalizedUrls,
                                   const QString &html,
                                   const QByteArray &imageBytes,
                                   const QString &filePath,
                                   const QSize &targetSize,
                                   qreal devicePixelRatio) {
    PreviewPayload payload;

    switch (contentType) {
        case ClipboardItem::Text:
        case ClipboardItem::Link: {
            payload.kind = PreviewKind::PlainText;
            payload.text = normalizedText.isEmpty() ? unavailableText() : normalizedText;
            break;
        }
        case ClipboardItem::Image: {
            QImage image = loadPreviewImageFromBytes(imageBytes, targetSize, devicePixelRatio);
            if (!image.isNull()) {
                payload.kind = PreviewKind::Image;
                payload.image = image;
                payload.imageUrl = QStringLiteral("preview-image://clipboard-item");
            } else {
                payload.kind = PreviewKind::PlainText;
                payload.text = unavailableText();
            }
            break;
        }
        case ClipboardItem::File: {
            if (!filePath.isEmpty() && isLocalImageFile(filePath)) {
                QImage image = loadPreviewImageFileBytes(filePath, targetSize, devicePixelRatio);
                if (!image.isNull()) {
                    payload.kind = PreviewKind::Image;
                    payload.image = image;
                    payload.imageUrl = QStringLiteral("preview-image://local-file");
                    break;
                }
            }
            payload.kind = PreviewKind::PlainText;
            payload.text = normalizedUrls.isEmpty() ? unavailableText() : joinUrls(normalizedUrls);
            break;
        }
        case ClipboardItem::RichText: {
            if (!html.isEmpty()) {
                payload.kind = PreviewKind::Html;
                payload.html = html;
                const QString imageSource = firstHtmlImageSource(html);
                if (!imageSource.isEmpty() && !imageBytes.isEmpty()) {
                    QImage image = loadPreviewImageFromBytes(imageBytes, targetSize, devicePixelRatio);
                    if (!image.isNull()) {
                        payload.image = image;
                        payload.imageUrl = imageSource;
                    }
                }
            } else {
                payload.kind = PreviewKind::PlainText;
                payload.text = normalizedText.isEmpty() ? unavailableText() : normalizedText;
            }
            break;
        }
        case ClipboardItem::Color:
        case ClipboardItem::All:
            payload.kind = PreviewKind::PlainText;
            payload.text = unavailableText();
            break;
    }

    return payload;
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
            font-size: 22px;
            font-weight: 700;
            background: transparent;
        }
        QLabel#previewSubtitle {
            color: #5E7084;
            font-size: 14px;
            background: transparent;
        }
        QTextBrowser {
            background-color: #FFFFFF;
            border: 1px solid rgba(74, 144, 226, 18);
            border-radius: 14px;
            padding: 12px;
            color: #1E2936;
            font-size: 16px;
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
    QFont previewFont = ui_.browser->font();
    previewFont.setPointSize(kPreviewBodyFontSize);
    ui_.browser->setFont(previewFont);
    ui_.browser->document()->setDefaultFont(previewFont);
    ui_.browser->setCursorWidth(0);
    ui_.browser->setFocusPolicy(Qt::NoFocus);
    ui_.browser->setOpenLinks(false);
    ui_.browser->setOpenExternalLinks(false);
    ui_.browser->setReadOnly(true);
    ui_.browser->setUndoRedoEnabled(false);
    ui_.browser->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui_.browser->setContextMenuPolicy(Qt::DefaultContextMenu);
    ui_.browser->installEventFilter(this);
    ui_.browser->viewport()->installEventFilter(this);
    ui_.browser->viewport()->setCursor(Qt::ArrowCursor);
    cardLayout->addWidget(ui_.browser, 1);

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ClipboardItemPreviewDialog::applyTheme);
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
    releasePreviewContent();

    const ClipboardItem::ContentType contentType = item.getContentType();

    ui_.titleLabel->setText(uiText(QStringLiteral("Clipboard Preview"), QStringLiteral("剪贴板预览")));
    ui_.subtitleLabel->setText(item.getName().isEmpty()
        ? uiText(QStringLiteral("Read-only preview"), QStringLiteral("只读预览"))
        : QStringLiteral("%1 | %2")
              .arg(uiText(QStringLiteral("Read-only preview"), QStringLiteral("只读预览")))
              .arg(item.getName()));

    ui_.browser->clear();
    ui_.browser->document()->clear();
    ui_.browser->setPlainText(uiText(QStringLiteral("Loading preview..."), QStringLiteral("正在加载预览...")));

    const QString normalizedText = item.getNormalizedText();
    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();
    const QString html = contentType == ClipboardItem::RichText ? item.getHtml() : QString();
    const QByteArray imageBytes = (contentType == ClipboardItem::Image || contentType == ClipboardItem::RichText)
        ? item.imagePayloadBytesFast()
        : QByteArray();
    QString filePath;
    if (contentType == ClipboardItem::File && normalizedUrls.size() == 1 && normalizedUrls.first().isLocalFile()) {
        filePath = normalizedUrls.first().toLocalFile();
    }
    const QString sourceFilePath = item.sourceFilePath();
    const quint64 mimeOffset = item.mimeDataFileOffset();
    const QSize targetSize(kPreviewDialogWidth - 120, kPreviewDialogHeight - 180);
    const qreal dpr = devicePixelRatioF();
    const quint64 token = ++previewToken_;

    QPointer<ClipboardItemPreviewDialog> guard(this);
    QThread *thread = QThread::create([guard, contentType, normalizedText, normalizedUrls, html, imageBytes, filePath, sourceFilePath, mimeOffset, targetSize, dpr, token]() mutable {
        QString resolvedHtml = html;
        QByteArray resolvedImageBytes = imageBytes;
        if ((resolvedHtml.isEmpty() || resolvedImageBytes.isEmpty())
            && !sourceFilePath.isEmpty()
            && (contentType == ClipboardItem::Image || contentType == ClipboardItem::RichText)) {
            QString htmlPayload;
            QByteArray imagePayload;
            LocalSaver::loadMimePayloads(sourceFilePath,
                                         mimeOffset,
                                         contentType == ClipboardItem::RichText ? &htmlPayload : nullptr,
                                         (contentType == ClipboardItem::Image || contentType == ClipboardItem::RichText) ? &imagePayload : nullptr);
            if (resolvedHtml.isEmpty()) {
                resolvedHtml = htmlPayload;
            }
            if (resolvedImageBytes.isEmpty()) {
                resolvedImageBytes = imagePayload;
            }
        }

        PreviewPayload payload = buildPreviewPayload(contentType,
                                                     normalizedText,
                                                     normalizedUrls,
                                                     resolvedHtml,
                                                     resolvedImageBytes,
                                                     filePath,
                                                     targetSize,
                                                     dpr);
        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, payload, token]() {
                if (!guard || guard->previewToken_ != token) {
                    return;
                }
                if (!guard->ui_.browser) {
                    return;
                }

                guard->ui_.browser->clear();
                guard->ui_.browser->document()->clear();

                switch (payload.kind) {
                    case PreviewKind::Image: {
                        const QString imageUrl = payload.imageUrl.isEmpty()
                            ? QStringLiteral("preview-image://clipboard-item")
                            : payload.imageUrl;
                        guard->ui_.browser->document()->addResource(QTextDocument::ImageResource,
                                                                    QUrl(imageUrl),
                                                                    payload.image);
                        guard->ui_.browser->setHtml(QStringLiteral("<div style=\"text-align:center;\"><img src=\"%1\"></div>")
                                                       .arg(imageUrl));
                        break;
                    }
                    case PreviewKind::Html: {
                        if (!payload.imageUrl.isEmpty() && !payload.image.isNull()) {
                            guard->ui_.browser->document()->addResource(QTextDocument::ImageResource,
                                                                        QUrl(payload.imageUrl),
                                                                        payload.image);
                        }
                        guard->ui_.browser->setHtml(payload.html);
                        break;
                    }
                    case PreviewKind::PlainText: {
                        guard->ui_.browser->setPlainText(payload.text.isEmpty() ? unavailableText() : payload.text);
                        break;
                    }
                    case PreviewKind::Unavailable:
                        guard->ui_.browser->setPlainText(unavailableText());
                        break;
                }
            }, Qt::QueuedConnection);
        }
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();

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
}

void ClipboardItemPreviewDialog::reject() {
    QDialog::reject();
}

void ClipboardItemPreviewDialog::applyTheme(bool dark) {
    darkTheme_ = dark;
    if (ui_.browser) {
        ui_.browser->setStyleSheet(QString());
    }
    if (QWidget *card = findChild<QWidget*>(QStringLiteral("previewCard"))) {
        card->setStyleSheet(previewStyleSheet(darkTheme_));
    } else {
        setStyleSheet(previewStyleSheet(darkTheme_));
    }
    update();
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
    painter.setBrush(darkTheme_ ? QColor(26, 31, 38, 245) : QColor(247, 250, 255, 245));
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

void ClipboardItemPreviewDialog::releasePreviewContent() {
    if (!ui_.browser) {
        return;
    }

    QPointer<QTextDocument> oldDocument = ui_.browser->document();
    auto *newDocument = new QTextDocument(ui_.browser);
    ui_.browser->setDocument(newDocument);
    ui_.browser->document()->setDefaultFont(ui_.browser->font());
    if (oldDocument && oldDocument->parent() == ui_.browser) {
        oldDocument->deleteLater();
    }
}
