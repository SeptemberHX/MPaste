// input: Depends on QFileSystemWatcher, QTimer, and ClipboardBoardService for write-generation checks.
// output: Provides file-system sync watching that detects external changes to board directories.
// pos: Widget-layer helper owned by MPasteWidget.
// update: If I change, update this header block.
#ifndef SYNCWATCHER_H
#define SYNCWATCHER_H

#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>

class ClipboardBoardService;

class SyncWatcher : public QObject {
    Q_OBJECT

public:
    explicit SyncWatcher(QObject *parent = nullptr);
    ~SyncWatcher() override = default;

    /// Set up (or reconfigure) watched directories.
    void setup(const QString &rootDir, const QStringList &categoryDirs);

    /// Register board services used for internal-write detection.
    void setBoardServices(ClipboardBoardService *clipboardService, ClipboardBoardService *starService);

    /// Mark a pending reload (e.g. when hidden and a change was detected).
    void setPendingReload();

    /// Check and clear the pending reload flag.  Returns true if a reload was pending
    /// and the write generation has not changed since it was set (i.e. the change was external).
    bool checkPendingReload();

    /// Snapshot the current combined write generation from the board services.
    quint64 currentWriteGeneration() const;

    /// Suppress reloads until a given timestamp (e.g. after our own file writes).
    void suppressReloadUntil(qint64 timestampMs);

signals:
    /// Emitted when an external sync change is detected and the widget is visible.
    void syncRequested();

private:
    void scheduleSyncReload();

    QFileSystemWatcher *watcher_ = nullptr;
    QTimer *reloadTimer_ = nullptr;
    bool pendingReload_ = false;
    qint64 suppressReloadUntilMs_ = 0;
    quint64 pendingWriteGen_ = 0;

    ClipboardBoardService *clipboardService_ = nullptr;
    ClipboardBoardService *starService_ = nullptr;
};

#endif // SYNCWATCHER_H
