// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 ClipboardMonitor 的声明接口。
// pos: utils 层中的 ClipboardMonitor 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
// note: Adds image payload settle retries for lazy clipboard providers.
//
// Created by ragdoll on 2021/5/22.
//

#ifndef MPASTE_CLIPBOARDMONITOR_H
#define MPASTE_CLIPBOARDMONITOR_H

#include <QObject>
#include <QByteArray>
#include <QMap>
#include <QTimer>
#include <QPointer>
#include "data/ClipboardItem.h"

class QMimeData;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

class ClipboardMonitor : public QObject {
    Q_OBJECT

public:
    ClipboardMonitor();

    void disconnectMonitor();
    void connectMonitor();
    void primeCurrentClipboard();

signals:
    void clipboardActivityObserved(int wId);
    void clipboardUpdated(const ClipboardItem &item, int wId);
    void clipboardMimeCompleted(const QString &itemName, const QMap<QString, QByteArray> &extraFormats);

public slots:
    void clipboardChanged();

private:
    void beginClipboardCapture(bool emitActivitySignal);
    void checkAndCapture();
    void captureClipboard();
    void cancelPendingImageFetch();
    void emitCapturedItem(const ClipboardItem &item, int wId);
    bool isDuplicateRecentCapture(const ClipboardItem &item, int wId) const;
    void rememberCapturedItem(const ClipboardItem &item, int wId);
    void fetchWpsImageAndEmit(const QMimeData *mimeData, const QUrl &url, int wId, quint64 captureToken);
    void scheduleDeferredMimeCapture(const QString &itemName);

    static bool hasMeaningfulContent(const QMimeData *mimeData);
    static bool looksLikeWpsStagedClipboard(const QMimeData *mimeData);
    static QUrl extractWpsSingleImageUrl(const QMimeData *mimeData);
    static QMimeData *cloneMimeData(const QMimeData *mimeData);
    static void materializePng(QMimeData *mimeData, const QByteArray &pngBytes);

    QTimer stabilizeTimer_;
    int pendingWId_ = 0;
    quint32 lastSeqNumber_ = 0;
    int retryCount_ = 0;
    quint64 captureToken_ = 0;
    bool wpsSettlePending_ = false;
    QNetworkAccessManager *imageFetchManager_ = nullptr;
    QPointer<QNetworkReply> pendingImageFetchReply_;
    QByteArray lastCaptureKey_;
    qint64 lastCaptureAtMs_ = 0;
    int lastCaptureWindowId_ = 0;

    static const int STABILIZE_INTERVAL = 200;
    static const int MAX_RETRIES = 10;
    static const int WPS_SETTLE_INTERVAL = 700;  // WPS staged clipboard settle window in ms
    static const int IMAGE_SETTLE_INTERVAL = 120;  // Lazy image payload settle window in ms
    static const int DUPLICATE_CAPTURE_WINDOW_MS = 900;
};


#endif //MPASTE_CLIPBOARDMONITOR_H
