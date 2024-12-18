#ifndef MPASTEWIDGET_H
#define MPASTEWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QMimeData>
#include <QMenu>
#include <QMediaPlayer>
#include <QPropertyAnimation>
#include <QSystemTrayIcon>

#include "utils/ClipboardMonitor.h"
#include "ClipboardItemWidget.h"
#include "data/ClipboardItem.h"
#include "data/LocalSaver.h"
#include "AboutWidget.h"
#include "ScrollItemsWidget.h"

namespace Ui {
class MPasteWidget;
}

class MPasteWidget : public QWidget {
    Q_OBJECT

public:
    explicit MPasteWidget(QWidget *parent = nullptr);
    ~MPasteWidget();
    void setVisibleWithAnnimation(bool visible);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void clipboardUpdated(ClipboardItem item, int wId);
    void updateItemCount(int itemCount);
    void hideAndPaste();
    void debugKeyState();

private:
    // 初始化相关
    void initializeWidget();
    void initStyle();
    void initUI();
    void initSearchAnimations();
    void initClipboard();
    void initShortcuts();
    void initSystemTray();
    void initSound();
    void initMenu();
    void setupConnections();

    // 剪贴板操作
    void setClipboard(const ClipboardItem &item);
    void handleUrlsClipboard(const ClipboardItem &item);
    void loadFromSaveDir();

    // 搜索操作
    void setFocusOnSearch(bool flag);
    void handleSearchInput(QKeyEvent *event);

    // 键盘事件处理
    void handleKeyboardEvent(QKeyEvent *event);
    void handleEscapeKey();
    void handleEnterKey();
    void handleNavigationKeys(QKeyEvent *event);
    void handleHomeEndKeys(QKeyEvent *event);
    bool handleAltNumShortcut(QKeyEvent *event);

    // 辅助方法
    ScrollItemsWidget* currItemsWidget();

private:
    // UI 组件
    struct {
        Ui::MPasteWidget *ui;
        QHBoxLayout *layout;
        AboutWidget *aboutWidget;
        ScrollItemsWidget *clipboardWidget;
        QPropertyAnimation *searchShowAnim;
        QPropertyAnimation *searchHideAnim;
        QSystemTrayIcon *trayIcon;
        QMenu *menu;
    } ui_;

    // 剪贴板相关
    struct {
        ClipboardMonitor *monitor;
        QMimeData *mimeData;
        bool isPasting;
    } clipboard_;

    // 其他组件
    struct {
        QMediaPlayer *player;
        QList<int> numKeyList;
    } misc_;

    static constexpr int HIDE_ANIMATION_TIME = 50;
};

#endif // MPASTEWIDGET_H
