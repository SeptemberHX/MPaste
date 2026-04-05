// Internal helpers shared across ClipboardBoardService translation units.
// This header is NOT part of the public API — include only from
// ClipboardBoardService*.cpp files.
#ifndef CLIPBOARDBOARDSERVICEINTERNAL_H
#define CLIPBOARDBOARDSERVICEINTERNAL_H

#include "ClipboardBoardService.h"

class ClipboardItem;
class QString;

// Build a lightweight index entry from a fully loaded (light) ClipboardItem.
ClipboardBoardService::IndexedItemMeta buildIndexedItemMeta(const QString &filePath,
                                                            const ClipboardItem &item);

// Check whether an indexed item passes the content-type / keyword filter.
bool indexedItemMatchesFilter(const ClipboardBoardService::IndexedItemMeta &item,
                              ContentType type,
                              const QString &keyword,
                              const QSet<QString> &matchedNames);

#endif // CLIPBOARDBOARDSERVICEINTERNAL_H
