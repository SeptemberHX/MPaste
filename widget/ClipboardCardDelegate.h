// input: Depends on Qt delegate painting APIs and ClipboardBoardModel roles.
// output: Exposes the card painter used by the delegate-based clipboard board view.
// pos: Widget-layer delegate that draws clipboard cards without per-row widgets.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDCARDDELEGATE_H
#define MPASTE_CLIPBOARDCARDDELEGATE_H

#include <memory>

#include <QCache>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QQueue>
#include <QSet>
#include <QSize>
#include <QStyledItemDelegate>
#include <QString>
#include <QThreadPool>
#include <QUrl>

#include "data/ClipboardItem.h"
#include "CardTheme.h"
#include "ClipboardBoardModel.h"

class CardBodyRenderer;
class QModelIndex;
class QTimer;
class QUrl;

struct CardData {
    ContentType contentType = All;
    ClipboardPreviewKind previewKind = TextPreview;
    ClipboardBoardModel::PreviewState previewState = ClipboardBoardModel::PreviewNotApplicable;
    QPixmap icon;
    QPixmap thumbnail;
    QPixmap favicon;
    QString title;
    QString url;
    QString alias;
    QString normalizedText;
    int textLength = 0;
    QList<QUrl> normalizedUrls;
    QDateTime time;
    QSize imageSize;
    QColor color;
    bool favorite = false;
    bool pinned = false;
    QString shortcutText;
    QString name;
};

class ClipboardCardDelegate : public QStyledItemDelegate {
public:
    explicit ClipboardCardDelegate(const QColor &borderColor, QObject *parent = nullptr);
    ~ClipboardCardDelegate() override;

    const CardTheme &cachedTheme() const { return cachedTheme_; }
    void setLoadingPhase(int phase);
    void clearIntermediateCaches();
    void clearVisualCaches();
    bool isCardCached(const QString &name) const;
    void invalidateCard(const QString &name);
    int cachedCardCount() const { return cardPixmapCache_.size(); }
    int cachedCardMaxCost() const { return cardPixmapCache_.maxCost(); }
    QString cacheMemoryStats() const;
    void preRenderAll(QAbstractItemModel *model, const QStyleOptionViewItem &baseOption, int maxRows = -1);
    void drawSelectionBorder(QPainter *painter, const QStyleOptionViewItem &option,
                             bool selected, int scale) const;
    void drawShortcutOverlay(QPainter *painter, const QStyleOptionViewItem &option,
                             const QString &shortcutText, int scale) const;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // Delegate-level cache accessors used by card body renderers.
    QPixmap linkFallbackPreview(const QUrl &url,
                                const QString &title,
                                const QSize &targetSize,
                                qreal devicePixelRatio,
                                const QPixmap &favicon) const;
    QPixmap localImageThumbnail(const QString &filePath,
                                const QSize &targetLogicalSize,
                                qreal targetDpr,
                                const QModelIndex &index) const;
    QPixmap localFileIcon(const QString &filePath,
                          const QSize &targetLogicalSize,
                          qreal targetDpr,
                          const QModelIndex &index) const;

private:
    struct FileIconRequest {
        QString cacheKey;
        QString filePath;
        QSize targetLogicalSize;
        qreal targetDpr = 1.0;
        QPersistentModelIndex index;
    };

    const CardBodyRenderer &bodyRendererForType(ContentType type) const;

    QColor headerColorForIcon(const QPixmap &iconPixmap) const;
    QPixmap headerIconForPixmap(const QPixmap &sourcePixmap, const QSize &targetLogicalSize, qreal targetDpr) const;
    void paintCardContent(QPainter *painter, const QStyleOptionViewItem &option,
                          const QModelIndex &index, const CardData &card, int scale) const;
    void ensureFileIconTimer() const;
    void enqueueFileIconRequest(const FileIconRequest &request) const;
    void scheduleViewportUpdate(const QPersistentModelIndex &index) const;

    CardTheme cachedTheme_;
    QColor borderColor_;
    int loadingPhase_ = 0;
    mutable QHash<quint64, QColor> headerColorCache_;
    mutable QCache<QString, QPixmap> headerIconCache_;
    mutable QCache<QString, QPixmap> linkFallbackCache_;
    mutable QCache<QString, QPixmap> cardPixmapCache_;
    mutable QCache<QString, QPixmap> localImageThumbnailCache_;
    mutable QCache<QString, QPixmap> localFileIconCache_;
    mutable QSet<QString> pendingLocalImageThumbnailKeys_;
    mutable QSet<QString> failedLocalImageThumbnailKeys_;
    mutable QSet<QString> pendingLocalFileIconKeys_;
    mutable QSet<QString> failedLocalFileIconKeys_;
    mutable QQueue<FileIconRequest> pendingFileIconRequests_;
    mutable QTimer *fileIconLoadTimer_ = nullptr;
    mutable QThreadPool previewTaskPool_;

    // Body renderers (one per content type).
    std::unique_ptr<CardBodyRenderer> imageRenderer_;
    std::unique_ptr<CardBodyRenderer> officeRenderer_;
    std::unique_ptr<CardBodyRenderer> richTextRenderer_;
    std::unique_ptr<CardBodyRenderer> colorRenderer_;
    std::unique_ptr<CardBodyRenderer> linkRenderer_;
    std::unique_ptr<CardBodyRenderer> fileRenderer_;
    std::unique_ptr<CardBodyRenderer> textRenderer_;
};

#endif // MPASTE_CLIPBOARDCARDDELEGATE_H
