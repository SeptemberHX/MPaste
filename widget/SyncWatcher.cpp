// input: Depends on SyncWatcher.h, ClipboardBoardService, QDir, and MPasteSettings.
// output: Implements file-system sync watching with debounce and internal-write suppression.
// pos: Widget-layer helper implementation.
// update: If I change, update this header block.
#include "SyncWatcher.h"
#include "utils/ClipboardBoardService.h"
#include <QDir>
#include <QDateTime>

SyncWatcher::SyncWatcher(QObject *parent)
    : QObject(parent)
{
}

void SyncWatcher::setup(const QString &rootDir, const QStringList &categoryDirs) {
    if (rootDir.isEmpty()) {
        return;
    }

    if (!watcher_) {
        watcher_ = new QFileSystemWatcher(this);
        connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
            scheduleSyncReload();
        });
    }

    if (!reloadTimer_) {
        reloadTimer_ = new QTimer(this);
        reloadTimer_->setSingleShot(true);
        reloadTimer_->setInterval(400);
        connect(reloadTimer_, &QTimer::timeout, this, [this]() {
            // The parent (MPasteWidget) checks visibility before acting on the signal.
            // If the widget is hidden, mark pending instead.
            emit syncRequested();
        });
    }

    QDir().mkpath(rootDir);
    QStringList watchDirs;
    watchDirs << rootDir;
    for (const QString &dir : categoryDirs) {
        QDir().mkpath(dir);
        watchDirs << dir;
    }

    const QStringList existing = watcher_->directories();
    if (!existing.isEmpty()) {
        watcher_->removePaths(existing);
    }
    watcher_->addPaths(watchDirs);
}

void SyncWatcher::setBoardServices(ClipboardBoardService *clipboardService, ClipboardBoardService *starService) {
    clipboardService_ = clipboardService;
    starService_ = starService;
}

void SyncWatcher::setPendingReload() {
    pendingReload_ = true;
    pendingWriteGen_ = currentWriteGeneration();
}

bool SyncWatcher::checkPendingReload() {
    if (!pendingReload_) {
        return false;
    }
    pendingReload_ = false;
    // If write generations changed since the pending flag was set,
    // the directory change was caused by our own async saves -- skip.
    return currentWriteGeneration() == pendingWriteGen_;
}

quint64 SyncWatcher::currentWriteGeneration() const {
    quint64 gen = 0;
    if (clipboardService_) {
        gen += clipboardService_->internalWriteGeneration();
    }
    if (starService_) {
        gen += starService_->internalWriteGeneration();
    }
    return gen;
}

void SyncWatcher::suppressReloadUntil(qint64 timestampMs) {
    suppressReloadUntilMs_ = timestampMs;
}

void SyncWatcher::scheduleSyncReload() {
    if (!reloadTimer_) {
        return;
    }
    // Ignore directory changes caused by our own file writes.
    if ((clipboardService_ && clipboardService_->hasRecentInternalWrite())
        || (starService_ && starService_->hasRecentInternalWrite())) {
        return;
    }
    reloadTimer_->start();
}
