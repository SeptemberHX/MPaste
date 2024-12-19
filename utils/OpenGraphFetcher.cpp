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

    // 更新 favicon 正则表达式，使其更通用
    this->faviconReg1.setPattern(R"(<link[^>]*(?:rel=["'](?:[^"']*\s)?(?:icon|shortcut)(?:\s[^"']*)?["'][^>]*href=["']([^"']+)["']|href=["']([^"']+)["'][^>]*rel=["'](?:[^"']*\s)?(?:icon|shortcut)(?:\s[^"']*)?["'])[^>]*>)");

    // 可以再添加一个专门匹配 type="image/x-icon" 的正则
    this->faviconReg2.setPattern(R"(<link[^>]*type=["']image/(?:x-icon|vnd\.microsoft\.icon)["'][^>]*href=["']([^"']+)["'][^>]*>)");

    // 更新正则表达式以适应Qt6的QRegularExpression语法
    this->ogImageReg.setPattern("<meta .*?property[ ]*=[ ]*\"og:image\"[ ]*content[ ]*=[ ]*\"(.*?)\"[ ]*/?>");
    this->ogTitleReg.setPattern("<meta .*?property[ ]*=[ ]*\"og:title\"[ ]*content[ ]*=[ ]*\"(.*?)\"[ ]*/?>");
    this->titleReg.setPattern("<title.*?>(.*?)</title>");


    // QRegularExpression默认就是最小匹配（非贪婪模式），使用 *? 代替 *
    
    this->naManager = new QNetworkAccessManager(this);
    connect(this->naManager, &QNetworkAccessManager::finished, this, &OpenGraphFetcher::requestFinished);
}

void OpenGraphFetcher::handle() {
    QUrl tUrl = this->targetUrl;
    if (this->replaceHttpHosts.contains(this->targetUrl.host())) {
        tUrl = QUrl(tUrl.toString().replace("https://", "http://"));
    }

    QNetworkRequest request(tUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:88.0) Gecko/20100101 Firefox/88.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    this->realCalledUrl = tUrl;
    this->naManager->get(request);
}

void OpenGraphFetcher::requestFinished(QNetworkReply *reply) {
if (reply->error() != QNetworkReply::NoError) {
        if (!pendingImageUrls.isEmpty()) {
            tryNextImage();
            return;
        }
        Q_EMIT finished(this->ogItem);
        return;
    }

    if (reply->request().url() == this->realCalledUrl) {
        // HTML 内容处理
        QDomDocument doc;
        QString body(reply->readAll());

        // 收集所有可能的图片URL
        pendingImageUrls.clear();

        if (doc.setContent(body)) {
            // 1. 尝试 og:image
            QDomNodeList metaNodes = doc.elementsByTagName("meta");
            for (int i = 0; i < metaNodes.size(); ++i) {
                QDomElement meta = metaNodes.at(i).toElement();
                if (meta.attribute("property") == "og:image" ||
                    meta.attribute("property") == "twitter:image") {
                    pendingImageUrls << meta.attribute("content");
                }
            }

            // 2. 尝试 link 标签中的图标
            QDomNodeList linkNodes = doc.elementsByTagName("link");
            for (int i = 0; i < linkNodes.size(); ++i) {
                QDomElement link = linkNodes.at(i).toElement();
                QString rel = link.attribute("rel").toLower();
                QString type = link.attribute("type").toLower();
                QString href = link.attribute("href");

                // 检查所有可能的 favicon 相关属性
                if ((rel.contains("icon") ||
                     type == "image/x-icon" ||
                     type == "image/vnd.microsoft.icon") &&
                    !href.isEmpty() && !href.startsWith("$")) {
                    pendingImageUrls << href;
                    }
            }

            // 处理标题...
            QDomNodeList titleNodes = doc.elementsByTagName("title");
            if (!titleNodes.isEmpty()) {
                this->ogItem.setTitle(titleNodes.at(0).toElement().text());
            }
        } else {
            // 使用正则表达式提取信息
            auto processFaviconMatch = [this](const QRegularExpressionMatch& match) {
                QString url = match.captured(1);
                if (!url.isEmpty()) {
                    pendingImageUrls << url;
                }
                // 检查第二个捕获组（针对 faviconReg1 的第二种模式）
                url = match.captured(2);
                if (!url.isEmpty()) {
                    pendingImageUrls << url;
                }
            };

            QRegularExpressionMatch match = this->faviconReg1.match(body);
            if (match.hasMatch()) {
                processFaviconMatch(match);
            }

            match = this->faviconReg2.match(body);
            if (match.hasMatch()) {
                processFaviconMatch(match);
            }

            // 处理标题...
            match = this->titleReg.match(body);
            if (match.hasMatch()) {
                this->ogItem.setTitle(match.captured(1));
            }
        }

        // 3. 添加常见的高质量图标路径
        for (const QString &path : getHighQualityIconPaths()) {
            pendingImageUrls << (targetUrl.toString() + path);
        }

        // 开始尝试获取图片
        tryNextImage();

    } else {
        // 处理图片请求的响应
        QPixmap image;
        if (image.loadFromData(reply->readAll())) {
            // 检查图片质量
            if (image.width() >= 32 && image.height() >= 32) {
                this->ogItem.setImage(image);
                Q_EMIT finished(this->ogItem);
                return;
            }
        }

        // 如果这个图片不合适，尝试下一个
        if (!pendingImageUrls.isEmpty()) {
            tryNextImage();
        } else {
            Q_EMIT finished(this->ogItem);
        }
    }
}

void OpenGraphFetcher::tryNextImage() {
    if (pendingImageUrls.isEmpty()) {
        Q_EMIT finished(this->ogItem);
        return;
    }

    QString imageUrl = pendingImageUrls.takeFirst();
    if (!imageUrl.startsWith("http")) {
        if (imageUrl.startsWith("//")) {
            imageUrl = "https:" + imageUrl;
        } else {
            imageUrl = targetUrl.resolved(QUrl(imageUrl)).toString();
        }
    }

    QNetworkRequest request{QUrl(imageUrl)};
    request.setRawHeader("User-Agent",
        "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:88.0) Gecko/20100101 Firefox/88.0");
    this->naManager->get(request);
}
