// input: 依赖 Qt 平台抽象、系统 API 与调用方声明。
// output: 对外提供 OpenGraphFetcher 的工具接口。
// pos: utils 层中的 OpenGraphFetcher 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
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