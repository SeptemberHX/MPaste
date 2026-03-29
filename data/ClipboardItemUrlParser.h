// input: Raw clipboard text and byte payloads from various MIME formats.
// output: Parsed, normalized URL lists suitable for content classification.
// pos: Data-layer URL parsing utilities extracted from ClipboardItem.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDITEMURLPARSER_H
#define MPASTE_CLIPBOARDITEMURLPARSER_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

namespace ClipboardUrlParser {

QString windowsDrivePathFromUrl(const QUrl &url);
QUrl normalizePotentialLocalFileUrl(const QUrl &url);
QList<QUrl> parseUrlsFromLines(const QStringList &lines, bool strictMode);
QList<QUrl> parseProtocolTextUrls(const QString &text, bool allowOperationOnlyHeader);
QList<QUrl> parseUriListText(const QString &text);
QString decodeNullTerminatedText(const QByteArray &data, bool utf16);
QList<QUrl> parseWindowsUrlMime(const QByteArray &data, bool utf16);
QList<QUrl> parseWindowsFileNameMime(const QByteArray &data, bool utf16);
bool textMatchesUrls(const QString &text, const QList<QUrl> &urls);

} // namespace ClipboardUrlParser

#endif // MPASTE_CLIPBOARDITEMURLPARSER_H
