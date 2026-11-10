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
        reloadTimer_->setInterval(kReloadDebounceMs);
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
}

bool SyncWatcher::checkPendingReload() {
    if (!pendingReload_) {
        return false;
    }
    pendingReload_ = false;
    // The flag is only set for a change scheduleSyncReload() let through, so
    // it stands whatever we wrote since: a local copy made while the panel was
    // hidden must not cancel a reload owed to another device. The reload is an
    // incremental diff of the directory, cheap when there is nothing new.
    return true;
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
    // Honour the suppress window requested by the controller after
    // a local persistence change (e.g. save, delete, alias update).
    // Inside either window the change is most likely our own write, but an
    // external one can land there too and no later event will announce it.
    // So look again once the windows have passed instead of dropping it.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool suppressed = suppressReloadUntilMs_ > 0 && now < suppressReloadUntilMs_;
    const bool recentInternalWrite = (clipboardService_ && clipboardService_->hasRecentInternalWrite())
        || (starService_ && starService_->hasRecentInternalWrite());
    if (suppressed || recentInternalWrite) {
        reloadTimer_->start(kRecheckAfterOwnWriteMs);
        return;
    }
    reloadTimer_->start(kReloadDebounceMs);
}
