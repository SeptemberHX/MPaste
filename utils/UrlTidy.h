// input: Depends on QUrl and QUrlQuery.
// output: Tells which query parameters of a web address only track a share, and gives the address without them.
// pos: Utils-layer helper for the link preview's parameter list and "copy clean link".
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_URLTIDY_H
#define MPASTE_URLTIDY_H

#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

namespace UrlTidy {

struct Parameter {
    QString key;
    QString value;     // decoded, as people read it
    bool tracking = false;
};

// The address's query, in order, each marked if it only says who shared it
// and where (utm_*, spm, vd_source, fbclid, ...). The list is kept to
// parameters known to be safe to drop: the page opens the same without them.
QList<Parameter> parameters(const QUrl &url);
bool hasTrackingParameters(const QUrl &url);

// The address without its tracking parameters; the others keep their order
// and their encoding.
QUrl withoutTracking(const QUrl &url);

// The address as people read it: percent-encoded text decoded (中文 rather
// than %E4%B8%AD%E6%96%87), punycode hosts shown in their own script.
QString readable(const QUrl &url);

} // namespace UrlTidy

#endif // MPASTE_URLTIDY_H
