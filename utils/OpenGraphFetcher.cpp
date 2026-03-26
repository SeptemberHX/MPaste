// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 OpenGraphFetcher 的实现逻辑。
// pos: utils 层中的 OpenGraphFetcher 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。 (image kind)
#include "OpenGraphFetcher.h"
#include <QNetworkReply>
#include <iostream>
#include <QImageReader>
#include <QPixmap>
#include <QDomDocument>
#include <QNetworkProxy>

OpenGraphFetcher::OpenGraphFetcher(const QUrl &url, QObject *parent)
    : QObject(parent)
    , targetUrl(url)
{
    this->replaceHttpHosts << "www.baidu.com";

    // 更新 favicon 正则表达式，提高通用性
    this->faviconReg1.setPattern(R"(<link[^>]*(?:rel=["'](?:[^"']*\s)?(?:icon|shortcut)(?:\s[^"']*)?["'][^>]*href=["']([^"']+)["']|href=["']([^"']+)["'][^>]*rel=["'](?:[^"']*\s)?(?:icon|shortcut)(?:\s[^"']*)?["'])[^>]*>)");

    // 可按需额外匹配 type="image/x-icon" 的链接标签
    this->faviconReg2.setPattern(R"(<link[^>]*type=["']image/(?:x-icon|vnd\.microsoft\.icon)["'][^>]*href=["']([^"']+)["'][^>]*>)");

    // 调整正则写法以兼容 Qt 6 的 QRegularExpression
    this->ogImageReg.setPattern(
        R"(<meta[^>]*(?=(?:[^>]*(?:property|name)\s*=\s*["'](?:og:image|og:image:secure_url|twitter:image|twitter:image:src)["']))(?=(?:[^>]*content\s*=\s*["']([^"']+)["']))[^>]*>)");
    this->ogTitleReg.setPattern(
        R"(<meta[^>]*(?=(?:[^>]*(?:property|name)\s*=\s*["'](?:og:title|twitter:title)["']))(?=(?:[^>]*content\s*=\s*["']([^"']+)["']))[^>]*>)");
    this->titleReg.setPattern("<title.*?>(.*?)</title>");


    // QRegularExpression 默认使用最小匹配，这里用 *? 避免贪婪匹配
    
    this->naManager = new QNetworkAccessManager(this);
    connect(this->naManager, &QNetworkAccessManager::finished, this, &OpenGraphFetcher::requestFinished);
}

void OpenGraphFetcher::appendPendingImage(const QString &url, OpenGraphItem::ImageKind kind) {
    if (url.isEmpty()) {
        return;
    }
    PendingImage pending;
    pending.url = url;
    pending.kind = kind;
    pendingImages.append(pending);
}

void OpenGraphFetcher::handle() {
    QUrl tUrl = this->targetUrl;
    if (this->replaceHttpHosts.contains(this->targetUrl.host())) {
        tUrl = QUrl(tUrl.toString().replace("https://", "http://"));
    }

    QNetworkRequest request(tUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:88.0) Gecko/20100101 Firefox/88.0");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    this->realCalledUrl = tUrl;
    this->naManager->get(request);
}

