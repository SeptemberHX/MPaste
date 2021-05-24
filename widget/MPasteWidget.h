#ifndef MPASTEWIDGET_H
#define MPASTEWIDGET_H

#include <QWidget>
#include "ClipboardMonitor.h"
#include "ClipboardItemWidget.h"
#include "ClipboardItem.h"
#include "data/LocalSaver.h"
#include <QHBoxLayout>
#include <QMimeData>
#include <QMenu>
#include <QMediaPlayer>
#include "AboutWidget.h"

namespace Ui {
class MPasteWidget;
}

class MPasteWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MPasteWidget(QWidget *parent = nullptr);
    ~MPasteWidget();

    bool eventFilter(QObject *watched, QEvent *event) override;
protected:
    void keyPressEvent(QKeyEvent *event) override;

    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void itemClicked();
    void itemDoubleClicked();
    void clipboardUpdated(ClipboardItem item, int wId);
    void filterByKeyword(const QString &keyword);

private:
    void setFocusOnSearch(bool flag);
    void setFirstVisibleItemSelected();

    void setAllItemVisible();

    bool addOneItem(const ClipboardItem &item);
    void removeOneItemByWidget(ClipboardItemWidget *widget);
    void moveItemToFirst(ClipboardItemWidget *widget);

    void setSelectedItem(ClipboardItemWidget *item);
    void setClipboard(const ClipboardItem &item);

    void checkSaveDir();
    void loadFromSaveDir();
    void saveItem(const ClipboardItem &item);
    QString getItemFilePath(const ClipboardItem &item);

    Ui::MPasteWidget *ui;
    QHBoxLayout *layout;
    ClipboardMonitor *monitor;

    ClipboardItemWidget *currItemWidget;
    QMimeData *mimeData;

    QMenu *menu;

    QMediaPlayer *player;
    QList<int> numKeyList;

    LocalSaver *saver;
    AboutWidget *aboutWidget;
};

#endif // MPASTEWIDGET_H
