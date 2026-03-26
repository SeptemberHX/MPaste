// input: Depends on ClipboardItemPreviewDialog.h, Qt widgets/layout, and ClipboardItem payload accessors.
// output: Implements a centered read-only preview dialog with selection/copy support for rich text, text, images, and files.
// pos: Widget-layer preview dialog implementation for larger clipboard inspection.
// update: If I change, update this header block and my folder README.md (tuned preview font size + hidden caret/focus).
// note: Adds fallback decoding for Qt serialized image payloads and image zoom interactions.
#include "ClipboardItemPreviewDialog.h"

#include <QCursor>
#include <QBuffer>
#include <cmath>
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
#include <QPalette>
#include <QTextBrowser>
#include <QTextDocument>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedLayout>

#include "data/ContentClassifier.h"
#include "data/LocalSaver.h"
#include "utils/ThemeManager.h"
namespace {
constexpr int kPreviewDialogWidth = 980;
constexpr int kPreviewDialogHeight = 760;
constexpr int kPreviewBodyFontSize = 16;
constexpr qreal kImageZoomMin = 0.1;
constexpr qreal kImageZoomMax = 8.0;
constexpr qreal kImageZoomStep = 1.15;

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
        image = ContentClassifier::decodeQtSerializedImage(imageBytes);
    }
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
    if (filePath.isEmpty()) {
        return {};
    }

    QImageReader reader(filePath);
    const QSize sourceSize = reader.size();
    if (targetSize.isValid() && sourceSize.isValid()) {
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

QImage scalePreviewImage(const QImage &image, const QSize &targetSize, qreal devicePixelRatio) {
    if (image.isNull()) {
        return {};
    }

    QImage scaled = image;
    if (targetSize.isValid()) {
        const QSize pixelTargetSize = targetSize * devicePixelRatio;
        if (pixelTargetSize.isValid()) {
            scaled = scaled.scaled(pixelTargetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    if (devicePixelRatio > 0.0) {
        scaled.setDevicePixelRatio(devicePixelRatio);
    }
    return scaled;
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
                                   const QImage &fallbackImage,
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
            QImage image = loadPreviewImageFromBytes(imageBytes, QSize(), devicePixelRatio);
            if (image.isNull() && !fallbackImage.isNull()) {
                image = scalePreviewImage(fallbackImage, QSize(), devicePixelRatio);
            }
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
        case ClipboardItem::Office: {
            // Prefer the full-resolution image from MIME data over the
            // low-res card thumbnail used as fallback.
            QImage image = loadPreviewImageFromBytes(imageBytes, QSize(), devicePixelRatio);
            if (image.isNull() && !fallbackImage.isNull()) {
                image = scalePreviewImage(fallbackImage, QSize(), devicePixelRatio);
            }
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
                QImage image = loadPreviewImageFileBytes(filePath, QSize(), devicePixelRatio);
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
                const QString imageSource = ContentClassifier::firstHtmlImageSource(html);
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
        default: {
            // Fallback for unknown/corrupt content types (e.g. items synced
            // from a remote machine): try image → text → unavailable.
            QImage image = loadPreviewImageFromBytes(imageBytes, QSize(), devicePixelRatio);
            if (image.isNull() && !fallbackImage.isNull()) {
                image = scalePreviewImage(fallbackImage, QSize(), devicePixelRatio);
            }
            if (!image.isNull()) {
                payload.kind = PreviewKind::Image;
                payload.image = image;
            } else if (!normalizedText.isEmpty()) {
                payload.kind = PreviewKind::PlainText;
                payload.text = normalizedText;
            } else if (!html.isEmpty()) {
                payload.kind = PreviewKind::Html;
                payload.html = html;
            } else {
                payload.kind = PreviewKind::PlainText;
                payload.text = unavailableText();
            }
            break;
        }
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

    ui_.imageLabel = new QLabel(card);
    ui_.imageLabel->setAlignment(Qt::AlignCenter);
    ui_.imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    ui_.imageLabel->setBackgroundRole(QPalette::Base);
    ui_.imageLabel->setFocusPolicy(Qt::NoFocus);

    ui_.imageScrollArea = new QScrollArea(card);
    ui_.imageScrollArea->setFrameShape(QFrame::NoFrame);
    ui_.imageScrollArea->setWidgetResizable(false);
    ui_.imageScrollArea->setAlignment(Qt::AlignCenter);
    ui_.imageScrollArea->setFocusPolicy(Qt::NoFocus);
    ui_.imageScrollArea->setWidget(ui_.imageLabel);
    ui_.imageScrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    if (ui_.imageScrollArea->viewport()) {
        ui_.imageScrollArea->viewport()->setAutoFillBackground(false);
        ui_.imageScrollArea->viewport()->setCursor(Qt::OpenHandCursor);
    }
    ui_.imageScrollArea->viewport()->installEventFilter(this);

    auto *contentHost = new QWidget(card);
    ui_.contentLayout = new QStackedLayout(contentHost);
    ui_.contentLayout->setContentsMargins(0, 0, 0, 0);
    ui_.contentLayout->addWidget(ui_.browser);
    ui_.contentLayout->addWidget(ui_.imageScrollArea);
    ui_.contentLayout->setCurrentWidget(ui_.browser);
    cardLayout->addWidget(contentHost, 1);

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ClipboardItemPreviewDialog::applyTheme);
}

bool ClipboardItemPreviewDialog::supportsPreview(const ClipboardItem &item) {
    switch (item.getContentType()) {
        case ClipboardItem::Text:
        case ClipboardItem::Link:
        case ClipboardItem::RichText:
        case ClipboardItem::Image:
        case ClipboardItem::Office:
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
    const QByteArray imageBytes = (contentType == ClipboardItem::Image
            || contentType == ClipboardItem::Office
            || contentType == ClipboardItem::RichText)
        ? item.imagePayloadBytesFast()
        : QByteArray();
    QImage fallbackImage;
    if (contentType == ClipboardItem::Office) {
        const QPixmap previewPixmap = item.getImage();
        if (!previewPixmap.isNull()) {
            fallbackImage = previewPixmap.toImage();
            fallbackImage.setDevicePixelRatio(previewPixmap.devicePixelRatio());
        } else if (item.hasThumbnail()) {
            fallbackImage = item.thumbnail().toImage();
        }
    } else if (contentType == ClipboardItem::Image && item.hasThumbnail()) {
        fallbackImage = item.thumbnail().toImage();
    }
    QString filePath;
    if (contentType == ClipboardItem::File && normalizedUrls.size() == 1 && normalizedUrls.first().isLocalFile()) {
        filePath = normalizedUrls.first().toLocalFile();
    }
    const QString sourceFilePath = item.sourceFilePath();
    const quint64 mimeOffset = item.mimeDataFileOffset();
    const bool preferFullItem = !sourceFilePath.isEmpty();
    const QSize targetSize(kPreviewDialogWidth - 120, kPreviewDialogHeight - 180);
    const qreal dpr = devicePixelRatioF();
    const quint64 token = ++previewToken_;

    QPointer<ClipboardItemPreviewDialog> guard(this);
    QThread *thread = QThread::create([guard, contentType, normalizedText, normalizedUrls, html, imageBytes, fallbackImage, filePath, sourceFilePath, mimeOffset, preferFullItem, targetSize, dpr, token]() mutable {
        ClipboardItem::ContentType resolvedType = contentType;
        QString resolvedText = normalizedText;
        QList<QUrl> resolvedUrls = normalizedUrls;
        QString resolvedHtml = html;
        QByteArray resolvedImageBytes = imageBytes;
        QImage resolvedFallbackImage = fallbackImage;
        QString resolvedFilePath = filePath;

        // Load only what each content type needs — avoid loading the full
        // ClipboardItem which pulls all MIME data into memory.
        if (preferFullItem && !sourceFilePath.isEmpty()) {
            LocalSaver saver;
            // Light load: metadata + text + urls, no MIME blobs.
            ClipboardItem lightItem = saver.loadFromFileLight(sourceFilePath, true);
            if (!lightItem.getName().isEmpty()) {
                resolvedType = lightItem.getContentType();
                resolvedText = lightItem.getNormalizedText();
                resolvedUrls = lightItem.getNormalizedUrls();
                if (resolvedType == ClipboardItem::File && resolvedUrls.size() == 1 && resolvedUrls.front().isLocalFile()) {
                    resolvedFilePath = resolvedUrls.front().toLocalFile();
                }
                if (lightItem.hasThumbnail()) {
                    resolvedFallbackImage = lightItem.thumbnail().toImage();
                }
            }

            // For types that need image/html payloads, read ONLY those
            // sections from disk — not the full MIME blob.
            // Also try for All/unknown types (e.g. items from remote sync).
            if (resolvedType == ClipboardItem::RichText
                || resolvedType == ClipboardItem::Image
                || resolvedType == ClipboardItem::Office
                || resolvedType == ClipboardItem::All) {
                QString htmlPayload;
                QByteArray imagePayload;
                LocalSaver::loadMimePayloads(sourceFilePath, mimeOffset,
                    resolvedType == ClipboardItem::RichText ? &htmlPayload : nullptr,
                    &imagePayload);
                if (!htmlPayload.isEmpty()) {
                    resolvedHtml = htmlPayload;
                }
                if (!imagePayload.isEmpty()) {
                    resolvedImageBytes = imagePayload;
                }
            } else if (resolvedType == ClipboardItem::Text
                       || resolvedType == ClipboardItem::Link) {
                // lightItem.getNormalizedText() already has the full cached
                // text from disk.  For rich HTML preview (e.g. RichText
                // fallback-to-text), load the HTML payload only.
                if (resolvedText.isEmpty() || resolvedType == ClipboardItem::Link) {
                    QString htmlPayload;
                    LocalSaver::loadMimePayloads(sourceFilePath, mimeOffset, &htmlPayload, nullptr);
                    if (!htmlPayload.isEmpty()) {
                        resolvedHtml = htmlPayload;
                    }
                }
            }
        } else if (!sourceFilePath.isEmpty()
                   && (resolvedType == ClipboardItem::Image
                       || resolvedType == ClipboardItem::Office
                       || resolvedType == ClipboardItem::RichText)) {
            // Fallback: load image/html payloads for items without
            // preferFullItem (e.g. freshly copied, not yet on disk).
            QString htmlPayload;
            QByteArray imagePayload;
            LocalSaver::loadMimePayloads(sourceFilePath, mimeOffset,
                resolvedType == ClipboardItem::RichText ? &htmlPayload : nullptr,
                &imagePayload);
            if (resolvedHtml.isEmpty() && !htmlPayload.isEmpty()) {
                resolvedHtml = htmlPayload;
            }
            if (resolvedImageBytes.isEmpty() && !imagePayload.isEmpty()) {
                resolvedImageBytes = imagePayload;
            }
        }

        qInfo().noquote() << QStringLiteral("[preview] type=%1 sourceFile=%2 mimeOffset=%3 imageBytes=%4 fallbackNull=%5 preferFull=%6")
            .arg(resolvedType)
            .arg(sourceFilePath.isEmpty() ? QStringLiteral("(empty)") : sourceFilePath)
            .arg(mimeOffset)
            .arg(resolvedImageBytes.size())
            .arg(resolvedFallbackImage.isNull())
            .arg(preferFullItem);

        PreviewPayload payload = buildPreviewPayload(resolvedType,
                                                     resolvedText,
                                                     resolvedUrls,
                                                     resolvedHtml,
                                                     resolvedImageBytes,
                                                     resolvedFallbackImage,
                                                     resolvedFilePath,
                                                     targetSize,
                                                     dpr);
        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, payload, token]() {
                if (!guard || guard->previewToken_ != token) {
                    return;
                }
                if (!guard->ui_.browser || !guard->ui_.contentLayout) {
                    return;
                }

                switch (payload.kind) {
                    case PreviewKind::Image: {
                        guard->setImagePreview(payload.image);
                        guard->ui_.contentLayout->setCurrentWidget(guard->ui_.imageScrollArea);
                        // Viewport size may not be valid until after the
                        // layout switch.  Reset zoom and re-fit after
                        // the layout settles with the correct viewport.
                        QTimer::singleShot(0, guard.data(), [guard]() {
                            if (!guard) return;
                            guard->imageZoomFactor_ = 1.0;
                            guard->updateImagePreview();
                        });
                        break;
                    }
                    case PreviewKind::Html: {
                        guard->ui_.browser->clear();
                        guard->ui_.browser->document()->clear();
                        if (!payload.imageUrl.isEmpty() && !payload.image.isNull()) {
                            guard->ui_.browser->document()->addResource(QTextDocument::ImageResource,
                                                                        QUrl(payload.imageUrl),
                                                                        payload.image);
                        }
                        guard->ui_.browser->setHtml(payload.html);
                        guard->ui_.contentLayout->setCurrentWidget(guard->ui_.browser);
                        break;
                    }
                    case PreviewKind::PlainText: {
                        guard->ui_.browser->clear();
                        guard->ui_.browser->document()->clear();
                        guard->ui_.browser->setPlainText(payload.text.isEmpty() ? unavailableText() : payload.text);
                        guard->ui_.contentLayout->setCurrentWidget(guard->ui_.browser);
                        break;
                    }
                    case PreviewKind::Unavailable:
                        guard->ui_.browser->clear();
                        guard->ui_.browser->document()->clear();
                        guard->ui_.browser->setPlainText(unavailableText());
                        guard->ui_.contentLayout->setCurrentWidget(guard->ui_.browser);
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

bool ClipboardItemPreviewDialog::isImagePreviewActive() const {
    return ui_.contentLayout && ui_.imageScrollArea
        && ui_.contentLayout->currentWidget() == ui_.imageScrollArea;
}

void ClipboardItemPreviewDialog::setImagePreview(const QImage &image) {
    if (!ui_.imageLabel || !ui_.imageScrollArea) {
        return;
    }

    imageOriginal_ = image;
    if (!imageOriginal_.isNull()) {
        imageOriginal_.setDevicePixelRatio(1.0);
    }
    imageZoomFactor_ = 1.0;
    updateImagePreview();
}

void ClipboardItemPreviewDialog::updateImagePreview() {
    if (!ui_.imageLabel || imageOriginal_.isNull()) {
        if (ui_.imageLabel) {
            ui_.imageLabel->clear();
        }
        return;
    }

    QSize viewportSize;
    if (ui_.imageScrollArea && ui_.imageScrollArea->viewport()) {
        viewportSize = ui_.imageScrollArea->viewport()->size();
    }
    if (!viewportSize.isValid() && ui_.imageScrollArea) {
        viewportSize = ui_.imageScrollArea->size();
    }

    const QSize imageSize = imageOriginal_.size();
    qreal fitScale = 1.0;
    if (viewportSize.isValid() && imageSize.isValid()) {
        const qreal scaleX = viewportSize.width() / static_cast<qreal>(imageSize.width());
        const qreal scaleY = viewportSize.height() / static_cast<qreal>(imageSize.height());
        fitScale = qMin(scaleX, scaleY);
    }
    imageFitScale_ = fitScale;
    qInfo().noquote() << QStringLiteral("[updateImagePreview] imageSize=%1x%2 viewportSize=%3x%4 fitScale=%5 zoomFactor=%6")
        .arg(imageSize.width()).arg(imageSize.height())
        .arg(viewportSize.width()).arg(viewportSize.height())
        .arg(fitScale, 0, 'f', 3).arg(imageZoomFactor_, 0, 'f', 3);

    qreal scale = imageFitScale_ * imageZoomFactor_;
    scale = qBound(kImageZoomMin, scale, kImageZoomMax);
    if (imageFitScale_ > 0.0) {
        imageZoomFactor_ = scale / imageFitScale_;
    }

    const qreal dpr = devicePixelRatioF();
    const QSizeF scaledPixelF = QSizeF(imageSize) * (scale * dpr);
    QSize scaledPixelSize = scaledPixelF.toSize();
    if (!scaledPixelSize.isValid()) {
        return;
    }

    QImage scaled = imageOriginal_.scaled(scaledPixelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return;
    }

    QPixmap pixmap = QPixmap::fromImage(scaled);
    pixmap.setDevicePixelRatio(dpr);
    ui_.imageLabel->setPixmap(pixmap);
    ui_.imageLabel->resize(pixmap.size() / pixmap.devicePixelRatio());
}

void ClipboardItemPreviewDialog::adjustImageZoom(qreal factor) {
    if (imageOriginal_.isNull()) {
        return;
    }
    imageZoomFactor_ = qBound(kImageZoomMin, imageZoomFactor_ * factor, kImageZoomMax);
    updateImagePreview();
}

void ClipboardItemPreviewDialog::resetImageZoom() {
    if (imageOriginal_.isNull()) {
        return;
    }
    imageZoomFactor_ = 1.0;
    updateImagePreview();
}

bool ClipboardItemPreviewDialog::handleImageZoomKey(QKeyEvent *event) {
    if (!isImagePreviewActive()) {
        return false;
    }
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (!(mods == Qt::NoModifier || mods == Qt::ControlModifier)) {
        return false;
    }

    switch (event->key()) {
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            adjustImageZoom(kImageZoomStep);
            event->accept();
            return true;
        case Qt::Key_Minus:
            adjustImageZoom(1.0 / kImageZoomStep);
            event->accept();
            return true;
        case Qt::Key_0:
            resetImageZoom();
            event->accept();
            return true;
        default:
            break;
    }

    return false;
}

bool ClipboardItemPreviewDialog::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (handleImageZoomKey(keyEvent)) {
            return true;
        }
        if (keyEvent->modifiers() == Qt::NoModifier
            && (keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_Escape)) {
            reject();
            keyEvent->accept();
            return true;
        }
    }

    if (ui_.imageScrollArea && watched == ui_.imageScrollArea->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && isImagePreviewActive()) {
            imageDragActive_ = true;
            imageDragStartPos_ = mouseEvent->globalPosition().toPoint();
            imageDragStartScroll_ = QPoint(ui_.imageScrollArea->horizontalScrollBar()->value(),
                                           ui_.imageScrollArea->verticalScrollBar()->value());
            ui_.imageScrollArea->viewport()->setCursor(Qt::ClosedHandCursor);
            mouseEvent->accept();
            return true;
        }
    }

    if (ui_.imageScrollArea && watched == ui_.imageScrollArea->viewport() && event->type() == QEvent::MouseMove) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (imageDragActive_ && isImagePreviewActive()) {
            const QPoint delta = mouseEvent->globalPosition().toPoint() - imageDragStartPos_;
            ui_.imageScrollArea->horizontalScrollBar()->setValue(imageDragStartScroll_.x() - delta.x());
            ui_.imageScrollArea->verticalScrollBar()->setValue(imageDragStartScroll_.y() - delta.y());
            mouseEvent->accept();
            return true;
        }
    }

    if (ui_.imageScrollArea && watched == ui_.imageScrollArea->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && imageDragActive_) {
            imageDragActive_ = false;
            ui_.imageScrollArea->viewport()->setCursor(Qt::OpenHandCursor);
            mouseEvent->accept();
            return true;
        }
    }

    if (ui_.imageScrollArea && watched == ui_.imageScrollArea->viewport() && event->type() == QEvent::Wheel) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        if (wheelEvent->modifiers() & Qt::ControlModifier) {
            const QPoint angle = wheelEvent->angleDelta();
            if (!angle.isNull()) {
                const qreal steps = angle.y() / 120.0;
                if (!qFuzzyIsNull(steps)) {
                    adjustImageZoom(std::pow(kImageZoomStep, steps));
                    wheelEvent->accept();
                    return true;
                }
            }
        }
    }

    if (ui_.imageScrollArea && watched == ui_.imageScrollArea->viewport() && event->type() == QEvent::Resize) {
        if (isImagePreviewActive()) {
            updateImagePreview();
        }
    }

    return QDialog::eventFilter(watched, event);
}

void ClipboardItemPreviewDialog::keyPressEvent(QKeyEvent *event) {
    if (handleImageZoomKey(event)) {
        return;
    }
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
    if (isImagePreviewActive()) {
        QDialog::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
    QDialog::mousePressEvent(event);
}

void ClipboardItemPreviewDialog::mouseMoveEvent(QMouseEvent *event) {
    if (isImagePreviewActive()) {
        QDialog::mouseMoveEvent(event);
        return;
    }
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

    imageOriginal_ = QImage();
    imageZoomFactor_ = 1.0;
    imageFitScale_ = 1.0;
    imageDragActive_ = false;
    if (ui_.imageLabel) {
        ui_.imageLabel->clear();
    }
    if (ui_.contentLayout && ui_.browser) {
        ui_.contentLayout->setCurrentWidget(ui_.browser);
    }
    if (ui_.imageScrollArea && ui_.imageScrollArea->viewport()) {
        ui_.imageScrollArea->viewport()->setCursor(Qt::OpenHandCursor);
    }

    QPointer<QTextDocument> oldDocument = ui_.browser->document();
    auto *newDocument = new QTextDocument(ui_.browser);
    ui_.browser->setDocument(newDocument);
    ui_.browser->document()->setDefaultFont(ui_.browser->font());
    if (oldDocument && oldDocument->parent() == ui_.browser) {
        oldDocument->deleteLater();
    }
}
