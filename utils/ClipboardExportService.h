// input: Depends on ClipboardItem data and Qt mime primitives.
// output: Builds clipboard-export MIME payloads with semantic fields plus preserved raw formats.
// pos: utils layer clipboard export helper.
#ifndef MPASTE_CLIPBOARD_EXPORT_SERVICE_H
#define MPASTE_CLIPBOARD_EXPORT_SERVICE_H

class QMimeData;
class ClipboardItem;

namespace ClipboardExportService {

QMimeData *buildMimeData(const ClipboardItem &item);

} // namespace ClipboardExportService

#endif // MPASTE_CLIPBOARD_EXPORT_SERVICE_H
