//
// Created by ragdoll on 2021/5/27.
//

#ifndef MPASTE_OPENGRAPHFETCHER_H
#define MPASTE_OPENGRAPHFETCHER_H


#include <QNetworkAccessManager>
#include "data/OpenGraphItem.h"

class OpenGraphFetcher : public QObject {

    Q_OBJECT

public:
    explicit OpenGraphFetcher(const QUrl &url, QObject *parent = nullptr);
    void handle();

signals:
    void finished(const OpenGraphItem &item);

private slots:
    void requestFinished(QNetworkReply *reply);

private:
    QUrl targetUrl;
    QUrl imageUrl;

    QRegExp ogImageReg;
    QRegExp ogTitleReg;
    QRegExp titleReg;

    QRegExp faviconReg1;
    QRegExp faviconReg2;

    QNetworkAccessManager *naManager;
    OpenGraphItem ogItem;
};


#endif //MPASTE_OPENGRAPHFETCHER_H
