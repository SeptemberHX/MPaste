// input: Depends on Qt Widgets, board widgets, clipboard monitor, settings, and platform helpers.
// output: Exposes the main MPaste window API, clipboard write helpers, and reliable keyboard-driven paste flow.
// pos: Widget-layer main window declaration coordinating boards, shortcuts, and major UI features.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTEWIDGET_H
#define MPASTEWIDGET_H

#include <QAbstractButton>
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
#include "MPasteSettingsWidget.h"
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
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool focusNextPrevChild(bool next) override;

signals:
    void shortcutChanged(const QString &newShortcut);

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
    bool setClipboard(const ClipboardItem &item, bool plainText = false);
    QMimeData *createPlainTextMimeData(const ClipboardItem &item) const;
    void handleUrlsClipboard(QMimeData *mimeData, const ClipboardItem &item);
    void loadFromSaveDir();

    // 搜索操作
    void setFocusOnSearch(bool flag);
    void handleSearchInput(QKeyEvent *event);

    // 键盘事件处理
    void handleKeyboardEvent(QKeyEvent *event);
    void handleEscapeKey();
    void handleEnterKey(bool plainText = false);
    void handleNavigationKeys(QKeyEvent *event);
    void handleHomeEndKeys(QKeyEvent *event);
    void handleTabKey();
    bool triggerShortcutPaste(int shortcutIndex, bool plainText);

    // 辅助方法
    ScrollItemsWidget* currItemsWidget();

private:
    // UI 组件
    struct {
        Ui::MPasteWidget *ui;
        QHBoxLayout *layout;
        AboutWidget *aboutWidget;
        MPasteSettingsWidget *settingsWidget;

        // 存储所有的 boardWidget
        QMap<QString, ScrollItemsWidget*> boardWidgetMap;

        // 存储切换的 button
        QButtonGroup *buttonGroup;

        // 类型过滤按钮组
        QButtonGroup *typeButtonGroup;

        // 剪贴板，必须要存在
        ScrollItemsWidget *clipboardWidget;

        // 收藏夹，必须要存在
        ScrollItemsWidget *staredWidget;

        QPropertyAnimation *searchShowAnim;
        QPropertyAnimation *searchHideAnim;
        QSystemTrayIcon *trayIcon;
        QMenu *menu;
    } ui_;

    // 剪贴板相关
    struct {
        ClipboardMonitor *monitor;
        bool isPasting = false;
        bool copiedWhenHide = false;
    } clipboard_;

    // 其他组件
    struct {
        QMediaPlayer *player;
        QList<int> numKeyList;
        int pendingNumKey = -1;
        bool pendingPlainTextNumKey = false;
        qint64 lastSoundPlayAtMs = 0;  // 添加这个变量来跟踪按下的数字键
    } misc_;

    static constexpr int HIDE_ANIMATION_TIME = 50;
};


#endif // MPASTEWIDGET_H
