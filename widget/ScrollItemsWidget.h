#ifndef SCROLLITEMSWIDGET_H
#define SCROLLITEMSWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <data/LocalSaver.h>
#include "data/ClipboardItem.h"
#include "ClipboardItemWidget.h"

namespace Ui {
class ScrollItemsWidget;
}

class ScrollItemsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScrollItemsWidget(const QString &category, const QString &borderColor, QWidget *parent = nullptr);
    ~ScrollItemsWidget();

    bool addOneItem(const ClipboardItem &item);
    bool addAndSaveItem(const ClipboardItem &item);
    void filterByKeyword(const QString &keyword);
    void setShortcutInfo();
    void cleanShortCutInfo();
    void loadFromSaveDir();
    QScrollBar* horizontalScrollbar();
    void setAllItemVisible();
    const ClipboardItem& selectedByShortcut(int visibleOrder);
    void selectedByEnter();
    void focusMoveLeft();
    void focusMoveRight();
    int getItemCount();

    void scrollToFirst();
    void scrollToLast();
    QString getCategory() const;

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void doubleClicked(const ClipboardItem &item);
    void itemCountChanged(int itemCount);
    void itemStared(const ClipboardItem &item);

private slots:
    void itemClicked();
    void itemDoubleClicked();

private:
    void setSelectedItem(ClipboardItemWidget *item);
    QString getItemFilePath(const ClipboardItem &item);
    void setFirstVisibleItemSelected();
    void saveItem(const ClipboardItem &item);
    void checkSaveDir();
    void moveItemToFirst(ClipboardItemWidget *widget);
    QString saveDir();

    Ui::ScrollItemsWidget *ui;
    QHBoxLayout *layout;
    QString category;
    QString borderColor;

    ClipboardItemWidget *currItemWidget;
    LocalSaver *saver;

};

#endif // SCROLLITEMSWIDGET_H
