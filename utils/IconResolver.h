// input: Depends on Qt icon loading and theme state.
// output: Resolves light/dark icon variants for QIcon usage.
// pos: utils layer icon helper.
// update: If I change, update this header block and utils/README.md.
#ifndef MPASTE_ICON_RESOLVER_H
#define MPASTE_ICON_RESOLVER_H

#include <QIcon>
#include <QString>

class IconResolver {
public:
    static QString themedPath(const QString &baseName, bool dark);
    static QIcon themedIcon(const QString &baseName, bool dark);
};

#endif // MPASTE_ICON_RESOLVER_H
