// input: Depends on Qt Widgets, ClipboardItem data, and inner-card widget APIs.
// output: Exposes one clipboard card widget with actions like save, favorite, preview, and plain-text paste.
// pos: Widget-layer item card declaration used inside horizontal history boards.
// update: If I change, update this header block and my folder README.md.
// ClipboardItemWidget.h
#ifndef MPASTE_CLIPBOARDITEMWIDGET_H
#define MPASTE_CLIPBOARDITEMWIDGET_H

#include <QWidget>
#include "ClipboardItemInnerWidget.h"
#include "data/ClipboardItem.h"

class QLabel;
class QToolButton;
class QMenu;
class QHBoxLayout;

class ClipboardItemWidget : public QWidget {
    Q_OBJECT

public:
    explicit ClipboardItemWidget(QString category, QColor borderColor, QWidget *parent = nullptr);
    ~ClipboardItemWidget() override = default;

    // Public API
    [[nodiscard]] const ClipboardItem& getItem() const;
    void setShortcutInfo(int num);
    void clearShortcutInfo();
    void setFavorite(bool favorite);

public slots:
    void showItem(const ClipboardItem& item);
    void setSelected(bool selected);

signals:
    void clicked();
    void doubleClicked();
    void itemNeedToSave();
    void itemStared(const ClipboardItem& item);
    void itemUnstared(const ClipboardItem& item);
    void pastePlainTextRequested(const ClipboardItem& item);
    void favoriteChanged(bool isFavorite);
    void deleteRequested();
    void saveRequested(const ClipboardItem& item);
    void detailsRequested(const ClipboardItem& item);
    void previewRequested();

protected:
    // Event handlers
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // UI Setup
    void setupUI();
    void setupActionButtons();
    void setupContextMenu();
    void initializeEffects();

    // UI Helpers
    [[nodiscard]] QToolButton* createActionButton(const QString& iconPath, const QString& tooltip) const;
    void showContextMenu(const QPoint& pos);
    void updateFavoriteButton();

    // Action handlers
    void handleFavoriteAction();
    void handlePastePlainTextAction();
    void handleDeleteAction();
    void handleDetailsAction();
    void handleSaveAction();
    void handleStarAction();

    // UI Components
    struct {
        QHBoxLayout* mainLayout{nullptr};
        ClipboardItemInnerWidget* innerWidget{nullptr};

        struct {
            QWidget* container{nullptr};
            QHBoxLayout* layout{nullptr};
            QToolButton* favoriteBtn{nullptr};
            QToolButton* deleteBtn{nullptr};
        } actions;

        QMenu* contextMenu{nullptr};
        QLabel* favoriteIndicator{nullptr};
    } ui;

    // Data
    ClipboardItem currentItem;
    bool isFavorite{false};

    QString category;
    QColor borderColor;
};

#endif //MPASTE_CLIPBOARDITEMWIDGET_H
