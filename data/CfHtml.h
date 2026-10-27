// input: HTML as an application put it on the clipboard.
// output: The same HTML without a Windows CF_HTML header in front of it, and
//         (cleanClipboardHtml) without bytes trailing after the page.
// pos: Data-layer helper shared by capture, thumbnails, preview and paste.
// update: If I change, update data/README.md.
#ifndef MPASTE_CFHTML_H
#define MPASTE_CFHTML_H

#include <QRegularExpression>
#include <QString>

// Some applications on Linux (WPS among them) put Windows' CF_HTML format into
// text/html as is: "Version:0.9\nStartHTML:0000000105\n...EndFragment:..." and
// then the page. Rendered, those header lines show up as text. This returns the
// page alone: from the StartHTML offset when it is a sane one, else from the
// first tag. HTML that does not start with such a header is returned unchanged.
inline QString stripCfHtmlHeader(const QString &html) {
    if (!html.startsWith(QStringLiteral("Version:")) || !html.left(400).contains(QStringLiteral("StartHTML:"))) {
        return html;
    }
    // The offset counts UTF-8 bytes; the header itself is ASCII, so it is also a
    // character offset as long as it stays within the header.
    static const QRegularExpression startHtml(QStringLiteral("StartHTML:(\\d+)"));
    const QRegularExpressionMatch match = startHtml.match(html.left(400));
    if (match.hasMatch()) {
        const int offset = match.captured(1).toInt();
        if (offset > 0 && offset < html.size() && html.at(offset) == QLatin1Char('<')) {
            return html.mid(offset);
        }
    }
    const int firstTag = html.indexOf(QLatin1Char('<'));
    return firstTag >= 0 ? html.mid(firstTag) : html;
}

// WPS on Linux can hand over text/html longer than its page: after "</html>"
// come bytes of its memory, which a render shows as a line of garbled text.
// This drops anything but white space after the last "</html>".
inline QString dropTrailingAfterHtml(const QString &html) {
    const int end = html.lastIndexOf(QStringLiteral("</html>"), -1, Qt::CaseInsensitive);
    if (end < 0) {
        return html;
    }
    const int after = end + 7;
    for (int i = after; i < html.size(); ++i) {
        if (!html.at(i).isSpace()) {
            return html.left(after);
        }
    }
    return html;
}

// HTML as MPaste keeps it: no CF_HTML header, nothing after the page.
inline QString cleanClipboardHtml(const QString &html) {
    return dropTrailingAfterHtml(stripCfHtmlHeader(html));
}

#endif // MPASTE_CFHTML_H
