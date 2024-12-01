#ifndef MPASTE_OPENGRAPHFETCHER_H
#define MPASTE_OPENGRAPHFETCHER_H

#include <QNetworkAccessManager>
#include <QRegularExpression>
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
    QStringList replaceHttpHosts;

    QUrl targetUrl;
    QUrl imageUrl;
    QUrl realCalledUrl;

    QRegularExpression ogImageReg;
    QRegularExpression ogTitleReg;
    QRegularExpression titleReg;
    QRegularExpression faviconReg1;
    QRegularExpression faviconReg2;

    QNetworkAccessManager *naManager;
    OpenGraphItem ogItem;
};

#endif //MPASTE_OPENGRAPHFETCHER_H