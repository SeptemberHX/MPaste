// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 ClipboardMonitor 的实现逻辑。
// pos: utils 层中的 ClipboardMonitor 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
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
#include <functional>
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
