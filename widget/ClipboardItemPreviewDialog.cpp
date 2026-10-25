// input: Depends on ClipboardItemPreviewDialog.h, Qt widgets/layout, and ClipboardItem payload accessors.
// output: Screen-centered 980x760 previews with an independent compositor identity and capability-aware frosted surface.
// pos: Widget-layer preview dialog implementation for larger clipboard inspection.
// update: If I change, update this header block and my folder README.md (tuned preview font size + hidden caret/focus).
// note: Adds fallback decoding for Qt serialized image payloads and image zoom interactions.
#include "SurfacePainter.h"
#include "AuroraChrome.h"
#include "ClipboardCardDelegate.h"
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
#include <QPainterPath>
#include <QPalette>
#include <QTextBrowser>
#include <QTextDocument>
#include <QThread>
#include <QTimer>
#include <QtMath>
#include <QToolButton>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWindow>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedLayout>

#include "data/ContentClassifier.h"
#include "data/LocalSaver.h"
#include "data/MathMLRenderer.h"
#include "utils/ThemeManager.h"
#include "WindowBlurHelper.h"
#include "utils/ThumbnailBuilder.h"
#include "utils/MxGraphRenderer.h"
#include "utils/ThemeManager.h"
#include "BoardInternalHelpers.h"

// QTextBrowser subclass that blocks all remote resource loading.
// Without this, setHtml() with complex web content (e.g. B站 comments)
// triggers HTTP requests for every <img> and CSS background-image,
// freezing the UI for tens of seconds.
class OfflineTextBrowser : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;
    QVariant loadResource(int type, const QUrl &url) override {
        Q_UNUSED(type);
        Q_UNUSED(url);
        return QVariant();
    }
};

// simplifyHtmlForPreview — use ThumbnailBuilder::simplifyHtmlForRendering()


namespace {
// What show() does first: a hidden widget only records a resize, so the layouts
// below it keep the previous preview's geometry until the event is delivered.
// Parents go first, since their layout resizes the children.
void deliverPendingResizes(QWidget *widget) {
    if (widget->testAttribute(Qt::WA_PendingResizeEvent)) {
        widget->setAttribute(Qt::WA_PendingResizeEvent, false);
        QResizeEvent event(widget->size(), QSize());
        QCoreApplication::sendEvent(widget, &event);
    }
    for (QObject *child : widget->children()) {
        auto *childWidget = qobject_cast<QWidget *>(child);
        if (childWidget && !childWidget->isWindow()) {
            deliverPendingResizes(childWidget);
        }
    }
}

constexpr int kPreviewDialogWidth = 980;
constexpr int kPreviewDialogHeight = 760;
constexpr int kPreviewBodyFontSize = 16;
constexpr qreal kImageZoomMin = 0.1;
constexpr qreal kImageZoomMax = 8.0;
constexpr qreal kImageZoomStep = 1.15;

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
                background-color: rgba(255, 255, 255, 10);
                border: 1px solid rgba(255, 255, 255, 25);
                border-radius: 14px;
                padding: 12px;
                color: #E6EDF5;
                font-size: 16px;
                selection-background-color: rgba(74, 144, 226, 110);
            }
            QTextBrowser:focus {
                border-color: rgba(255, 255, 255, 40);
            }
            QToolButton#closeButton {
                background-color: rgba(255, 255, 255, 14);
                border: 1px solid rgba(255, 255, 255, 25);
                border-radius: 14px;
                color: #C9D4E0;
                font-size: 17px;
                font-weight: 700;
                min-width: 28px;
                min-height: 28px;
            }
            QToolButton#closeButton:hover {
                background-color: rgba(255, 255, 255, 28);
                border-color: rgba(255, 255, 255, 40);
            }
        )");
    }

    return QStringLiteral(R"(
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
            background-color: rgba(0, 0, 0, 8);
            border: 1px solid rgba(0, 0, 0, 18);
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
            background-color: rgba(0, 0, 0, 8);
            border: 1px solid rgba(0, 0, 0, 18);
            border-radius: 14px;
            color: #5E7084;
            font-size: 17px;
            font-weight: 700;
            min-width: 28px;
            min-height: 28px;
        }
        QToolButton#closeButton:hover {
            background-color: rgba(0, 0, 0, 16);
            border-color: rgba(0, 0, 0, 25);
        }
    )");
}

