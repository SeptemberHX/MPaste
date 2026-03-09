// input: 濠电偞鎸荤喊宥囨崲閸℃瑧鐭夐柛鈩冪憿閸嬫捇鎮介崹顐㈡畬缂備降鍔屽璺侯嚗閸曨厸鍋撳☉娅虫垿鎮楅娴庡綊鎮╅搹鐟扮墯闂侀€炲苯鍘搁柛灞藉祿 闂備礁鎼悧鍡欑矓鐎涙ɑ鍙忛柣鏂挎憸閳绘棃骞栨潏鍓х瘈闁瑰壊鍓熼弻娑樷枎閹邦剦妫嗛梻渚囧枛椤曨厾妲愰幒妤佸亜閻犲洦褰冮獮瀣⒑閸涘﹥鈷愮紒瀣箻閸┾偓?
// output: 闂佽娴烽弫鎼佸箠閹惧嚢鍥ㄧ節濮橆剛顔岄梺褰掑亰娴滅偞娼?ClipboardMonitor 闂備焦鐪归崝宀€鈧凹鍓氱€靛ジ骞囬弶璺槯閻庣懓瀚竟鍡涘级娴犲鐓熸繝濠傛閻掍粙鏌?
// pos: utils 闂佽绻掗崑娑㈠磹閻戣姤鍤堥柟杈鹃檮閸?ClipboardMonitor 闂佽楠稿﹢閬嶅磻閻旇偐宓侀柛銉墮濡﹢鏌涢妷顖炴妞ゆ劒绮欓弻?
// update: 濠电偞鍨堕幐鎾磻閹剧粯鐓涢柛鎰健濡绢噣鏌涢妸锔剧煂闁诡噮鍣ｉ、鏇㈡晲閸℃瑥鍤遍梻浣告惈閸婂憡鎯斿鍛潟婵犻潧顑呯粈澶愭煃閵夈儳锛嶇紒鐘冲灴閺岋繝宕惰閹界娀鏌＄仦鏂よ含妤犵偛顑夐獮瀣偐瀹曞洦娈搁柣搴㈩問閸犳帡宕戦幘鎰佺唵閻犻缚娅ｉ幗鐘绘煛娴ｅ搫浠遍柡灞芥湰缁绘繆绠涢弴鐙€妲峰┑鐐差嚟婵即宕曢崡鐑嗗殨闁绘垼妫勭粻銉╂煃瑜滈崜娑欑閿曞倹鍊烽柛顭戝亞閺嗙娀姊洪崫鍕偓绋棵洪敐鍛瀻闁靛繆鈧磭绐為梺闈╁瘜閸樹粙鎮?README.md闂?
//
// Created by ragdoll on 2021/5/22.
//

#include "ClipboardMonitor.h"

#include "PlatformRelated.h"
#include <iostream>
#include <QClipboard>
#include <QBuffer>
#include <QGuiApplication>
#include <QMimeData>
#include <memory>
#include <QRegularExpression>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QUrl normalizeWpsImageUrl(const QString &rawUrl) {
    QString urlText = rawUrl.trimmed();
    if (urlText.startsWith(QStringLiteral("//"))) {
        urlText.prepend(QStringLiteral("https:"));
    }
    return QUrl(urlText);
}
}

QList<QUrl> buildImageFetchCandidates(const QUrl &url) {
    QList<QUrl> candidates;
    if (!url.isValid()) {
        return candidates;
    }

    candidates << url;
    if (url.host().contains(QStringLiteral("kdocs.cn"))) {
        QUrl alternate = url;
        if (url.scheme() == QStringLiteral("http")) {
            alternate.setScheme(QStringLiteral("https"));
        } else if (url.scheme() == QStringLiteral("https")) {
            alternate.setScheme(QStringLiteral("http"));
        }
        if (alternate != url) {
            candidates << alternate;
        }
    }
    return candidates;
}

void prepareImageFetchRequest(QNetworkRequest &request, const QUrl &url) {
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 MPaste/1.0"));
    request.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(8000);

    if (url.host().contains(QStringLiteral("kdocs.cn"))) {
        request.setRawHeader("Referer", "https://www.kdocs.cn/");
        request.setRawHeader("Origin", "https://www.kdocs.cn");
    }
}

