// input: 依赖 Qt Widgets、data 层对象与同层组件声明。
// output: 对外提供 ClipboardItemWidget 的声明接口。
// pos: widget 层中的 ClipboardItemWidget 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
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
    void favoriteChanged(bool isFavorite);
    void deleteRequested();
    void saveRequested(const ClipboardItem& item);
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
    void handleDeleteAction();
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