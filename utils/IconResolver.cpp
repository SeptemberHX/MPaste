// input: Depends on IconResolver.h and Qt resource paths.
// output: Provides theme-aware icon paths with fallback to base icons.
// pos: utils layer icon helper implementation.
// update: If I change, update this header block and utils/README.md.
#include "IconResolver.h"

QString IconResolver::themedPath(const QString &baseName, bool dark) {
    const QString basePath = QStringLiteral(":/resources/resources/%1.svg").arg(baseName);
    if (!dark) {
        return basePath;
    }
    return QStringLiteral(":/resources/resources/%1_light.svg").arg(baseName);
}

QIcon IconResolver::themedIcon(const QString &baseName, bool dark) {
    const QString basePath = QStringLiteral(":/resources/resources/%1.svg").arg(baseName);
    if (!dark) {
        return QIcon(basePath);
    }
    const QString lightPath = QStringLiteral(":/resources/resources/%1_light.svg").arg(baseName);
    QIcon icon(lightPath);
    return icon.isNull() ? QIcon(basePath) : icon;
}
