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

    bool eventFilter(QObject *watched, QEvent *event) override;
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void clipboardUpdated(ClipboardItem item, int wId);

private:
    void setFocusOnSearch(bool flag);
    ScrollItemsWidget* currItemsWidget();

    void setClipboard(const ClipboardItem &item);
    void loadFromSaveDir();

    Ui::MPasteWidget *ui;
    QHBoxLayout *layout;
    ClipboardMonitor *monitor;

    QMimeData *mimeData;

    QMenu *menu;

    QMediaPlayer *player;
    QList<int> numKeyList;

    AboutWidget *aboutWidget;

    ScrollItemsWidget *clipboardWidget;
};

#endif // MPASTEWIDGET_H