static quint32 getClipboardSeqNumber() {
#ifdef Q_OS_WIN
    return GetClipboardSequenceNumber();
#else
    // Linux/macOS 婵犵數濮烽弫鎼佸磻濞戞瑥绶為柛銉墮缁€鍫熺節闂堟稒锛旈柤鏉跨仢闇夐柨婵嗙墛椤忕姴顪冮悷鏉挎毐妞ゎ叀娉曢幑鍕瑹椤栨艾澹夋繝?API闂傚倸鍊烽悞锔锯偓绗涘懐鐭欓柟杈鹃檮閸庢棃鏌ｉ幘宕囩槏婵炲樊浜滈柨銈嗕繆閵堝倸浜剧紓浣哄У婵炲﹪寮婚弴鐔风窞闁割偅绻傞‖瀣磽娴ｅ搫校闁绘搫绻濆?0
    return 0;
#endif
}

void ClipboardMonitor::clipboardChanged() {
    pendingWId_ = PlatformRelated::currActiveWindow();
    lastSeqNumber_ = getClipboardSeqNumber();
    retryCount_ = 0;
    ++captureToken_;
    wpsSettlePending_ = false;
    cancelPendingImageFetch();
    stabilizeTimer_.setInterval(STABILIZE_INTERVAL);
    stabilizeTimer_.start();
}

void ClipboardMonitor::checkAndCapture() {
#ifdef Q_OS_WIN
    quint32 currentSeq = getClipboardSeqNumber();
    if (currentSeq != lastSeqNumber_) {
        lastSeqNumber_ = currentSeq;
        retryCount_++;
        if (retryCount_ < MAX_RETRIES) {
            stabilizeTimer_.start();
            return;
        }
    }
#endif
    captureClipboard();
}

void ClipboardMonitor::captureClipboard() {
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();

    if (!hasMeaningfulContent(mimeData)) {
        return;
    }

    if (looksLikeWpsStagedClipboard(mimeData) && !wpsSettlePending_) {
        wpsSettlePending_ = true;
        stabilizeTimer_.setInterval(WPS_SETTLE_INTERVAL);
        stabilizeTimer_.start();
        return;
    }

    stabilizeTimer_.setInterval(STABILIZE_INTERVAL);
    wpsSettlePending_ = false;

    ClipboardItem immediateItem(PlatformRelated::getWindowIcon(pendingWId_), mimeData);
    if (!immediateItem.getImage().isNull()) {
        Q_EMIT clipboardUpdated(immediateItem, pendingWId_);
        return;
    }

    const QUrl wpsImageUrl = extractWpsSingleImageUrl(mimeData);
    if (!wpsImageUrl.isValid()) {
        Q_EMIT clipboardUpdated(immediateItem, pendingWId_);
        return;
    }

    fetchWpsImageAndEmit(mimeData, wpsImageUrl, pendingWId_, captureToken_);
}

ClipboardMonitor::ClipboardMonitor() {
    stabilizeTimer_.setSingleShot(true);
    stabilizeTimer_.setInterval(STABILIZE_INTERVAL);
    connect(&stabilizeTimer_, &QTimer::timeout, this, &ClipboardMonitor::checkAndCapture);

    imageFetchManager_ = new QNetworkAccessManager(this);
    imageFetchManager_->setProxy(QNetworkProxy::NoProxy);

    this->connectMonitor();
}

void ClipboardMonitor::disconnectMonitor() {
    stabilizeTimer_.stop();
    cancelPendingImageFetch();
    disconnect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
              this, &ClipboardMonitor::clipboardChanged);
}

void ClipboardMonitor::connectMonitor() {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
        this, &ClipboardMonitor::clipboardChanged);
}


bool ClipboardMonitor::hasMeaningfulContent(const QMimeData *mimeData) {
    if (!mimeData) {
        return false;
    }

    bool hasContent = mimeData->hasText() && !mimeData->text().isEmpty();
    hasContent = hasContent || mimeData->hasHtml();
    hasContent = hasContent || mimeData->hasImage();
    hasContent = hasContent || mimeData->hasUrls();
    hasContent = hasContent || mimeData->hasColor();
    return hasContent;
}


bool ClipboardMonitor::looksLikeWpsStagedClipboard(const QMimeData *mimeData) {
    if (!mimeData) {
        return false;
    }

    const QStringList formats = mimeData->formats();
    for (const QString &format : formats) {
        const QString lower = format.toLower();
        if (lower.contains(QStringLiteral("ksdocclipboard"))
            || lower.contains(QStringLiteral("kingsoft"))
            || lower.contains(QStringLiteral("chromium web custom mime data format"))) {
            return true;
        }
    }

    const QString html = mimeData->hasHtml() ? mimeData->html().toLower() : QString();
    if (html.contains(QStringLiteral("ksdocclipboard"))
        || html.contains(QStringLiteral("kingsoft"))
        || html.contains(QStringLiteral("from:'wps'"))
        || html.contains(QStringLiteral("from:&quot;wps&quot;"))) {
        return true;
    }

    const QString text = mimeData->hasText() ? mimeData->text().toLower() : QString();
    return text.contains(QStringLiteral("ksdocclipboard"))
        || text.contains(QStringLiteral("kingsoft"));
}

