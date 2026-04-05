// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 ClipboardMonitor 的实现逻辑。
// pos: utils 层中的 ClipboardMonitor 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
// note: Adds image payload settle retries for lazy clipboard providers.
//
// Created by ragdoll on 2021/5/22.
//

#include "ClipboardMonitor.h"

#include "PlatformRelated.h"
#include <iostream>
#include <QClipboard>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
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

QByteArray captureKeyForItem(const ClipboardItem &item) {
    QCryptographicHash hash(QCryptographicHash::Sha1);

    const QList<QUrl> urls = item.getNormalizedUrls();
    if (!urls.isEmpty()) {
        hash.addData(QByteArrayLiteral("urls\n"));
        for (const QUrl &url : urls) {
            hash.addData(url.toString(QUrl::FullyEncoded).toUtf8());
            hash.addData(QByteArrayLiteral("\n"));
        }
        return hash.result();
    }

    const QString normalizedText = item.getNormalizedText().trimmed();
    if (!normalizedText.isEmpty()) {
        hash.addData(QByteArrayLiteral("text\n"));
        hash.addData(normalizedText.simplified().toUtf8());
        return hash.result();
    }

    const QColor color = item.getColor();
    if (color.isValid()) {
        hash.addData(QByteArrayLiteral("color\n"));
        hash.addData(QByteArray::number(static_cast<quint32>(color.rgba())));
        return hash.result();
    }

    const QByteArray imageBytes = item.imagePayloadBytesFast();
    if (!imageBytes.isEmpty()) {
        hash.addData(QByteArrayLiteral("image\n"));
        hash.addData(imageBytes);
        return hash.result();
    }

    const QString html = item.getHtml().trimmed();
    if (!html.isEmpty()) {
        hash.addData(QByteArrayLiteral("html\n"));
        hash.addData(ClipboardItem::htmlFragment(html).toString().simplified().toUtf8());
        return hash.result();
    }

    hash.addData(QByteArrayLiteral("fallback\n"));
    hash.addData(item.fingerprint());
    return hash.result();
}

QString shortHex(const QByteArray &bytes, int chars = 12) {
    return QString::fromLatin1(bytes.toHex().left(chars));
}

QString elideForLog(QString text, int maxLen = 48) {
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    if (text.size() > maxLen) {
        text.truncate(maxLen);
        text.append(QStringLiteral("..."));
    }
    return text;
}

const char *contentTypeName(ContentType type) {
    switch (type) {
        case All: return "All";
        case Text: return "Text";
        case Link: return "Link";
        case Image: return "Image";
        case RichText: return "RichText";
        case File: return "File";
        case Color: return "Color";
        case Office: return "Office";
    }
    return "Unknown";
}

QString mimeSummary(const QMimeData *mimeData) {
    if (!mimeData) {
        return QStringLiteral("mime=null");
    }

    const QStringList formats = mimeData->formats();
    const QString textPreview = mimeData->hasText() ? elideForLog(mimeData->text()) : QString();
    return QStringLiteral("formats=%1 hasText=%2 textLen=%3 text=\"%4\" hasHtml=%5 htmlLen=%6 hasImage=%7 hasUrls=%8 urlCount=%9 hasColor=%10")
        .arg(formats.join(QStringLiteral(",")))
        .arg(mimeData->hasText())
        .arg(mimeData->hasText() ? mimeData->text().size() : 0)
        .arg(textPreview)
        .arg(mimeData->hasHtml())
        .arg(mimeData->hasHtml() ? mimeData->html().size() : 0)
        .arg(mimeData->hasImage())
        .arg(mimeData->hasUrls())
        .arg(mimeData->hasUrls() ? mimeData->urls().size() : 0)
        .arg(mimeData->hasColor());
}

