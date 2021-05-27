//
// Created by ragdoll on 2021/5/27.
//

#include "OpenGraphFetcher.h"
#include <QNetworkReply>
#include <iostream>
#include <QImageReader>
#include <QPixmap>

OpenGraphFetcher::OpenGraphFetcher(const QUrl &url, QObject *parent)
    : QObject(parent)
    , targetUrl(url)
{
    this->ogImageReg = QRegExp("<meta [ ]*property[ ]*=[ ]*\"og:image\"[ ]*content[ ]*=[ ]*\"(.*)\"[ ]*/?>");
    this->ogTitleReg = QRegExp("<meta [ ]*property[ ]*=[ ]*\"og:title\"[ ]*content[ ]*=[ ]*\"(.*)\"[ ]*/?>");
    this->titleReg = QRegExp("<title.*>(.*)</title>");
    this->faviconReg1 = QRegExp("<link[ ]*href=\"(.*)\".*ref=\"{short }?icon\".*/?>");
    this->faviconReg2 = QRegExp("<link[ ]*ref=\"{short }?icon\".*href=\"(.*)\".*/?>");

    this->ogImageReg.setMinimal(true);
    this->ogTitleReg.setMinimal(true);
    this->titleReg.setMinimal(true);
    this->faviconReg1.setMinimal(true);
    this->faviconReg2.setMinimal(true);
    this->naManager = new QNetworkAccessManager(this);
    connect(this->naManager, &QNetworkAccessManager::finished, this, &OpenGraphFetcher::requestFinished);
}

void OpenGraphFetcher::handle() {
    this->naManager->get(QNetworkRequest(this->targetUrl));
}

void OpenGraphFetcher::requestFinished(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        std::cout << "Error happened" << std::endl;
        Q_EMIT finished(this->ogItem);
    } else if (reply->request().url() == this->targetUrl) {
        QString body(reply->readAll());
        int pos = this->ogImageReg.indexIn(body);
        QString faviconStr;
        if (pos >= 0) {
            faviconStr = this->ogImageReg.cap(1);
        } else {
            QString str = this->targetUrl.toString();
            str = str.mid(0, str.indexOf(':'));
            pos = this->faviconReg1.indexIn(body);
            if (pos >= 0) {
                faviconStr = this->faviconReg1.cap(1);
            }  else {
                pos = this->faviconReg2.indexIn(body);
                if (pos >= 0) {
                    faviconStr = this->faviconReg2.cap(1);
                } else {
                    QUrl faviconUrl(str + "://" + this->targetUrl.host() + "/favicon.ico");
                    faviconStr = faviconUrl.toString();
                }
            }

            if (!faviconStr.startsWith("http")) {
                if (faviconStr.startsWith("//")) {
                    faviconStr = str + faviconStr;
                } else {
                    QUrl url(str + "://" + this->targetUrl.host());
                    faviconStr = url.resolved(faviconStr).toString();
                }
            }
        }
        this->imageUrl = QUrl(faviconStr);
        this->naManager->get(QNetworkRequest(this->imageUrl));

        pos = this->ogTitleReg.indexIn(body);
        if (pos >= 0) {
            QString omgTitleStr = this->ogTitleReg.cap(1);
            this->ogItem.setTitle(omgTitleStr);
        } else {
            pos = this->titleReg.indexIn(body);
            if (pos >= 0) {
                QString titleStr = this->titleReg.cap(1);
                this->ogItem.setTitle(titleStr);
            }
        }
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