QUrl ClipboardMonitor::extractWpsSingleImageUrl(const QMimeData *mimeData) {
    if (!mimeData || !mimeData->hasHtml()) {
        return {};
    }

    const QString html = mimeData->html();
    const QString lowerHtml = html.toLower();
    if (!lowerHtml.contains(QStringLiteral("<img"))) {
        return {};
    }
    if (!lowerHtml.contains(QStringLiteral("ksdocclipboard"))
        && !lowerHtml.contains(QStringLiteral("kingsoft"))
        && !lowerHtml.contains(QStringLiteral("from:'wps'"))
        && !lowerHtml.contains(QStringLiteral("from:&quot;wps&quot;"))) {
        return {};
    }

    static const QRegularExpression srcRegex(
        QStringLiteral(R"(<img[^>]+src\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = srcRegex.match(html);
    if (!match.hasMatch()) {
        return {};
    }

    return normalizeWpsImageUrl(match.captured(1));
}

QMimeData *ClipboardMonitor::cloneMimeData(const QMimeData *mimeData) {
    if (!mimeData) {
        return nullptr;
    }

    auto *copy = new QMimeData;
    for (const QString &format : mimeData->formats()) {
        copy->setData(format, mimeData->data(format));
    }
    return copy;
}

void ClipboardMonitor::materializePng(QMimeData *mimeData, const QByteArray &pngBytes) {
    if (!mimeData || pngBytes.isEmpty()) {
        return;
    }

    mimeData->setData(QStringLiteral("application/x-qt-image"), pngBytes);
    mimeData->setData(QStringLiteral("image/png"), pngBytes);
    mimeData->setData(QStringLiteral("application/x-qt-windows-mime;value=\"PNG\""), pngBytes);
}

void ClipboardMonitor::cancelPendingImageFetch() {
    if (pendingImageFetchReply_) {
        pendingImageFetchReply_->abort();
        pendingImageFetchReply_->deleteLater();
        pendingImageFetchReply_.clear();
    }
}

void ClipboardMonitor::emitCapturedItem(const QMimeData *mimeData, int wId) {
    if (!hasMeaningfulContent(mimeData)) {
        return;
    }
    Q_EMIT clipboardUpdated(ClipboardItem(PlatformRelated::getWindowIcon(wId), mimeData), wId);
}

void ClipboardMonitor::fetchWpsImageAndEmit(const QMimeData *mimeData, const QUrl &url, int wId, quint64 captureToken) {
    if (!imageFetchManager_) {
        emitCapturedItem(mimeData, wId);
        return;
    }

    std::shared_ptr<QMimeData> mimeDataCopy(cloneMimeData(mimeData));
    if (!mimeDataCopy) {
        emitCapturedItem(mimeData, wId);
        return;
    }

    cancelPendingImageFetch();

    auto candidates = std::make_shared<QList<QUrl>>(buildImageFetchCandidates(url));
    auto startNextRequest = std::make_shared<std::function<void()>>();
    *startNextRequest = [this, mimeDataCopy, wId, captureToken, candidates, startNextRequest]() {
        if (candidates->isEmpty()) {
            emitCapturedItem(mimeDataCopy.get(), wId);
            return;
        }

        const QUrl currentUrl = candidates->takeFirst();
        QNetworkRequest request(currentUrl);
        prepareImageFetchRequest(request, currentUrl);
        pendingImageFetchReply_ = imageFetchManager_->get(request);

        connect(pendingImageFetchReply_, &QNetworkReply::finished, this,
            [this, mimeDataCopy, wId, captureToken, candidates, startNextRequest]() {
                QNetworkReply *reply = pendingImageFetchReply_;
                pendingImageFetchReply_.clear();
                if (!reply) {
                    return;
                }

                const QByteArray payload = reply->readAll();
                const bool ok = reply->error() == QNetworkReply::NoError;
                reply->deleteLater();

                if (captureToken != captureToken_) {
                    return;
                }

                if (ok) {
                    QPixmap pixmap;
                    if (pixmap.loadFromData(payload)) {
                        QByteArray pngBytes;
                        QBuffer buffer(&pngBytes);
                        if (buffer.open(QIODevice::WriteOnly) && pixmap.save(&buffer, "PNG")) {
                            materializePng(mimeDataCopy.get(), pngBytes);
                            emitCapturedItem(mimeDataCopy.get(), wId);
                            return;
                        }
                    }
                }

                (*startNextRequest)();
            });
    };

    (*startNextRequest)();
}
