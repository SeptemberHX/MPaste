// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 MPasteWidget 的声明接口。
// pos: widget 层中的 MPasteWidget 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#ifndef MPASTEWIDGET_H
#define MPASTEWIDGET_H

#include <QAbstractButton>
#include <QWidget>
#include <QHBoxLayout>
#include <QMimeData>
#include <QMenu>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QPropertyAnimation>
#include <QSystemTrayIcon>

#include "utils/ClipboardMonitor.h"
#include "ClipboardItemWidget.h"
#include "data/ClipboardItem.h"
#include "data/LocalSaver.h"
#include "AboutWidget.h"
#include "ClipboardItemDetailsDialog.h"
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

    bool setClipboard(const ClipboardItem &item, bool plainText = false);
    QMimeData *createPlainTextMimeData(const ClipboardItem &item) const;
    void handleUrlsClipboard(QMimeData *mimeData, const ClipboardItem &item);
    void loadFromSaveDir();

    void setFocusOnSearch(bool flag);
    void handleSearchInput(QKeyEvent *event);

    void handleKeyboardEvent(QKeyEvent *event);
    void handleEscapeKey();
    void handleEnterKey(bool plainText = false);
    void handleNavigationKeys(QKeyEvent *event);
    void handleHomeEndKeys(QKeyEvent *event);
    void handleTabKey();
    bool triggerShortcutPaste(int shortcutIndex, bool plainText);

    ScrollItemsWidget* currItemsWidget();

private:
    struct {
        Ui::MPasteWidget *ui;
        QHBoxLayout *layout;
        AboutWidget *aboutWidget;
        ClipboardItemDetailsDialog *detailsDialog;
        MPasteSettingsWidget *settingsWidget;

        QMap<QString, ScrollItemsWidget*> boardWidgetMap;

        QButtonGroup *buttonGroup;

        QButtonGroup *typeButtonGroup;

        ScrollItemsWidget *clipboardWidget;

        ScrollItemsWidget *staredWidget;

        QPropertyAnimation *searchShowAnim;
        QPropertyAnimation *searchHideAnim;
        QSystemTrayIcon *trayIcon;
        QMenu *menu;
    } ui_;

    struct {
        ClipboardMonitor *monitor;
        bool isPasting = false;
        bool copiedWhenHide = false;
    } clipboard_;

    struct {
        QMediaPlayer *player;
        QAudioOutput *audioOutput = nullptr;
        QMediaDevices *mediaDevices = nullptr;
        QList<int> numKeyList;
        int pendingNumKey = -1;
        bool pendingPlainTextNumKey = false;
        qint64 lastSoundPlayAtMs = 0;
    } misc_;

    static constexpr int HIDE_ANIMATION_TIME = 50;
    static constexpr qint64 SOUND_BURST_WINDOW_MS = 500;
};


#endif // MPASTEWIDGET_H
