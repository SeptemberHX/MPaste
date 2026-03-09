// input: 娓氭繆绂?Qt 楠炲啿褰撮幎鍊熻杽閵嗕胶閮寸紒?API 娑撳氦鐨熼悽銊︽煙婢圭増妲戦妴?
// output: 鐎电懓顦婚幓鎰返 ClipboardMonitor 閻ㄥ嫬浼愰崗閿嬪复閸欙絻鈧?
// pos: utils 鐏炲倷鑵戦惃?ClipboardMonitor 閹恒儱褰涚€规矮绠熼妴?
// update: 娑撯偓閺冿附鍨滅悮顐ｆ纯閺傚府绱濋崝鈥崇箑閺囧瓨鏌婇幋鎴犳畱瀵偓婢跺瓨鏁為柌濠忕礉娴犮儱寮烽幍鈧仦鐐垫畱閺傚洣娆㈡径鍦畱 README.md閵?
//
// Created by ragdoll on 2021/5/22.
//

#ifndef MPASTE_CLIPBOARDMONITOR_H
#define MPASTE_CLIPBOARDMONITOR_H

#include <QObject>
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

signals:
    void clipboardUpdated(ClipboardItem item, int wId);

public slots:
    void clipboardChanged();

private:
    void checkAndCapture();
    void captureClipboard();
    void cancelPendingImageFetch();
    void emitCapturedItem(const QMimeData *mimeData, int wId);
    void fetchWpsImageAndEmit(const QMimeData *mimeData, const QUrl &url, int wId, quint64 captureToken);

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

    static const int STABILIZE_INTERVAL = 200;  // 濠殿噯绲界换鎴︻敃閻撳宫娑㈠焵椤掑嫬钃熼柕澶嗘櫆閿涚喖姊?ms
    static const int MAX_RETRIES = 10;           // 闂佸搫鐗冮崑鎾愁熆閼稿灚鐨戦柣鈩冨灥椤?2 缂?
    static const int WPS_SETTLE_INTERVAL = 700;  // WPS staged clipboard settle window in ms
};


#endif //MPASTE_CLIPBOARDMONITOR_H
