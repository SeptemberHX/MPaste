// input: Raw clipboard text and byte payloads from various MIME formats.
// output: Parsed, normalized URL lists suitable for content classification.
// pos: Data-layer URL parsing utilities extracted from ClipboardItem.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardItemUrlParser.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace ClipboardUrlParser {

QString windowsDrivePathFromUrl(const QUrl &url) {
    if (!url.isValid() || url.isLocalFile()) {
        return {};
    }

    const QString scheme = url.scheme();
    const QString path = url.path();
    if (scheme.size() == 1 && !path.isEmpty() && path.startsWith(QLatin1Char('/'))) {
        return QDir::fromNativeSeparators(scheme.toUpper() + QStringLiteral(":") + path);
    }

    const QString asText = url.toDisplayString(QUrl::PreferLocalFile);
    if (asText.size() > 2
        && asText[1] == QLatin1Char(':')
        && (asText[2] == QLatin1Char('/') || asText[2] == QLatin1Char('\\'))) {
        return QDir::fromNativeSeparators(asText);
    }

    return {};
}

QUrl normalizePotentialLocalFileUrl(const QUrl &url) {
    if (!url.isValid()) {
        return {};
    }
    if (url.isLocalFile()) {
        return url;
    }

    const QString localPath = windowsDrivePathFromUrl(url);
    if (!localPath.isEmpty()) {
        return QUrl::fromLocalFile(localPath);
    }
    return url;
}

QList<QUrl> parseUrlsFromLines(const QStringList &lines, bool strictMode) {
    QList<QUrl> result;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        if ((trimmed.startsWith(QLatin1String("/"))
             || (trimmed.size() > 2 && trimmed[1] == QLatin1Char(':')
                 && (trimmed[2] == QLatin1Char('/') || trimmed[2] == QLatin1Char('\\'))))
            && QFileInfo::exists(trimmed)) {
            result << QUrl::fromLocalFile(QDir::fromNativeSeparators(trimmed));
            continue;
        }

        QUrl url(trimmed);
        if (url.isValid() && !url.isRelative() && !trimmed.contains(QLatin1Char(' '))) {
            result << normalizePotentialLocalFileUrl(url);
            continue;
        }

        if (strictMode) {
            return {};
        }
    }
    return result;
}

QList<QUrl> parseProtocolTextUrls(const QString &text, bool allowOperationOnlyHeader) {
    if (text.isEmpty()) {
        return {};
    }

    QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        return {};
    }

    bool protocolHeader = false;
    QString first = lines.first().trimmed();
    if (first == QLatin1String("x-special/nautilus-clipboard")) {
        protocolHeader = true;
        lines.removeFirst();
        if (!lines.isEmpty()) {
            const QString op = lines.first().trimmed().toLower();
            if (op == QLatin1String("copy") || op == QLatin1String("cut")) {
                lines.removeFirst();
            }
        }
    } else if (allowOperationOnlyHeader
               && (first.compare(QLatin1String("copy"), Qt::CaseInsensitive) == 0
                   || first.compare(QLatin1String("cut"), Qt::CaseInsensitive) == 0)) {
        protocolHeader = true;
        lines.removeFirst();
    }

    QList<QUrl> parsed = parseUrlsFromLines(lines, protocolHeader);
    if (!parsed.isEmpty()) {
        return parsed;
    }

    return {};
}

QList<QUrl> parseUriListText(const QString &text) {
    if (text.isEmpty()) {
        return {};
    }

    QStringList filteredLines;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QLatin1Char('#'))) {
            filteredLines << trimmed;
        }
    }

    return parseUrlsFromLines(filteredLines, true);
}

QString decodeNullTerminatedText(const QByteArray &data, bool utf16) {
    if (data.isEmpty()) {
        return {};
    }

    QString text = utf16
        ? QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData()), data.size() / 2)
        : QString::fromUtf8(data);

    const int nullIndex = text.indexOf(QChar('\0'));
    if (nullIndex >= 0) {
        text.truncate(nullIndex);
    }
    return text.trimmed();
}

QList<QUrl> parseWindowsUrlMime(const QByteArray &data, bool utf16) {
    const QString text = decodeNullTerminatedText(data, utf16);
    if (text.isEmpty()) {
        return {};
    }

    const QUrl url(text);
    if (url.isValid() && !url.isRelative()) {
        return {url};
    }
    return {};
}

QList<QUrl> parseWindowsFileNameMime(const QByteArray &data, bool utf16) {
    if (data.isEmpty()) {
        return {};
    }

    QString text = utf16
        ? QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData()), data.size() / 2)
        : QString::fromUtf8(data);

    const QStringList parts = text.split(QChar('\0'), Qt::SkipEmptyParts);
    QList<QUrl> result;
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        result << QUrl::fromLocalFile(trimmed);
    }
    return result;
}

bool textMatchesUrls(const QString &text, const QList<QUrl> &urls) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    auto candidatesForUrl = [](const QUrl &url) {
        QStringList candidates;
        candidates << url.toString(QUrl::FullyEncoded);
        candidates << QUrl::fromPercentEncoding(url.toEncoded());
        if (url.isLocalFile()) {
            candidates << url.toLocalFile();
        }
        candidates.removeAll(QString());
        candidates.removeDuplicates();
        return candidates;
    };

    if (urls.size() == 1) {
        return candidatesForUrl(urls.first()).contains(trimmed);
    }

    const QStringList lines = trimmed.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    if (lines.size() != urls.size()) {
        return false;
    }

    for (int i = 0; i < urls.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (!candidatesForUrl(urls[i]).contains(line)) {
            return false;
        }
    }
    return true;
}

} // namespace ClipboardUrlParser
