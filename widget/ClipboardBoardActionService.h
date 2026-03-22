// input: Depends on ClipboardBoardModel, ClipboardBoardService, and ClipboardItem identity semantics.
// output: Exposes board-level item actions that are independent from widget presentation.
// pos: Widget-layer action helper bridging board model persistence and mutation.
// update: If I change, update widget/README.md.
#ifndef MPASTE_CLIPBOARDACTIONSERVICE_H
#define MPASTE_CLIPBOARDACTIONSERVICE_H

#include <QList>

#include "data/ClipboardItem.h"

class ClipboardBoardModel;
class ClipboardBoardService;

namespace ClipboardBoardActionService {

void refreshPersistedItem(ClipboardBoardModel *boardModel,
                          ClipboardBoardService *boardService,
                          int row);

bool persistItemMetadata(ClipboardBoardService *boardService,
                         ClipboardBoardModel *boardModel,
                         int row,
                         const ClipboardItem &item);

QList<int> resolveRowsForItems(ClipboardBoardModel *boardModel,
                               const QList<ClipboardItem> &items);

int removeItems(ClipboardBoardService *boardService,
                ClipboardBoardModel *boardModel,
                const QList<ClipboardItem> &items);

bool removeItem(ClipboardBoardService *boardService,
                ClipboardBoardModel *boardModel,
                const ClipboardItem &item);

bool setFavorite(ClipboardBoardModel *boardModel,
                 const ClipboardItem &item,
                 bool favorite);

bool applyPinnedState(ClipboardBoardModel *boardModel,
                      ClipboardBoardService *boardService,
                      int row,
                      int targetRow,
                      bool pinned);

bool exportItemToFile(const ClipboardItem &item,
                      const QString &filePath,
                      QString *errorMessage);

} // namespace ClipboardBoardActionService

#endif // MPASTE_CLIPBOARDACTIONSERVICE_H
