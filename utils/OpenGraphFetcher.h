#ifndef MPASTE_OPENGRAPHFETCHER_H
#define MPASTE_OPENGRAPHFETCHER_H

#include <QDomDocument>
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
    QStringList getHighQualityIconPaths() const {
        return {
            "/apple-touch-icon.png",
            "/apple-touch-icon-precomposed.png",
            "/favicon-192x192.png",
            "/favicon-128x128.png",
            "/favicon-96x96.png",
            "/favicon-64x64.png",
            "/favicon-32x32.png",
            "/favicon.ico"
        };
    }

    QStringList pendingImageUrls;
    void tryNextImage();

    QString findMetaContent(const QDomDocument &doc, const QString &property);

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