QString itemSummary(const ClipboardItem &item) {
    const QString normalizedText = elideForLog(item.getNormalizedText());
    const QList<QUrl> urls = item.getNormalizedUrls();
    return QStringLiteral("type=%1 fp=%2 key=%3 text=\"%4\" urlCount=%5 image=%6x%7 htmlLen=%8")
        .arg(QString::fromLatin1(contentTypeName(item.getContentType())))
        .arg(shortHex(item.fingerprint()))
        .arg(shortHex(captureKeyForItem(item)))
        .arg(normalizedText)
        .arg(urls.size())
        .arg(item.getImagePixelSize().isValid() ? item.getImagePixelSize().width() : 0)
        .arg(item.getImagePixelSize().isValid() ? item.getImagePixelSize().height() : 0)
        .arg(item.getHtml().size());
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

void ClipboardMonitor::beginClipboardCapture(bool emitActivitySignal) {
    pendingWId_ = PlatformRelated::currActiveWindow();
    lastSeqNumber_ = getClipboardSeqNumber();
    retryCount_ = 0;
    ++captureToken_;
    wpsSettlePending_ = false;
    cancelPendingImageFetch();
    stabilizeTimer_.setInterval(STABILIZE_INTERVAL);
    stabilizeTimer_.start();
    qInfo().noquote() << QStringLiteral("[clipboard-monitor] dataChanged token=%1 wId=%2 seq=%3")
        .arg(captureToken_)
        .arg(pendingWId_)
        .arg(lastSeqNumber_);

    if (emitActivitySignal) {
        const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
        if (hasMeaningfulContent(mimeData)) {
            qInfo().noquote() << QStringLiteral("[clipboard-monitor] early activity signal token=%1 wId=%2")
                .arg(captureToken_)
                .arg(pendingWId_);
            Q_EMIT clipboardActivityObserved(pendingWId_);
        }
    }
}

void ClipboardMonitor::clipboardChanged() {
    beginClipboardCapture(true);
}

void ClipboardMonitor::primeCurrentClipboard() {
    qInfo() << "[clipboard-monitor] primeCurrentClipboard";
    beginClipboardCapture(false);
}

void ClipboardMonitor::checkAndCapture() {
#ifdef Q_OS_WIN
    quint32 currentSeq = getClipboardSeqNumber();
    if (currentSeq != lastSeqNumber_) {
        qInfo().noquote() << QStringLiteral("[clipboard-monitor] seq changed during stabilize token=%1 oldSeq=%2 newSeq=%3 retry=%4/%5")
            .arg(captureToken_)
            .arg(lastSeqNumber_)
            .arg(currentSeq)
            .arg(retryCount_ + 1)
            .arg(MAX_RETRIES);
        lastSeqNumber_ = currentSeq;
        retryCount_++;
        if (retryCount_ < MAX_RETRIES) {
            stabilizeTimer_.start();
            return;
        }
    }
#endif
    qInfo().noquote() << QStringLiteral("[clipboard-monitor] captureClipboard token=%1 wId=%2")
        .arg(captureToken_)
        .arg(pendingWId_);
    captureClipboard();
}

void ClipboardMonitor::captureClipboard() {
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
    qInfo().noquote() << QStringLiteral("[clipboard-monitor] mime snapshot token=%1 %2")
        .arg(captureToken_)
        .arg(mimeSummary(mimeData));

    if (!hasMeaningfulContent(mimeData)) {
        qInfo().noquote() << QStringLiteral("[clipboard-monitor] ignore empty clipboard token=%1").arg(captureToken_);
        return;
    }

    if (looksLikeWpsStagedClipboard(mimeData) && !wpsSettlePending_) {
        wpsSettlePending_ = true;
        stabilizeTimer_.setInterval(WPS_SETTLE_INTERVAL);
        stabilizeTimer_.start();
        qInfo().noquote() << QStringLiteral("[clipboard-monitor] staged clipboard detected token=%1 settleMs=%2")
            .arg(captureToken_)
            .arg(WPS_SETTLE_INTERVAL);
        return;
    }

    stabilizeTimer_.setInterval(STABILIZE_INTERVAL);
    wpsSettlePending_ = false;

    ClipboardItem immediateItem = ClipboardItem::createLightweight(PlatformRelated::getWindowIcon(pendingWId_), mimeData);
    if (ContentClassifier::hasFastImagePayload(mimeData)
        && immediateItem.imagePayloadBytesFast().isEmpty()
        && retryCount_ < MAX_RETRIES) {
        retryCount_++;
        stabilizeTimer_.setInterval(IMAGE_SETTLE_INTERVAL);
        stabilizeTimer_.start();
        qInfo().noquote() << QStringLiteral("[clipboard-monitor] image payload pending token=%1 retry=%2/%3")
            .arg(captureToken_)
            .arg(retryCount_)
            .arg(MAX_RETRIES);
        return;
    }
    if (!immediateItem.imagePayloadBytesFast().isEmpty()) {
        qInfo().noquote() << QStringLiteral("[clipboard-monitor] immediate image item token=%1 type=%2 name=%3")
            .arg(captureToken_)
            .arg(QString::fromLatin1(contentTypeName(immediateItem.getContentType())))
            .arg(immediateItem.getName());
        emitCapturedItem(immediateItem, pendingWId_);
        if (hasDeferrableMimeFormats(mimeData)) {
            scheduleDeferredMimeCapture(immediateItem.getName());
        }
        return;
    }

    const QUrl wpsImageUrl = extractWpsSingleImageUrl(mimeData);
    if (!wpsImageUrl.isValid()) {
        qInfo().noquote() << QStringLiteral("[clipboard-monitor] immediate non-image item token=%1 type=%2 name=%3")
            .arg(captureToken_)
            .arg(QString::fromLatin1(contentTypeName(immediateItem.getContentType())))
            .arg(immediateItem.getName());
        emitCapturedItem(immediateItem, pendingWId_);
        if (hasDeferrableMimeFormats(mimeData)) {
            scheduleDeferredMimeCapture(immediateItem.getName());
        }
        return;
    }

    qInfo().noquote() << QStringLiteral("[clipboard-monitor] html image needs fetch token=%1 url=%2")
        .arg(captureToken_)
        .arg(wpsImageUrl.toString());
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
    qInfo() << "[clipboard-monitor] disconnected";
}

void ClipboardMonitor::connectMonitor() {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
        this, &ClipboardMonitor::clipboardChanged, Qt::UniqueConnection);
    qInfo() << "[clipboard-monitor] connected";
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

    // Check for OLE/vector/MathML formats that lack standard MIME types
    // (e.g. MathType clipboard with MetaFilePict + Embed Source only).
    if (!hasContent) {
        const ContentClassifier::ContentTraits traits = ContentClassifier::analyze(mimeData);
        hasContent = traits.hasVector || traits.hasOle;
    }

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
    if (mimeData->hasText()) {
        copy->setText(mimeData->text());
    }
    if (mimeData->hasHtml()) {
        copy->setHtml(mimeData->html());
    }
    if (mimeData->hasUrls()) {
        copy->setUrls(mimeData->urls());
    }
    if (mimeData->hasColor()) {
        copy->setColorData(mimeData->colorData());
    }

    for (const QString &format : mimeData->formats()) {
        const QString lower = format.toLower();
        if (!(lower.startsWith(QStringLiteral("text/"))
              || lower.contains(QStringLiteral("html"))
              || lower.contains(QStringLiteral("plain"))
              || lower.contains(QStringLiteral("xml"))
              || lower.contains(QStringLiteral("json"))
              || lower.contains(QStringLiteral("url"))
              || lower.contains(QStringLiteral("uri"))
              || lower.contains(QStringLiteral("descriptor"))
              || lower.contains(QStringLiteral("ksdocclipboard"))
              || lower.contains(QStringLiteral("rich text")))) {
            continue;
        }

        const QByteArray data = mimeData->data(format);
        if (!data.isEmpty()) {
            copy->setData(format, data);
        }
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

bool ClipboardMonitor::isDuplicateRecentCapture(const ClipboardItem &item, int wId) const {
    if (lastCaptureAtMs_ <= 0) {
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastCaptureAtMs_ > DUPLICATE_CAPTURE_WINDOW_MS) {
        return false;
    }

    if (wId != 0 && lastCaptureWindowId_ != 0 && wId != lastCaptureWindowId_) {
        return false;
    }

    return captureKeyForItem(item) == lastCaptureKey_;
}

void ClipboardMonitor::rememberCapturedItem(const ClipboardItem &item, int wId) {
    lastCaptureKey_ = captureKeyForItem(item);
    lastCaptureWindowId_ = wId;
    lastCaptureAtMs_ = QDateTime::currentMSecsSinceEpoch();
}

void ClipboardMonitor::emitCapturedItem(const ClipboardItem &item, int wId) {
    // Compute captureKey once and reuse for dup-check, remember, and logging.
    const QByteArray key = captureKeyForItem(item);

    if (lastCaptureAtMs_ > 0) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastCaptureAtMs_ <= DUPLICATE_CAPTURE_WINDOW_MS
            && !(wId != 0 && lastCaptureWindowId_ != 0 && wId != lastCaptureWindowId_)
            && key == lastCaptureKey_) {
            qInfo().noquote() << QStringLiteral("[clipboard-monitor] suppress duplicate app event wId=%1 key=%2")
                .arg(wId)
                .arg(shortHex(key));
            return;
        }
    }

    lastCaptureKey_ = key;
    lastCaptureWindowId_ = wId;
    lastCaptureAtMs_ = QDateTime::currentMSecsSinceEpoch();

    qInfo().noquote() << QStringLiteral("[clipboard-monitor] emit app event wId=%1 type=%2 fp=%3 key=%4")
        .arg(wId)
        .arg(QString::fromLatin1(contentTypeName(item.getContentType())))
        .arg(shortHex(item.fingerprint()))
        .arg(shortHex(key));
    Q_EMIT clipboardUpdated(item, wId);
}

