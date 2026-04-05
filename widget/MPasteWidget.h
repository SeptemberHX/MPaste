// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 MPasteWidget 的声明接口。
// pos: widget 层中的 MPasteWidget 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
// note: Added theme application hooks for dark mode, multi-select count handling, and a manual page-number selector for board browsing.
#ifndef MPASTEWIDGET_H
#define MPASTEWIDGET_H

#include <QAbstractButton>
#include <QWidget>
#include <QHBoxLayout>
#include <QMimeData>
#include <QMenu>
#include <QPointer>
#include <QPropertyAnimation>
#include <QSet>
#include <QSystemTrayIcon>
#include <QElapsedTimer>
#include <QHideEvent>
#include <QTimer>
#include <QLabel>
#include <QComboBox>

#include "data/ClipboardItem.h"
#include "AboutWidget.h"
#include "ClipboardItemDetailsDialog.h"
#include "ClipboardItemPreviewDialog.h"
#include "MPasteSettingsWidget.h"
#include "ScrollItemsWidget.h"

class ClipboardAppController;
class ClipboardPasteController;

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
    void hideEvent(QHideEvent *event) override;
    bool focusNextPrevChild(bool next) override;

signals:
    void shortcutChanged(const QString &newShortcut);

private slots:
    void updateItemCount(int itemCount);
    void hideAndPaste();
    void debugKeyState();
    void dumpMemoryStats();

private:
    void initializeWidget();
    void initStyle();
    void initUI();
    void initSearchAnimations();
    void initShortcuts();
    void initSystemTray();
    void initMenu();
    void setupConnections();
    AboutWidget *ensureAboutWidget();
    ClipboardItemDetailsDialog *ensureDetailsDialog();
    ClipboardItemPreviewDialog *ensurePreviewDialog();
    MPasteSettingsWidget *ensureSettingsWidget();
    void applyTheme(bool dark);
    void scheduleStartupWarmup();
    void reloadHistoryBoards();
    void updatePageSelector();
    void updatePageSelectorStyle();

    void setFocusOnSearch(bool flag);
    void handleSearchInput(QKeyEvent *event);

    void handleKeyboardEvent(QKeyEvent *event);
    void handleEscapeKey();
    void handleEnterKey(bool plainText = false);
    void handlePreviewKey();
    void handleNavigationKeys(QKeyEvent *event);
    void handleHomeEndKeys(QKeyEvent *event);
    void handleTabKey();
    bool triggerShortcutPaste(int shortcutIndex, bool plainText);

    ScrollItemsWidget* currItemsWidget();
    void applyScale(int scale);

private:
    struct {
        Ui::MPasteWidget *ui = nullptr;
        QHBoxLayout *layout = nullptr;
        AboutWidget *aboutWidget = nullptr;
        ClipboardItemDetailsDialog *detailsDialog = nullptr;
        ClipboardItemPreviewDialog *previewDialog = nullptr;
        MPasteSettingsWidget *settingsWidget = nullptr;

        QMap<QString, ScrollItemsWidget*> boardWidgetMap;
        QWidget *pageSelectorWidget = nullptr;
        QLabel *pagePrefixLabel = nullptr;
        QComboBox *pageComboBox = nullptr;
        QLabel *pageNumberLabel = nullptr;
        QLabel *pageTotalLabel = nullptr;
        QLabel *pageSuffixLabel = nullptr;

        QButtonGroup *buttonGroup = nullptr;

        QButtonGroup *typeButtonGroup = nullptr;

        ScrollItemsWidget *clipboardWidget = nullptr;

        ScrollItemsWidget *staredWidget = nullptr;

        QPropertyAnimation *searchShowAnim = nullptr;
        QPropertyAnimation *searchHideAnim = nullptr;
        QSystemTrayIcon *trayIcon = nullptr;
        QMenu *menu = nullptr;
        QMenu *trayMenu = nullptr;
        QAction *aboutAction = nullptr;
        QAction *settingsAction = nullptr;
        QAction *quitAction = nullptr;
    } ui_;

    ClipboardAppController *controller_ = nullptr;

    struct {
        bool startupWarmupScheduled = false;
        bool startupWarmupCompleted = false;
    } loading_;

    struct {
        QList<int> numKeyList;
        int pendingNumKey = -1;
        bool pendingPlainTextNumKey = false;
        QElapsedTimer startupPerfTimer;
    } misc_;

    static constexpr int HIDE_ANIMATION_TIME = 50;
    static constexpr int KEEPALIVE_INTERVAL_MS = 3 * 60 * 1000; // 3 minutes

    void startKeepAliveTimer();
    void stopKeepAliveTimer();
    void touchWorkingSet();

    QTimer *keepAliveTimer_ = nullptr;
    bool darkTheme_ = false;
};


#endif // MPASTEWIDGET_H
