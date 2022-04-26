//
// Created by ragdoll on 2021/5/27.
//

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
    this->ogImageReg = QRegularExpression("<meta .*property[ ]*=[ ]*\"og:image\"[ ]*content[ ]*=[ ]*\"(.*)\"[ ]*/?>");
    this->ogTitleReg = QRegularExpression("<meta .*property[ ]*=[ ]*\"og:title\"[ ]*content[ ]*=[ ]*\"(.*)\"[ ]*/?>");
    this->titleReg = QRegularExpression("<title.*>(.*)</title>");
    this->faviconReg1 = QRegularExpression("<link[ ]*href=\"(.*)\".*rel=\"{short }?icon\".*/?>");
    this->faviconReg2 = QRegularExpression("<link[ ]*rel=\"{short }?icon\".*href=\"(.*)\".*/?>");

//    this->ogImageReg.setMinimal(true);
//    this->ogTitleReg.setMinimal(true);
//    this->titleReg.setMinimal(true);
//    this->faviconReg1.setMinimal(true);
//    this->faviconReg2.setMinimal(true);
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
//    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    this->realCalledUrl = tUrl;
    this->naManager->get(request);
}

void OpenGraphFetcher::requestFinished(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        std::cout << "Error happened: " << reply->error() << std::endl;
        Q_EMIT finished(this->ogItem);
    } else if (reply->request().url() == this->realCalledUrl) {
        QDomDocument doc;
        QString faviconStr;
        QString body(reply->readAll());
        if (doc.setContent(body)) {
            QDomElement docElem = doc.documentElement();
            QDomNodeList metaNodeList = docElem.elementsByTagName("meta");
            for (int i = 0; i < metaNodeList.size(); ++i) {
                if (!metaNodeList.at(i).isNull()) {
                    QDomNode n = metaNodeList.at(i).toElement();
                    if (!n.isNull()) {
                        if (n.attributes().contains("property") && n.attributes().contains("content")) {
                            QString ogType = n.attributes().namedItem("property").nodeValue();
                            if (ogType == "og:title") {
                                this->ogItem.setTitle(n.attributes().namedItem("content").nodeValue());
                            } else if (ogType == "og:image" &&
                                       !n.attributes().namedItem("content").nodeValue().startsWith("$")) {
                                faviconStr = n.attributes().namedItem("content").nodeValue();
                            }
                        }
                    }
                }
            }

            if (faviconStr.isEmpty()) {
                QDomNodeList linkNodeList = docElem.elementsByTagName("link");
                for (int i = 0; i < linkNodeList.size(); ++i) {
                    if (!linkNodeList.at(i).isNull()) {
                        QDomNode n = linkNodeList.at(i).toElement();
                        if (!n.isNull()) {
                            if (n.attributes().contains("rel") && n.attributes().contains("href")) {
                                if (n.attributes().namedItem("rel").nodeValue().contains("icon")
                                    && !n.attributes().namedItem("href").nodeValue().startsWith("$")) {
                                    faviconStr = n.attributes().namedItem("href").nodeValue();
                                }
                            }
                        }
                    }
                }
            }
            if (this->ogItem.getTitle().isEmpty()) {
                QDomNodeList nodeList = docElem.elementsByTagName("title");
                for (int i = 0; i < nodeList.size(); ++i) {
                    if (!nodeList.at(i).isNull()) {
                        QDomElement e = nodeList.at(i).toElement();
                        if (!e.isNull()) {
                            this->ogItem.setTitle(e.text());
                        }
                    }
                }
            }
        } else {
            QRegularExpressionMatch match = this->ogImageReg.match(body);
            if (match.hasMatch()) {
                faviconStr = match.captured(1);
            } else {
                QString str = this->targetUrl.toString();
                str = str.mid(0, str.indexOf(':'));
                match = this->faviconReg1.match(str);
                if (match.hasMatch()) {
                    faviconStr = match.captured(1);
                }  else {
                    match = this->faviconReg2.match(str);
                    if (match.hasMatch()) {
                        faviconStr = match.captured(1);
                    } else {
                        QUrl faviconUrl(str + "://" + this->targetUrl.host() + "/favicon.ico");
                        faviconStr = faviconUrl.toString();
                    }
                }
            }

            match = this->ogTitleReg.match(body);
            if (match.hasMatch()) {
                QString omgTitleStr = match.captured(1);
                this->ogItem.setTitle(omgTitleStr);
            } else {
                match = this->titleReg.match(body);
                if (match.hasMatch()) {
                    QString titleStr = match.captured(1);
                    this->ogItem.setTitle(titleStr);
                }
            }
        }

        QString str = this->targetUrl.toString();
        if (!faviconStr.startsWith("http")) {
            if (faviconStr.startsWith("//")) {
                faviconStr = "https:" + faviconStr;
            } else {
                QUrl url(str + "://" + this->targetUrl.host());
                faviconStr = url.resolved(faviconStr).toString();
            }
        }
        this->imageUrl = QUrl(faviconStr);
        this->naManager->get(QNetworkRequest(this->imageUrl));


    } else if (reply->request().url() == this->imageUrl) {
        QPixmap image;
        image.loadFromData(reply->readAll());
        this->ogItem.setImage(image);
        Q_EMIT finished(this->ogItem);
    } else {
        std::cout << "Error happened" << std::endl;
        Q_EMIT finished(this->ogItem);
    }
}