bool ClipboardMonitor::hasDeferrableMimeFormats(const QMimeData *mimeData) const {
    if (!mimeData) {
        return false;
    }
    static const QStringList alreadyCaptured = {
        QStringLiteral("text/plain"),
        QStringLiteral("text/html"),
        QStringLiteral("text/uri-list"),
    };
    for (const QString &format : mimeData->formats()) {
        if (alreadyCaptured.contains(format)) {
            continue;
        }
        if (ClipboardItem::shouldCopyExtraMimeFormat(format)) {
            return true;
        }
    }
    return false;
}

void ClipboardMonitor::scheduleDeferredMimeCapture(const QString &itemName) {
    const quint64 token = captureToken_;
    const quint32 seq = lastSeqNumber_;
    // Read extra MIME formats from the live clipboard.  This involves
    // cross-process IPC to the source application and can block for
    // seconds if the app is busy, so run it off the main thread.
    QTimer::singleShot(0, this, [this, token, seq, itemName]() {
        if (captureToken_ != token) {
            return;
        }
#ifdef Q_OS_WIN
        if (getClipboardSeqNumber() != seq) {
            return;
        }
#endif
        const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
        if (!mimeData) {
            return;
        }

        QMap<QString, QByteArray> extraFormats;
        const QStringList alreadyCaptured = {
            QStringLiteral("text/plain"),
            QStringLiteral("text/html"),
            QStringLiteral("text/uri-list"),
        };
        for (const QString &format : mimeData->formats()) {
            if (alreadyCaptured.contains(format)
                || !ClipboardItem::shouldCopyExtraMimeFormat(format)) {
                continue;
            }
            const QByteArray data = mimeData->data(format);
            if (!data.isEmpty()) {
                extraFormats.insert(format, data);
            }
        }

        if (!extraFormats.isEmpty()) {
            qInfo().noquote() << QStringLiteral("[clipboard-monitor] deferred mime capture name=%1 formatCount=%2")
                .arg(itemName)
                .arg(extraFormats.size());
            Q_EMIT clipboardMimeCompleted(itemName, extraFormats);
        }
    });
}

