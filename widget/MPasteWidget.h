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
    // 闁告帗绻傞～鎰板礌閺嶎偅绁查柛?
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

    // 闁告搩浜ｉ崚娑㈠级閹稿孩鎯欏ù?
    bool setClipboard(const ClipboardItem &item, bool plainText = false);
    QMimeData *createPlainTextMimeData(const ClipboardItem &item) const;
    void handleUrlsClipboard(QMimeData *mimeData, const ClipboardItem &item);
    void loadFromSaveDir();

    // 闁瑰吋绮庨崒銊╁箼瀹ュ嫮绋?
    void setFocusOnSearch(bool flag);
    void handleSearchInput(QKeyEvent *event);

    // 闂佹鍠氬ú蹇旂鐎ｂ晜顐藉璺哄閹?
    void handleKeyboardEvent(QKeyEvent *event);
    void handleEscapeKey();
    void handleEnterKey(bool plainText = false);
    void handleNavigationKeys(QKeyEvent *event);
    void handleHomeEndKeys(QKeyEvent *event);
    void handleTabKey();
    bool triggerShortcutPaste(int shortcutIndex, bool plainText);

    // 閺夊牆鎳庢慨顏堝棘鐟欏嫮銆?
    ScrollItemsWidget* currItemsWidget();

private:
    // UI 缂備礁瀚▎?
    struct {
        Ui::MPasteWidget *ui;
        QHBoxLayout *layout;
        AboutWidget *aboutWidget;
        ClipboardItemDetailsDialog *detailsDialog;
        MPasteSettingsWidget *settingsWidget;

        // 閻庢稒锚閸嬪秹骞嶉埀顒勫嫉婢跺本鐣?boardWidget
        QMap<QString, ScrollItemsWidget*> boardWidgetMap;

        // 閻庢稒锚閸嬪秹宕氶崶銊ュ簥闁?button
        QButtonGroup *buttonGroup;

        // 缂侇偉顕ч悗閿嬫交閸ャ劍濮㈤柟绋款樀閹稿磭绱?
        QButtonGroup *typeButtonGroup;

        // 闁告搩浜ｉ崚娑㈠级閸栵紕绀夐煫鍥ф嚇閵嗗繒鎲版担鍝ユ憼闁?
        ScrollItemsWidget *clipboardWidget;

        // 闁衡偓閹増顥戝鍓佹缁辨繆绠涢崨娣偓蹇曟啺娴ｅ摜鎽犻柛?
        ScrollItemsWidget *staredWidget;

        QPropertyAnimation *searchShowAnim;
        QPropertyAnimation *searchHideAnim;
        QSystemTrayIcon *trayIcon;
        QMenu *menu;
    } ui_;

    // 闁告搩浜ｉ崚娑㈠级鐠恒劍绁查柛?
    struct {
        ClipboardMonitor *monitor;
        bool isPasting = false;
        bool copiedWhenHide = false;
    } clipboard_;

    // 闁稿繑婀圭划顒傜磼閸曨亝顐?
    struct {
        QMediaPlayer *player;
        QList<int> numKeyList;
        int pendingNumKey = -1;
        bool pendingPlainTextNumKey = false;
        qint64 lastSoundPlayAtMs = 0;
    } misc_;

    static constexpr int HIDE_ANIMATION_TIME = 50;
    static constexpr qint64 SOUND_BURST_WINDOW_MS = 500;
};


#endif // MPASTEWIDGET_H
