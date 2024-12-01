#ifndef MPASTEWIDGET_H
#define MPASTEWIDGET_H

#include <QWidget>
#include "utils/ClipboardMonitor.h"
#include "ClipboardItemWidget.h"
#include "data/ClipboardItem.h"
#include "data/LocalSaver.h"
#include <QHBoxLayout>
#include <QMimeData>
#include <QMenu>
#include <QMediaPlayer>
#include <QPropertyAnimation>
#include <QSystemTrayIcon>
#include "AboutWidget.h"
#include "ScrollItemsWidget.h"

namespace Ui {
class MPasteWidget;
}

class MPasteWidget : public QWidget
{
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
    void setFocusOnSearch(bool flag);
    ScrollItemsWidget* currItemsWidget();

    void setClipboard(const ClipboardItem &item);
    void loadFromSaveDir();

    void initializeWidget();
    bool handleAltNumShortcut(QKeyEvent *event);

    Ui::MPasteWidget *ui;
    QHBoxLayout *layout;
    ClipboardMonitor *monitor;

    QMimeData *mimeData;

    QMenu *menu;

    QMediaPlayer *player;
    QList<int> numKeyList;

    AboutWidget *aboutWidget;

    ScrollItemsWidget *clipboardWidget;

    QPropertyAnimation *searchShowAnim;
    QPropertyAnimation *searchHideAnim;

    QSystemTrayIcon *trayIcon;
};

#endif // MPASTEWIDGET_H
