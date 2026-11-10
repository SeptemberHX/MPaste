// input: Depends on UrlTidy.h and QUrl.
// output: Implements the tracking-parameter list, the clean address and the readable form of an address.
// pos: Utils-layer implementation behind the link preview.
// update: If I change, update this header block and my folder README.md.
#include "UrlTidy.h"

#include <QSet>
#include <QStringList>

namespace {

// Only parameters that say who shared a page, from where and with what:
// dropping them never changes the page. Ones some sites need (share_token,
// xsec_token, a WeChat article's __biz/mid/idx/sn) stay off the list.
const QSet<QString> &trackingKeys() {
    static const QSet<QString> keys = {
        // Ad and social networks.
        QStringLiteral("fbclid"), QStringLiteral("gclid"), QStringLiteral("dclid"),
        QStringLiteral("gbraid"), QStringLiteral("wbraid"), QStringLiteral("msclkid"),
        QStringLiteral("yclid"), QStringLiteral("igshid"), QStringLiteral("igsh"),
        QStringLiteral("mc_cid"), QStringLiteral("mc_eid"), QStringLiteral("ref_src"),
        QStringLiteral("_hsenc"), QStringLiteral("_hsmi"),
        // Alibaba (Taobao, Tmall, Alipay pages).
        QStringLiteral("spm"), QStringLiteral("ut_sk"),
        // Bilibili.
        QStringLiteral("spm_id_from"), QStringLiteral("vd_source"), QStringLiteral("unique_k"),
        QStringLiteral("share_source"), QStringLiteral("share_medium"), QStringLiteral("share_plat"),
        QStringLiteral("share_session_id"), QStringLiteral("share_tag"), QStringLiteral("share_from"),
        // WeChat articles.
        QStringLiteral("sharer_shareinfo"), QStringLiteral("sharer_shareinfo_first"),
    };
    return keys;
}

bool hostIs(const QString &host, const QString &domain) {
    return host == domain || host.endsWith(QLatin1Char('.') + domain);
}

bool isTrackingKey(const QString &host, const QString &rawKey) {
    const QString key = rawKey.toLower();
    if (key.startsWith(QLatin1String("utm_")) || trackingKeys().contains(key)) {
        return true;
    }
    // Share ids that are ordinary words elsewhere: only on their own sites.
    if (key == QLatin1String("si")) {
        return hostIs(host, QStringLiteral("youtube.com")) || hostIs(host, QStringLiteral("youtu.be"))
            || hostIs(host, QStringLiteral("spotify.com"));
    }
    if (key == QLatin1String("s") || key == QLatin1String("t")) {
        return hostIs(host, QStringLiteral("x.com")) || hostIs(host, QStringLiteral("twitter.com"));
    }
    if (key == QLatin1String("scene") || key == QLatin1String("srcid")) {
        return hostIs(host, QStringLiteral("mp.weixin.qq.com"));
    }
    return false;
}

// key=value pairs as written, so a clean address keeps the others' encoding.
QStringList rawPairs(const QUrl &url) {
    return url.query(QUrl::FullyEncoded).split(QLatin1Char('&'), Qt::SkipEmptyParts);
}

QString decoded(const QString &raw) {
    QByteArray bytes = raw.toUtf8();
    bytes.replace('+', ' ');
    return QUrl::fromPercentEncoding(bytes);
}

} // namespace

namespace UrlTidy {

QList<Parameter> parameters(const QUrl &url) {
    QList<Parameter> result;
    const QString host = url.host().toLower();
    for (const QString &pair : rawPairs(url)) {
        const int eq = pair.indexOf(QLatin1Char('='));
        Parameter parameter;
        parameter.key = decoded(eq < 0 ? pair : pair.left(eq));
        parameter.value = eq < 0 ? QString() : decoded(pair.mid(eq + 1));
        parameter.tracking = isTrackingKey(host, parameter.key);
        result << parameter;
    }
    return result;
}

bool hasTrackingParameters(const QUrl &url) {
    for (const Parameter &parameter : parameters(url)) {
        if (parameter.tracking) {
            return true;
        }
    }
    return false;
}

QUrl withoutTracking(const QUrl &url) {
    const QString host = url.host().toLower();
    QStringList kept;
    for (const QString &pair : rawPairs(url)) {
        const int eq = pair.indexOf(QLatin1Char('='));
        if (!isTrackingKey(host, decoded(eq < 0 ? pair : pair.left(eq)))) {
            kept << pair;
        }
    }
    QUrl clean = url;
    if (kept.isEmpty()) {
        clean.setQuery(QString());
    } else {
        clean.setQuery(kept.join(QLatin1Char('&')), QUrl::StrictMode);
    }
    return clean;
}

QString readable(const QUrl &url) {
    return url.toDisplayString(QUrl::PrettyDecoded);
}

} // namespace UrlTidy
