// input: Depends on ScrollItemsWidget.h, BoardInternalHelpers.h, Qt Widgets.
// output: Implements context menu methods of ScrollItemsWidget.
// pos: Split from ScrollItemsWidgetMV.cpp -- context menu population and save-to-file.
#include <QFileDialog>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QItemSelectionModel>

#include <utils/MPasteSettings.h>

#include "BoardInternalHelpers.h"
#include "ClipboardBoardActionService.h"
#include "ClipboardBoardModel.h"
#include "ClipboardBoardProxyModel.h"
#include "ClipboardItemPreviewDialog.h"
#include "ScrollItemsWidget.h"
#include "utils/IconResolver.h"
#include "utils/PlatformRelated.h"

using namespace BoardHelpers;

void ScrollItemsWidget::showContextMenu(const QPoint &pos) {
    if (!listView_) {
        return;
    }

    QModelIndex proxyIndex = listView_->indexAt(pos);
    if (!proxyIndex.isValid()) {
        proxyIndex = currentProxyIndex();
    }
    if (!proxyIndex.isValid()) {
        return;
    }

    QItemSelectionModel *selectionModel = listView_->selectionModel();
    const bool preserveSelection = selectionModel
        && selectionModel->isSelected(proxyIndex)
        && selectionModel->selectedRows().size() > 1;
    if (preserveSelection) {
        selectionModel->setCurrentIndex(proxyIndex, QItemSelectionModel::NoUpdate);
    } else {
        setCurrentProxyIndex(proxyIndex);
    }

    const QList<ClipboardItem> selection = selectedItems();
    if (selection.isEmpty()) {
        return;
    }

    const ClipboardItem item = selection.first();
    if (item.getName().isEmpty()) {
        return;
    }

    const bool multiSelection = selection.size() > 1;

    QMenu menu(this);
    applyMenuTheme(&menu);
    if (multiSelection) {
        populateMultiSelectionMenu(&menu, selection);
    } else {
        populateSingleSelectionMenu(&menu, proxyIndex, item);
    }

    menu.exec(listView_->viewport()->mapToGlobal(pos));
}

void ScrollItemsWidget::populateMultiSelectionMenu(QMenu *menu, const QList<ClipboardItem> &selection) {
    if (!menu || selection.isEmpty() || !boardModel_) {
        return;
    }

    const QList<int> sourceRows = selectedSourceRows();
    bool hasFavorite = false;
    bool hasNonFavorite = false;
    for (const int sourceRow : sourceRows) {
        const bool favorite = boardModel_->isFavorite(sourceRow);
        hasFavorite = hasFavorite || favorite;
        hasNonFavorite = hasNonFavorite || !favorite;
    }

    if (hasNonFavorite) {
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/star.svg")),
                        favoriteActionLabel(false),
                        [this, selection]() {
                            applyFavoriteToItems(selection, true);
                        });
    }
    if (hasFavorite) {
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/star_filled.svg")),
                        favoriteActionLabel(true),
                        [this, selection]() {
                            applyFavoriteToItems(selection, false);
                        });
    }
    if (category != MPasteSettings::STAR_CATEGORY_NAME) {
        if (hasFavorite || hasNonFavorite) {
            menu->addSeparator();
        }
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/delete.svg")),
                        deleteSelectedLabel(),
                        [this, selection]() {
                            removeItems(selection);
                        });
    }
}

