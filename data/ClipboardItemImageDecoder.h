// input: QMimeData payloads containing image data in various formats.
// output: Decoded QPixmap images and raw image byte extraction.
// pos: Data-layer image decoding utilities extracted from ClipboardItem.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDITEMIMAGEDECODER_H
#define MPASTE_CLIPBOARDITEMIMAGEDECODER_H

#include <QByteArray>
#include <QPixmap>
#include <QString>

class QMimeData;

namespace ClipboardImageDecoder {

QPixmap decodePixmapFromMimeData(const QMimeData *mimeData);
void materializeCanonicalImage(QMimeData *mimeData, const QPixmap &pixmap);
QByteArray extractFastImagePayloadBytes(const QMimeData *mimeData, QString *formatOut = nullptr);
bool shouldSkipImageDecodeFormat(const QString &format);

} // namespace ClipboardImageDecoder

#endif // MPASTE_CLIPBOARDITEMIMAGEDECODER_H