PreviewPayload buildPreviewPayload(ContentType contentType,
                                   const QString &normalizedText,
                                   const QList<QUrl> &normalizedUrls,
                                   const QString &html,
                                   const QByteArray &imageBytes,
                                   const QImage &fallbackImage,
                                   const QString &filePath,
                                   const QSize &targetSize,
                                   qreal devicePixelRatio,
                                   bool isEquation) {
    PreviewPayload payload;

    switch (contentType) {
        case Text:
        case Link: {
            payload.kind = PreviewKind::PlainText;
            payload.text = normalizedText.isEmpty() ? unavailableText() : normalizedText;
            break;
        }
        case Image: {
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
        case Office: {
            // Equation items (MathType or Word/PowerPoint native): skip
            // the text/html fallback because their normalizedText is just
            // a Unicode-stripped version of the MathML XML, which reads
            // as gibberish. Show the rendered formula thumbnail instead.
            if (isEquation && !fallbackImage.isNull()) {
                QImage image = scalePreviewImage(fallbackImage, QSize(), devicePixelRatio);
                if (!image.isNull()) {
                    payload.kind = PreviewKind::Image;
                    payload.image = image;
                    payload.imageUrl = QStringLiteral("preview-image://mathml");
                    break;
                }
            }
            // Prefer rich text or plain text so the user can select/copy
            // content.  Fall back to the image only for shape-only items
            // (e.g. PowerPoint diagrams) that carry no readable text.
            if (!html.isEmpty() && !normalizedText.trimmed().isEmpty()) {
                payload.kind = PreviewKind::Html;
                payload.html = html;
            } else if (!normalizedText.trimmed().isEmpty()) {
                payload.kind = PreviewKind::PlainText;
                payload.text = normalizedText;
            } else {
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
            }
            break;
        }
        case File: {
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
        case RichText: {
            if (MxGraphRenderer::isMxGraphContent(html)
                || MxGraphRenderer::isMxGraphContent(normalizedText)) {
                QString xml = MxGraphRenderer::extractMxGraphXml(html);
                if (xml.isEmpty()) {
                    xml = MxGraphRenderer::extractMxGraphXml(normalizedText);
                }
                if (!xml.isEmpty()) {
                    const QSize previewPixelSize = targetSize.isValid()
                        ? targetSize * devicePixelRatio
                        : QSize(kPreviewDialogWidth, kPreviewDialogHeight) * devicePixelRatio;
                    const bool dark = ThemeManager::instance()->isDark();
                    QImage image = MxGraphRenderer::render(xml, previewPixelSize, devicePixelRatio, dark);
                    if (!image.isNull()) {
                        payload.kind = PreviewKind::Image;
                        payload.image = image;
                        payload.imageUrl = QStringLiteral("preview-image://mxgraph");
                        break;
                    }
                }
            }
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
        case Color:
        case All:
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
    setWindowTitle(QStringLiteral("MPaste Preview")); // Stable compositor identity, separate from the panel.
    resize(kPreviewDialogWidth, kPreviewDialogHeight);

    auto *cardLayout = new QVBoxLayout(this);
    cardLayout->setContentsMargins(20, 16, 16, 12);
    cardLayout->setSpacing(12);

    // Header: the source app, what and when, the actions, close.
    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);

    ui_.iconLabel = new QLabel(this);
    ui_.iconLabel->setFixedSize(32, 32);
    headerLayout->addWidget(ui_.iconLabel, 0, Qt::AlignVCenter);

    auto *titleLayout = new QVBoxLayout;
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(1);
    ui_.titleLabel = new QLabel(this);
    ui_.titleLabel->setObjectName(QStringLiteral("auroraTitle"));
    titleLayout->addWidget(ui_.titleLabel);
    ui_.subtitleLabel = new QLabel(this);
    ui_.subtitleLabel->setObjectName(QStringLiteral("auroraSubtitle"));
    titleLayout->addWidget(ui_.subtitleLabel);
    headerLayout->addLayout(titleLayout, 1);

    ui_.actions = new QWidget(this);
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
        connect(button, &QPushButton::clicked, this, [this, button, primary, handler]() {
            button->setChecked(primary);
            handler();
        });
        actionsLayout->addWidget(button);
        return button;
    };
    addAction(uiText(QStringLiteral("Paste"), QStringLiteral("粘贴")), true, [this]() {
        const ClipboardItem item = currentItem_;
        reject();
        emit pasteRequested(item);
    });
    addAction(uiText(QStringLiteral("Copy"), QStringLiteral("复制")), false, [this]() {
        const ClipboardItem item = currentItem_;
        reject();
        emit copyRequested(item);
    });
    ui_.ocrButton = addAction(uiText(QStringLiteral("Read text"), QStringLiteral("识别文字")), false, [this]() {
        const ClipboardItem item = currentItem_;
        reject();
        emit ocrRequested(item);
    });
    ui_.saveButton = addAction(uiText(QStringLiteral("Save as"), QStringLiteral("另存为")), false, [this]() {
        emit saveRequested(currentItem_);
    });
    headerLayout->addWidget(ui_.actions, 0, Qt::AlignVCenter);

    ui_.closeButton = new QToolButton(this);
    ui_.closeButton->setObjectName(QStringLiteral("auroraClose"));
    ui_.closeButton->setText(QStringLiteral("\u2715"));
    ui_.closeButton->setCursor(Qt::PointingHandCursor);
    ui_.closeButton->setToolTip(uiText(QStringLiteral("Close"), QStringLiteral("关闭")));
    connect(ui_.closeButton, &QToolButton::clicked, this, &ClipboardItemPreviewDialog::reject);
    headerLayout->addWidget(ui_.closeButton, 0, Qt::AlignVCenter);
    cardLayout->addLayout(headerLayout);

    ui_.browser = new OfflineTextBrowser(this);
    ui_.browser->setObjectName(QStringLiteral("auroraWell"));
    QFont previewFont = ui_.browser->font();
#ifdef Q_OS_WIN
    previewFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
#endif
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

    ui_.imageLabel = new QLabel(this);
    ui_.imageLabel->setAlignment(Qt::AlignCenter);
    ui_.imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    ui_.imageLabel->setBackgroundRole(QPalette::Base);
    ui_.imageLabel->setFocusPolicy(Qt::NoFocus);

    ui_.imageScrollArea = new QScrollArea(this);
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

    auto *contentHost = new QWidget(this);
    ui_.contentLayout = new QStackedLayout(contentHost);
    ui_.contentLayout->setContentsMargins(0, 0, 0, 0);
    ui_.contentLayout->addWidget(ui_.browser);
    ui_.contentLayout->addWidget(ui_.imageScrollArea);
    ui_.contentLayout->setCurrentWidget(ui_.browser);
    cardLayout->addWidget(contentHost, 1);

    ui_.footerLabel = new QLabel(this);
    ui_.footerLabel->setObjectName(QStringLiteral("auroraHint"));
    ui_.footerLabel->setText(uiText(QStringLiteral("Enter  paste      Space / Esc  close"),
                                    QStringLiteral("↵  粘贴      Space / Esc  关闭")));
    cardLayout->addWidget(ui_.footerLabel);

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ClipboardItemPreviewDialog::applyTheme);
}

bool ClipboardItemPreviewDialog::supportsPreview(const ClipboardItem &item) {
    switch (item.getContentType()) {
        case Text:
        case Link:
        case RichText:
        case Image:
        case Office:
            return true;
        case File:
            return !item.getNormalizedUrls().isEmpty();
        default:
            return false;
    }
}

void ClipboardItemPreviewDialog::showItem(const ClipboardItem &item) {
    releasePreviewContent();
    resize(kPreviewDialogWidth, kPreviewDialogHeight);

    const ContentType contentType = item.getContentType();

    currentItem_ = item;
    accent_ = ClipboardCardDelegate::appAccentColor(item.getIcon());
    ui_.iconLabel->setPixmap(ClipboardCardDelegate::appIconTile(
        item.getIcon(), item.getSourceApp().isEmpty() ? QStringLiteral("M") : item.getSourceApp(), 32, devicePixelRatioF()));
    ui_.ocrButton->setVisible(contentType == Image || contentType == Office);
    ui_.saveButton->setVisible(contentType != File);

    // The card's own name, then what it is and when it was copied (the stored file name means nothing to people).
    const QString alias = item.getAlias().trimmed();
    ui_.titleLabel->setText(!alias.isEmpty() ? alias
        : (!item.getSourceApp().isEmpty() ? item.getSourceApp()
                                          : uiText(QStringLiteral("Clipboard Preview"), QStringLiteral("剪贴板预览"))));
    QStringList facts;
    if (!alias.isEmpty() && !item.getSourceApp().isEmpty()) {
        facts << item.getSourceApp();
    }
    switch (contentType) {
        case Text: facts << uiText(QStringLiteral("Plain text"), QStringLiteral("纯文本")); break;
        case RichText: facts << uiText(QStringLiteral("Rich text"), QStringLiteral("富文本")); break;
        case Link: facts << uiText(QStringLiteral("Link"), QStringLiteral("链接")); break;
        case Image: facts << uiText(QStringLiteral("Image"), QStringLiteral("图片")); break;
        case File: facts << uiText(QStringLiteral("File"), QStringLiteral("文件")); break;
        case Color: facts << uiText(QStringLiteral("Color"), QStringLiteral("颜色")); break;
        case Office: facts << QStringLiteral("Office"); break;
        default: break;
    }
    if (item.getTime().isValid()) {
        facts << QLocale().toString(item.getTime(), QLocale::ShortFormat);
    }
    const QSize pixelSize = item.getImagePixelSize();
    if ((contentType == Image || contentType == Office) && pixelSize.isValid()) {
        facts << QStringLiteral("%1 \u00D7 %2").arg(pixelSize.width()).arg(pixelSize.height());
    } else if (contentType == Text || contentType == RichText) {
        facts << uiText(QStringLiteral("%1 characters"), QStringLiteral("%1 字符")).arg(item.getNormalizedText().size());
    } else if (contentType == File && item.getNormalizedUrls().size() > 1) {
        facts << uiText(QStringLiteral("%1 files"), QStringLiteral("%1 个文件")).arg(item.getNormalizedUrls().size());
    }
    ui_.subtitleLabel->setText(facts.join(QStringLiteral(" \u00B7 ")));

    ui_.browser->clear();
    ui_.browser->document()->clear();
    ui_.browser->setPlainText(uiText(QStringLiteral("Loading preview..."), QStringLiteral("正在加载预览...")));

    const QString normalizedText = item.getNormalizedText();
    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();
    const QString html = (contentType == RichText || contentType == Office) ? item.getHtml() : QString();
    const QByteArray imageBytes = (contentType == Image
            || contentType == Office
            || contentType == RichText)
        ? item.imagePayloadBytesFast()
        : QByteArray();
    QImage fallbackImage;
    if (contentType == Office) {
        const QPixmap previewPixmap = item.getImage();
        if (!previewPixmap.isNull()) {
            fallbackImage = previewPixmap.toImage();
            fallbackImage.setDevicePixelRatio(previewPixmap.devicePixelRatio());
        } else if (item.hasThumbnail()) {
            fallbackImage = item.thumbnail().toImage();
        }
    } else if (contentType == Image && item.hasThumbnail()) {
        fallbackImage = item.thumbnail().toImage();
    }
    QString filePath;
    if (contentType == File && normalizedUrls.size() == 1 && normalizedUrls.first().isLocalFile()) {
        filePath = normalizedUrls.first().toLocalFile();
    }
    const QString sourceFilePath = item.sourceFilePath();
    const quint64 mimeOffset = item.mimeDataFileOffset();
    const bool preferFullItem = !sourceFilePath.isEmpty();
    // Equation items all carry the "公式" title regardless of source
    // (MathType, Word/PowerPoint native, ...). Both paths render through
    // the MathML renderer in the preview dialog.
    bool isEquation = (item.getTitle() == QStringLiteral("公式"));

    // If we already have mime data in memory, snapshot the MathML bytes
    // here so the worker thread can re-render at high resolution without
    // paying for a disk full-load.
    QByteArray mathmlBytes;
    if (isEquation) {
        if (const QMimeData *md = item.getMimeData()) {
            for (const QString &fmt : md->formats()) {
                if (fmt.toLower().contains(QLatin1String("mathml"))) {
                    mathmlBytes = md->data(fmt);
                    if (!mathmlBytes.isEmpty()) {
                        break;
                    }
                }
            }
        }
    }
    const QSize targetSize(kPreviewDialogWidth - 120, kPreviewDialogHeight - 180);
    const qreal dpr = devicePixelRatioF();
    const quint64 token = ++previewToken_;

    QPointer<ClipboardItemPreviewDialog> guard(this);
    QThread *thread = QThread::create([guard, contentType, normalizedText, normalizedUrls, html, imageBytes, fallbackImage, filePath, sourceFilePath, mimeOffset, preferFullItem, targetSize, dpr, token, isEquation, mathmlBytes]() mutable {
        ContentType resolvedType = contentType;
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
                if (resolvedType == File && resolvedUrls.size() == 1 && resolvedUrls.front().isLocalFile()) {
                    resolvedFilePath = resolvedUrls.front().toLocalFile();
                }
                if (lightItem.hasThumbnail()) {
                    resolvedFallbackImage = lightItem.thumbnail().toImage();
                }
                if (lightItem.getTitle() == QStringLiteral("公式")) {
                    isEquation = true;
                }
            }

            // For types that need image/html payloads, read ONLY those
            // sections from disk — not the full MIME blob.
            // Also try for All/unknown types (e.g. items from remote sync).
            if (resolvedType == RichText
                || resolvedType == Image
                || resolvedType == Office
                || resolvedType == All) {
                QString htmlPayload;
                QByteArray imagePayload;
                LocalSaver::loadMimePayloads(sourceFilePath, mimeOffset,
                    (resolvedType == RichText || resolvedType == Office) ? &htmlPayload : nullptr,
                    &imagePayload);
                if (!htmlPayload.isEmpty()) {
                    resolvedHtml = htmlPayload;
                }
                if (!imagePayload.isEmpty()) {
                    resolvedImageBytes = imagePayload;
                }
            } else if (resolvedType == Text
                       || resolvedType == Link) {
                // lightItem.getNormalizedText() already has the full cached
                // text from disk.  For rich HTML preview (e.g. RichText
                // fallback-to-text), load the HTML payload only.
                if (resolvedText.isEmpty() || resolvedType == Link) {
                    QString htmlPayload;
                    LocalSaver::loadMimePayloads(sourceFilePath, mimeOffset, &htmlPayload, nullptr);
                    if (!htmlPayload.isEmpty()) {
                        resolvedHtml = htmlPayload;
                    }
                }
            }
        } else if (!sourceFilePath.isEmpty()
                   && (resolvedType == Image
                       || resolvedType == Office
                       || resolvedType == RichText)) {
            // Fallback: load image/html payloads for items without
            // preferFullItem (e.g. freshly copied, not yet on disk).
            QString htmlPayload;
            QByteArray imagePayload;
            LocalSaver::loadMimePayloads(sourceFilePath, mimeOffset,
                (resolvedType == RichText || resolvedType == Office) ? &htmlPayload : nullptr,
                &imagePayload);
            if (resolvedHtml.isEmpty() && !htmlPayload.isEmpty()) {
                resolvedHtml = htmlPayload;
            }
            if (resolvedImageBytes.isEmpty() && !imagePayload.isEmpty()) {
                resolvedImageBytes = imagePayload;
            }
        }

        // For MathType items, re-render the formula at preview-dialog
        // resolution from the original MathML source — naively scaling the
        // 275×224 card thumbnail looks blurry. Try the in-memory snapshot
        // first; if absent (item only on disk), do a full load to fetch the
        // MathML format bytes.
        if (isEquation) {
            QByteArray mml = mathmlBytes;
            if (mml.isEmpty() && !sourceFilePath.isEmpty()) {
                LocalSaver fullSaver;
                ClipboardItem fullItem = fullSaver.loadFromFile(sourceFilePath);
                if (const QMimeData *fmd = fullItem.getMimeData()) {
                    for (const QString &fmt : fmd->formats()) {
                        if (fmt.toLower().contains(QLatin1String("mathml"))) {
                            mml = fmd->data(fmt);
                            if (!mml.isEmpty()) {
                                break;
                            }
                        }
                    }
                }
            }
            if (!mml.isEmpty()) {
                QString mathmlText;
                if (mml.size() >= 2
                    && (mml.at(1) == '\0' || (static_cast<unsigned char>(mml.at(0)) == 0xFF
                                              && static_cast<unsigned char>(mml.at(1)) == 0xFE))) {
                    mathmlText = QString::fromUtf16(
                        reinterpret_cast<const char16_t *>(mml.constData()),
                        mml.size() / 2);
                } else {
                    mathmlText = QString::fromUtf8(mml);
                }
                const QSize hiResSize = targetSize.isValid()
                    ? targetSize * qMax<qreal>(1.0, dpr)
                    : QSize(kPreviewDialogWidth, kPreviewDialogHeight);
                const bool dark = ThemeManager::instance()->isDark();
                QPixmap rendered = MathMLRenderer::renderAt(mathmlText, hiResSize, dark);
                if (!rendered.isNull()) {
                    QImage img = rendered.toImage();
                    img.setDevicePixelRatio(qMax<qreal>(1.0, dpr));
                    resolvedFallbackImage = img;
                }
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
                                                     dpr,
                                                     isEquation);
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
                        guard->ui_.browser->setHtml(ThumbnailBuilder::simplifyHtmlForRendering(payload.html));
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
                if (guard->revealPending_) {
                    guard->revealSizedToContent();
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
        screenArea_ = targetScreen->availableGeometry();
        const int fullWidth = qMin(kPreviewDialogWidth, qMax(1, screenArea_.width() - 32));
        // An open preview keeps its height until the new content is in.
        resize(fullWidth, isVisible() ? height()
                                      : qMin(kPreviewDialogHeight, qMax(1, screenArea_.height() - 32)));
    }

    // Shown (or, when already open, refitted) once the content is in, at the
    // height it needs; a slow load shows the window (at the full height) after a
    // moment instead of keeping it back.
    revealPending_ = true;
    QTimer::singleShot(400, this, [this, token]() {
        if (revealPending_ && previewToken_ == token) {
            revealSizedToContent();
        }
    });
}

int ClipboardItemPreviewDialog::heightForContent() const {
    QWidget *host = ui_.contentLayout ? ui_.contentLayout->parentWidget() : nullptr;
    if (!host) {
        return height();
    }
    const int chrome = height() - host->height();
    int content = host->height();
    if (isImagePreviewActive()) {
        if (!imageOriginal_.isNull()) {
            const qreal scale = qMin<qreal>(1.0, host->width() / qMax<qreal>(1.0, imageOriginal_.width()));
            content = qCeil(imageOriginal_.height() * scale);
        }
    } else if (ui_.browser) {
        // The browser's own box, not its viewport: the viewport also loses the
        // width of a scroll bar the previous, longer preview needed.
        const QRect inner = ui_.browser->contentsRect(); // frame + stylesheet padding removed
        const int frame = ui_.browser->height() - inner.height();
        QTextDocument *document = ui_.browser->document();
        document->setTextWidth(inner.width());
        content = qCeil(document->size().height()) + frame + 4;
    }
    const int maxHeight = screenArea_.isValid() ? qRound(screenArea_.height() * 0.85) : kPreviewDialogHeight;
    return qBound(240, chrome + content, qMax(240, maxHeight));
}

void ClipboardItemPreviewDialog::revealSizedToContent() {
    revealPending_ = false;
    ensurePolished();
    if (layout()) {
        layout()->activate();
    }
    deliverPendingResizes(this);
    resize(width(), heightForContent());
    if (screenArea_.isValid()) {
        move(screenArea_.center() - QPoint(width() / 2, height() / 2));
    }
    show();
    AuroraChrome::attachBackdrop(this, darkTheme_);
    raise();
    activateWindow();
    if (isImagePreviewActive()) {
        QTimer::singleShot(0, this, [this]() {
            imageZoomFactor_ = 1.0;
            updateImagePreview();
        });
    }
}

void ClipboardItemPreviewDialog::reject() {
    QDialog::reject();

}

void ClipboardItemPreviewDialog::applyTheme(bool dark) {
    darkTheme_ = dark;
    AuroraChrome::attachBackdrop(this, darkTheme_);
    if (ui_.browser) {
        ui_.browser->setStyleSheet(QString());
    }
    setStyleSheet(AuroraChrome::styleSheet(darkTheme_)
                  + QStringLiteral("QTextBrowser#auroraWell { font-size: 15px; padding: 12px 14px; }"));
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
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const ClipboardItem item = currentItem_;
        reject();
        emit pasteRequested(item);
        event->accept();
        return;
    }

    QDialog::keyPressEvent(event);
}

void ClipboardItemPreviewDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    AuroraChrome::paintWindow(painter, QRectF(rect()), darkTheme_, accent_);
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
    if (translated == source || BoardHelpers::looksBrokenTranslation(translated)) {
        return BoardHelpers::prefersChineseUi() ? zhFallback : source;
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