void OpenGraphFetcher::requestFinished(QNetworkReply *reply) {
    reply->deleteLater();

    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    const QByteArray contentEncoding = reply->rawHeader("Content-Encoding");
    const bool isHtmlReply = contentType.contains(QStringLiteral("text/html"))
        || contentType.contains(QStringLiteral("application/xhtml"));
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();

    const QByteArray payloadHead = payload.left(4096).toLower();
    const bool looksLikeHtml = payloadHead.contains("<meta")
        || payloadHead.contains("<html")
        || payloadHead.contains("<!doctype");

    if (reply->error() != QNetworkReply::NoError) {
        if (!pendingImages.isEmpty()) {
            tryNextImage();
            return;
        }
        Q_EMIT finished(this->ogItem);
        return;
    }

    if (isHtmlReply || looksLikeHtml || reply->request().url() == this->realCalledUrl) {
        QDomDocument doc;
        const QString body = QString::fromUtf8(payload);

        pendingImages.clear();
        currentImageKind = OpenGraphItem::ImageUnknown;

        const bool docOk = doc.setContent(body, true);
        const bool looksGzip = payload.size() >= 2
            && static_cast<unsigned char>(payload[0]) == 0x1f
            && static_cast<unsigned char>(payload[1]) == 0x8b;
        if (docOk) {
            // 1. 先处理 meta 标签，提取 og:title 和 og:image
            QDomNodeList metaNodes = doc.elementsByTagName("meta");
            bool foundTitle = false;
            for (int i = 0; i < metaNodes.size(); ++i) {
                QDomElement meta = metaNodes.at(i).toElement();

                const QString metaProperty = meta.attribute("property").toLower();
                const QString metaName = meta.attribute("name").toLower();

                // 处理标题
                if (!foundTitle && (metaProperty == "og:title"
                    || metaProperty == "twitter:title"
                    || metaName == "og:title"
                    || metaName == "twitter:title")) {
                    QString content = meta.attribute("content");
                    if (!content.isEmpty()) {
                        this->ogItem.setTitle(content);
                        foundTitle = true;
                    }
                }

                // 处理图片
                if (metaProperty == "og:image"
                    || metaProperty == "og:image:secure_url"
                    || metaProperty == "twitter:image"
                    || metaProperty == "twitter:image:src"
                    || metaName == "og:image"
                    || metaName == "og:image:secure_url"
                    || metaName == "twitter:image"
                    || metaName == "twitter:image:src") {
                    QString content = meta.attribute("content");
                    if (!content.isEmpty()) {
                        appendPendingImage(content, OpenGraphItem::PreviewImage);
                    }
                }
            }

            // DOM 没抓到时用正则兜底一次
            if (pendingImages.isEmpty()) {
                QRegularExpressionMatch ogMatch = this->ogImageReg.match(body);
                if (ogMatch.hasMatch()) {
                    const QString url = ogMatch.captured(1);
                    if (!url.isEmpty()) {
                        appendPendingImage(url, OpenGraphItem::PreviewImage);
                    }
                }
            }
            if (!foundTitle) {
                QRegularExpressionMatch titleMatch = this->ogTitleReg.match(body);
                if (titleMatch.hasMatch()) {
                    QString content = titleMatch.captured(1);
                    if (content.isEmpty()) {
                        content = titleMatch.captured(2);
                    }
                    if (!content.isEmpty()) {
                        this->ogItem.setTitle(content);
                        foundTitle = true;
                    }
                }
            }

            // 如果没找到 og:title，使用普通标题
            if (!foundTitle) {
                QDomNodeList titleNodes = doc.elementsByTagName("title");
                if (!titleNodes.isEmpty()) {
                    QString titleText = titleNodes.at(0).toElement().text().trimmed();
                    if (!titleText.isEmpty()) {
                        this->ogItem.setTitle(titleText);
                    }
                }
            }

            // 2. 其次选择正文第一张合适的图片
            if (pendingImages.isEmpty()) {
                QDomNodeList imgNodes = doc.elementsByTagName("img");
                for (int i = 0; i < imgNodes.size(); ++i) {
                    QDomElement img = imgNodes.at(i).toElement();
                    QString src = img.attribute("src");

                    // 跳过明显的小图标、广告等
                    if (!src.isEmpty() &&
                        !src.contains("avatar", Qt::CaseInsensitive) &&
                        !src.contains("logo", Qt::CaseInsensitive) &&
                        !src.contains("icon", Qt::CaseInsensitive) &&
                        !src.contains("ad", Qt::CaseInsensitive)) {
                        appendPendingImage(src, OpenGraphItem::PreviewImage);
                        break;  // 只取第一张合适的图片
                    }
                }
            }

            // 3. 最后选择 favicon
            if (pendingImages.isEmpty()) {
                QDomNodeList linkNodes = doc.elementsByTagName("link");
                for (int i = 0; i < linkNodes.size(); ++i) {
                    QDomElement link = linkNodes.at(i).toElement();
                    QString rel = link.attribute("rel").toLower();
                    QString type = link.attribute("type").toLower();
                    QString href = link.attribute("href");

                    if ((rel.contains("icon") ||
                         type == "image/x-icon" ||
                         type == "image/vnd.microsoft.icon") &&
                        !href.isEmpty() && !href.startsWith("$")) {
                        appendPendingImage(href, OpenGraphItem::IconImage);
                    }
                }
            }

        } else {
            // 使用正则表达式处理
            // 先尝试 og:title
            QRegularExpressionMatch match = this->ogTitleReg.match(body);
            if (match.hasMatch()) {
                QString content = match.captured(1);
                if (content.isEmpty()) {
                    content = match.captured(2);
                }
                if (!content.isEmpty()) {
                    this->ogItem.setTitle(content);
                }
            } else {
                // 如果没有 og:title，尝试普通标题
                match = this->titleReg.match(body);
                if (match.hasMatch()) {
                    this->ogItem.setTitle(match.captured(1));
                }
            }

            // 正则表达式提取 og:image
            match = this->ogImageReg.match(body);
            if (match.hasMatch()) {
                QString url = match.captured(1);
                if (!url.isEmpty()) {
                    appendPendingImage(url, OpenGraphItem::PreviewImage);
                }
            }

            // 如果没有 og:image，尝试 favicon
            if (pendingImages.isEmpty()) {
                auto processFaviconMatch = [this](const QRegularExpressionMatch& match) {
                    QString url = match.captured(1);
                    if (!url.isEmpty()) {
                        appendPendingImage(url, OpenGraphItem::IconImage);
                    }
                    url = match.captured(2);
                    if (!url.isEmpty()) {
                        appendPendingImage(url, OpenGraphItem::IconImage);
                    }
                };

                match = this->faviconReg1.match(body);
                if (match.hasMatch()) {
                    processFaviconMatch(match);
                }

                match = this->faviconReg2.match(body);
                if (match.hasMatch()) {
                    processFaviconMatch(match);
                }
            }
        }

        // 4. 最后添加默认的 favicon 路径
        if (pendingImages.isEmpty()) {
            for (const QString &path : getHighQualityIconPaths()) {
                appendPendingImage(targetUrl.toString() + path, OpenGraphItem::IconImage);
            }
        }

        // 开始尝试获取图片
        tryNextImage();
    } else {
        // 处理图片请求的响应
        QPixmap image;
        if (image.loadFromData(payload)) {
            this->ogItem.setImage(image);
            this->ogItem.setImageKind(currentImageKind);
            Q_EMIT finished(this->ogItem);
            return;
        }

        // 如果这个图片加载失败，尝试下一个
        if (!pendingImages.isEmpty()) {
            tryNextImage();
        } else {
            Q_EMIT finished(this->ogItem);
        }
    }
}

void OpenGraphFetcher::tryNextImage() {
    if (pendingImages.isEmpty()) {
        Q_EMIT finished(this->ogItem);
        return;
    }

    PendingImage pending = pendingImages.takeFirst();
    currentImageKind = pending.kind;
    QString imageUrl = pending.url;
    if (!imageUrl.startsWith("http")) {
        if (imageUrl.startsWith("//")) {
            imageUrl = "https:" + imageUrl;
        } else {
            imageUrl = targetUrl.resolved(QUrl(imageUrl)).toString();
        }
    }
    {
        const QUrl parsed(imageUrl);
        const QString host = parsed.host().toLower();
        if (host.endsWith(QStringLiteral("hdslb.com"))) {
            QString path = parsed.path();
            const int atPos = path.indexOf(QLatin1Char('@'));
            if (atPos > 0) {
                QUrl normalized = parsed;
                normalized.setPath(path.left(atPos));
                normalized.setQuery(QString());
                imageUrl = normalized.toString();
            }
        }
    }

    QNetworkRequest request{QUrl(imageUrl)};
    request.setRawHeader("User-Agent",
        "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:88.0) Gecko/20100101 Firefox/88.0");
    this->naManager->get(request);
}
