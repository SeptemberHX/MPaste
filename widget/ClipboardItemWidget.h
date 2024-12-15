//
// Created by ragdoll on 2021/5/22.
//

#ifndef MPASTE_CLIPBOARDITEMWIDGET_H
#define MPASTE_CLIPBOARDITEMWIDGET_H

#include <QWidget>
#include <QToolButton>
#include <QMenu>
#include "ClipboardItemInnerWidget.h"
#include "data/ClipboardItem.h"
#include "utils/ClipboardMonitor.h"

class ClipboardItemWidget : public QWidget{

    Q_OBJECT

public:
    explicit ClipboardItemWidget(QWidget *parent= nullptr);

    const ClipboardItem &getItem() const;
    void setShortcutInfo(int num);
    void clearShortcutInfo();

signals:
    void clicked();
    void doubleClicked();
    void itemNeedToSave();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

public slots:
    void showItem(ClipboardItem item);
    void setSelected(bool selected);

private:
    QHBoxLayout *layout;

    ClipboardItemInnerWidget *innerShadowedWidget;
    ClipboardItem item;

    ClipboardMonitor *monitor;

    QToolButton* favoriteButton;
    QToolButton* deleteButton;
    QHBoxLayout* buttonLayout;
    QWidget* buttonContainer;  // 用于容纳所有按钮
    bool isFavorite;

    void setupActionButtons();
    void setupButtonContainer();
    QToolButton* createActionButton(const QString& iconPath,
                                  const QString& tooltip);

    QMenu* contextMenu;
    void setupContextMenu();
    void showContextMenu(const QPoint& pos);

signals:
    void favoriteChanged(bool isFavorite);
    void deleteRequested();  // 新增信号
    void saveRequested(const ClipboardItem& item);

private slots:
    void onFavoriteClicked();
    void onDeleteClicked();
    void onSaveTriggered();
};


#endif //MPASTE_CLIPBOARDITEMWIDGET_H