void ClipboardMonitor::fetchWpsImageAndEmit(const QMimeData *mimeData, const QUrl &url, int wId, quint64 captureToken) {
    if (!imageFetchManager_) {
        qInfo().noquote() << QStringLiteral("[clipboard-monitor] no network manager, emit immediate token=%1").arg(captureToken);
        emitCapturedItem(ClipboardItem::createLightweight(PlatformRelated::getWindowIcon(wId), mimeData), wId);
        return;
    }

    std::shared_ptr<QMimeData> mimeDataCopy(cloneMimeData(mimeData));
    if (!mimeDataCopy) {
        qInfo().noquote() << QStringLiteral("[clipboard-monitor] clone mime failed, emit immediate token=%1").arg(captureToken);
        emitCapturedItem(ClipboardItem::createLightweight(PlatformRelated::getWindowIcon(wId), mimeData), wId);
        return;
    }

    cancelPendingImageFetch();

    auto candidates = std::make_shared<QList<QUrl>>(buildImageFetchCandidates(url));
    auto startNextRequest = std::make_shared<std::function<void()>>();
    *startNextRequest = [this, mimeDataCopy, wId, captureToken, candidates, startNextRequest]() {
        if (candidates->isEmpty()) {
            qInfo().noquote() << QStringLiteral("[clipboard-monitor] image fetch exhausted token=%1, emit fallback").arg(captureToken);
            emitCapturedItem(ClipboardItem::createLightweight(PlatformRelated::getWindowIcon(wId), mimeDataCopy.get()), wId);
            return;
        }

        const QUrl currentUrl = candidates->takeFirst();
        qInfo().noquote() << QStringLiteral("[clipboard-monitor] image fetch start token=%1 url=%2").arg(captureToken).arg(currentUrl.toString());
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
                const QString errorText = reply->errorString();
                reply->deleteLater();

                if (captureToken != captureToken_) {
                    qInfo().noquote() << QStringLiteral("[clipboard-monitor] ignore stale fetch result token=%1 currentToken=%2")
                        .arg(captureToken)
                        .arg(captureToken_);
                    return;
                }

                if (ok) {
                    QPixmap pixmap;
                    if (pixmap.loadFromData(payload)) {
                        QByteArray pngBytes;
                        QBuffer buffer(&pngBytes);
                        if (buffer.open(QIODevice::WriteOnly) && pixmap.save(&buffer, "PNG")) {
                            qInfo().noquote() << QStringLiteral("[clipboard-monitor] image fetch success token=%1 bytes=%2 size=%3x%4")
                                .arg(captureToken)
                                .arg(payload.size())
                                .arg(pixmap.width())
                                .arg(pixmap.height());
                            materializePng(mimeDataCopy.get(), pngBytes);
                            emitCapturedItem(ClipboardItem::createLightweight(PlatformRelated::getWindowIcon(wId), mimeDataCopy.get()), wId);
                            return;
                        }
                    }
                }

                qInfo().noquote() << QStringLiteral("[clipboard-monitor] image fetch failed token=%1 ok=%2 bytes=%3 error=%4")
                    .arg(captureToken)
                    .arg(ok)
                    .arg(payload.size())
                    .arg(errorText);

                (*startNextRequest)();
            });
    };

    (*startNextRequest)();
}
