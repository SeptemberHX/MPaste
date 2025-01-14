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
    this->ogTitleReg.setPattern(R"(<meta\s+(?=(?:[^>]*\s)?property=["']og:title["'])(?=[^>]*\scontent=["']([^"']*?)["'])[^>]*>)");
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
        QDomDocument doc;
        QString body(reply->readAll());

        pendingImageUrls.clear();

        if (doc.setContent(body, true)) {
            qDebug() << "OpenGraphFetcher::requestFinished";

            // 1. 先处理 meta 标签，提取 og:title 和 og:image
            QDomNodeList metaNodes = doc.elementsByTagName("meta");
            bool foundTitle = false;
            for (int i = 0; i < metaNodes.size(); ++i) {
                QDomElement meta = metaNodes.at(i).toElement();

                // 处理标题
                if (!foundTitle && (meta.attribute("property") == "og:title" ||
                    meta.attribute("property") == "twitter:title")) {
                    QString content = meta.attribute("content");
                    if (!content.isEmpty()) {
                        this->ogItem.setTitle(content);
                        foundTitle = true;
                    }
                }

                // 处理图片
                if (meta.attribute("property") == "og:image" ||
                    meta.attribute("property") == "twitter:image") {
                    QString content = meta.attribute("content");
                    if (!content.isEmpty()) {
                        pendingImageUrls << content;
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
            if (pendingImageUrls.isEmpty()) {
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
                        pendingImageUrls << src;
                        break;  // 只取第一张合适的图片
                    }
                }
            }

            // 3. 最后选择 favicon
            if (pendingImageUrls.isEmpty()) {
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
                        pendingImageUrls << href;
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
                    pendingImageUrls << url;
                }
            }

            // 如果没有 og:image，尝试 favicon
            if (pendingImageUrls.isEmpty()) {
                auto processFaviconMatch = [this](const QRegularExpressionMatch& match) {
                    QString url = match.captured(1);
                    if (!url.isEmpty()) {
                        pendingImageUrls << url;
                    }
                    url = match.captured(2);
                    if (!url.isEmpty()) {
                        pendingImageUrls << url;
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
        if (pendingImageUrls.isEmpty()) {
            for (const QString &path : getHighQualityIconPaths()) {
                pendingImageUrls << (targetUrl.toString() + path);
            }
        }

        // 开始尝试获取图片
        tryNextImage();
    } else {
        // 处理图片请求的响应
        QPixmap image;
        if (image.loadFromData(reply->readAll())) {
            this->ogItem.setImage(image);
            Q_EMIT finished(this->ogItem);
            return;
        }

        // 如果这个图片加载失败，尝试下一个
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