void ScrollItemsWidget::populateSingleSelectionMenu(QMenu *menu, const QModelIndex &proxyIndex, const ClipboardItem &item) {
    if (!menu || !proxyIndex.isValid() || !proxyModel_ || !boardModel_) {
        return;
    }

    const bool dark = darkTheme_;
    const int sourceRow = sourceRowForProxyIndex(proxyIndex);
    const ContentType contentType = item.getContentType();
    const bool isFavorite = boardModel_->isFavorite(sourceRow);
    const QList<QUrl> urls = item.getNormalizedUrls();
    const bool canOpenFolder = contentType == File
        && !urls.isEmpty()
        && std::all_of(urls.begin(), urls.end(), [](const QUrl &url) { return url.isLocalFile(); });
    const bool canSaveToFile = supportsSaveToFile(contentType);

    menu->addAction(IconResolver::themedIcon(QStringLiteral("text_plain"), dark), plainTextPasteLabel(), [this]() {
        const ClipboardItem *selectedItem = selectedByEnter();
        if (selectedItem) {
            emit plainTextPasteRequested(*selectedItem);
        }
    });
    menu->addAction(IconResolver::themedIcon(QStringLiteral("details"), dark), detailsLabel(), [this, proxyIndex]() {
        setCurrentProxyIndex(proxyIndex);
        const ClipboardItem *selectedItem = currentSelectedItem();
        if (!selectedItem) {
            return;
        }
        const QPair<int, int> sequenceInfo = displaySequenceForIndex(proxyIndex);
        emit detailsRequested(*selectedItem, sequenceInfo.first, sequenceInfo.second);
    });
    menu->addAction(IconResolver::themedIcon(QStringLiteral("rename"), dark), aliasLabel(), [this, item]() {
        openAliasDialogForItem(item);
    });
    if (ClipboardItemPreviewDialog::supportsPreview(item)) {
        menu->addAction(IconResolver::themedIcon(QStringLiteral("preview"), dark), tr("Preview"), [this]() {
            const ClipboardItem *selectedItem = currentSelectedItem();
            if (selectedItem) {
                emit previewRequested(*selectedItem);
            }
        });
    }
    if (canSaveToFile) {
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/save_black.svg")), saveItemLabel(), [this, proxyIndex]() {
            setCurrentProxyIndex(proxyIndex);
            const ClipboardItem *selectedItem = currentSelectedItem();
            if (selectedItem) {
                saveItemToFile(*selectedItem);
            }
        });
    }
    if (canOpenFolder) {
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/files.svg")), openContainingFolderLabel(), [urls]() {
            PlatformRelated::revealInFileManager(urls);
        });
    }

    if (contentType == Image || contentType == Office) {
        menu->addAction(IconResolver::themedIcon(QStringLiteral("text_plain"), dark),
                        tr("Extract Text (OCR)"), [this, item]() {
            emit ocrRequested(item);
        });
    }

    const bool isPinned = item.isPinned();
    menu->addAction(IconResolver::themedIcon(QStringLiteral("pin"), dark),
                    pinActionLabel(isPinned),
                    [this, item, isPinned]() {
                        setItemPinned(item, !isPinned);
                    });

    menu->addSeparator();
    menu->addAction(QIcon(isFavorite
                          ? QStringLiteral(":/resources/resources/star_filled.svg")
                          : QStringLiteral(":/resources/resources/star.svg")),
                    favoriteActionLabel(isFavorite),
                    [this, item, isFavorite]() {
                        if (isFavorite) {
                            setItemFavorite(item, false);
                            emit itemUnstared(item);
                        } else {
                            setItemFavorite(item, true);
                            emit itemStared(item);
                        }
                    });
    menu->addAction(QIcon(QStringLiteral(":/resources/resources/delete.svg")), deleteLabel(), [this, item]() {
        if (category == MPasteSettings::STAR_CATEGORY_NAME) {
            emit itemUnstared(item);
            return;
        }
        removeItemByContent(item);
    });
}

void ScrollItemsWidget::saveItemToFile(const ClipboardItem &item) {
    const ContentType contentType = item.getContentType();
    if (!supportsSaveToFile(contentType)) {
        return;
    }

    QString selectedFilter;
    QString filePath;
    switch (contentType) {
        case Image:
            selectedFilter = QStringLiteral("PNG Image (*.png)");
            filePath = QFileDialog::getSaveFileName(this,
                                                    saveDialogTitle(contentType),
                                                    suggestedExportPath(item, QStringLiteral(".png")),
                                                    imageSaveFilters(),
                                                    &selectedFilter);
            filePath = ensureFileSuffix(filePath, defaultImageSuffixForFilter(selectedFilter));
            break;
        case RichText:
            filePath = QFileDialog::getSaveFileName(this,
                                                    saveDialogTitle(contentType),
                                                    suggestedExportPath(item, QStringLiteral(".html")),
                                                    QStringLiteral("HTML Files (*.html *.htm);;All Files (*)"));
            filePath = ensureFileSuffix(filePath, QStringLiteral(".html"));
            break;
        case Text:
            filePath = QFileDialog::getSaveFileName(this,
                                                    saveDialogTitle(contentType),
                                                    suggestedExportPath(item, QStringLiteral(".txt")),
                                                    QStringLiteral("Text Files (*.txt);;All Files (*)"));
            filePath = ensureFileSuffix(filePath, QStringLiteral(".txt"));
            break;
        default:
            return;
    }

    if (filePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    const bool success = ClipboardBoardActionService::exportItemToFile(item, filePath, &errorMessage);

    if (!success) {
        if (prefersChineseUi()) {
            if (errorMessage == QLatin1String("The current item does not contain savable HTML content.")) {
                errorMessage = QString::fromUtf16(u"\u5F53\u524D\u6761\u76EE\u6CA1\u6709\u53EF\u4FDD\u5B58\u7684 HTML \u5185\u5BB9\u3002");
            } else if (errorMessage == QLatin1String("Unable to write the image to the target file.")) {
                errorMessage = QString::fromUtf16(u"\u65E0\u6CD5\u5C06\u56FE\u50CF\u5199\u5165\u76EE\u6807\u6587\u4EF6\u3002");
            } else if (errorMessage == QLatin1String("The current item does not support export.")) {
                errorMessage = QString::fromUtf16(u"\u5F53\u524D\u6761\u76EE\u6682\u4E0D\u652F\u6301\u5BFC\u51FA\u3002");
            }
        }
        QMessageBox::warning(this, saveFailedTitle(), saveFailedMessage(errorMessage));
    }
}
