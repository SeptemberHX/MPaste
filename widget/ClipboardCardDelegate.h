// input: Depends on Qt delegate painting APIs and ClipboardBoardModel roles.
// output: Exposes the card painter used by the delegate-based clipboard board view.
// pos: Widget-layer delegate that draws clipboard cards without per-row widgets.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDCARDDELEGATE_H
#define MPASTE_CLIPBOARDCARDDELEGATE_H

#include <QCache>
#include <QColor>
#include <QHash>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QQueue>
#include <QSet>
#include <QSize>
#include <QStyledItemDelegate>
#include <QString>
#include <QThreadPool>

class QModelIndex;
class QTimer;
class QUrl;

class ClipboardCardDelegate : public QStyledItemDelegate {
public:
    explicit ClipboardCardDelegate(const QColor &borderColor, QObject *parent = nullptr);
    ~ClipboardCardDelegate() override;

    void setLoadingPhase(int phase);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    struct FileIconRequest {
        QString cacheKey;
        QString filePath;
        QSize targetLogicalSize;
        qreal targetDpr = 1.0;
        QPersistentModelIndex index;
    };

    QColor headerColorForIcon(const QPixmap &iconPixmap) const;
    QPixmap headerIconForPixmap(const QPixmap &sourcePixmap, const QSize &targetLogicalSize, qreal targetDpr) const;
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
    void ensureFileIconTimer() const;
    void enqueueFileIconRequest(const FileIconRequest &request) const;
    void scheduleViewportUpdate(const QPersistentModelIndex &index) const;

    QColor borderColor_;
    int loadingPhase_ = 0;
    mutable QHash<quint64, QColor> headerColorCache_;
    mutable QCache<QString, QPixmap> headerIconCache_;
    mutable QCache<QString, QPixmap> linkFallbackCache_;
    mutable QCache<QString, QPixmap> localImageThumbnailCache_;
    mutable QCache<QString, QPixmap> localFileIconCache_;
    mutable QSet<QString> pendingLocalImageThumbnailKeys_;
    mutable QSet<QString> failedLocalImageThumbnailKeys_;
    mutable QSet<QString> pendingLocalFileIconKeys_;
    mutable QSet<QString> failedLocalFileIconKeys_;
    mutable QQueue<FileIconRequest> pendingFileIconRequests_;
    mutable QTimer *fileIconLoadTimer_ = nullptr;
    mutable QThreadPool previewTaskPool_;
};

#endif // MPASTE_CLIPBOARDCARDDELEGATE_H
