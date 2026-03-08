// input: 依赖 Qt Widgets、data 层对象与同层组件声明。
// output: 对外提供 ScrollItemsWidget 的声明接口。
// pos: widget 层中的 ScrollItemsWidget 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
#ifndef SCROLLITEMSWIDGET_H
#define SCROLLITEMSWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QHash>
#include <data/LocalSaver.h>
#include "data/ClipboardItem.h"
#include "ClipboardItemWidget.h"

class QPropertyAnimation;
class QWheelEvent;

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
    void filterByType(ClipboardItem::ContentType type);
    void setShortcutInfo();
    void cleanShortCutInfo();
    void loadFromSaveDir();
    QScrollBar* horizontalScrollbar();
    void setAllItemVisible();
    const ClipboardItem* selectedByShortcut(int visibleOrder);
    void selectedByEnter();
    void focusMoveLeft();
    void focusMoveRight();
    int getItemCount();

    void scrollToFirst();
    void scrollToLast();
    QString getCategory() const;
    void removeItemByContent(const ClipboardItem &item);
    void setItemFavorite(const ClipboardItem &item, bool favorite);
    bool handleWheelScroll(QWheelEvent *event);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void doubleClicked(const ClipboardItem &item);
    void itemCountChanged(int itemCount);
    void itemStared(const ClipboardItem &item);
    void itemUnstared(const ClipboardItem &item);

private slots:
    void itemClicked();
    void itemDoubleClicked();

private:
    ClipboardItemWidget *createItemWidget(const ClipboardItem &item);
    ClipboardItemWidget *findMatchingWidget(const ClipboardItem &item) const;
    void registerWidgetFingerprint(ClipboardItemWidget *widget);
    void unregisterWidgetFingerprint(ClipboardItemWidget *widget);
    void setSelectedItem(ClipboardItemWidget *item);
    QString getItemFilePath(const ClipboardItem &item);
    void setFirstVisibleItemSelected();
    void applyFilters();
    void saveItem(const ClipboardItem &item);
    void checkSaveDir();
    void moveItemToFirst(ClipboardItemWidget *widget);
    QString saveDir();
    void animateScrollTo(int targetValue);
    int wheelStepPixels() const;
    void loadNextBatch(int batchSize);
    void ensureAllItemsLoaded();
    void maybeLoadMoreItems();
    int itemCountForDisplay() const;
    void trimToMaxSize();

    Ui::ScrollItemsWidget *ui;
    QHBoxLayout *layout;
    QString category;
    QString borderColor;

    ClipboardItemWidget *currItemWidget;
    LocalSaver *saver;
    QPropertyAnimation *scrollAnimation;
    QHash<QByteArray, QList<ClipboardItemWidget*>> fingerprintBuckets_;
    QStringList pendingLoadFilePaths_;
    int totalItemCount_ = 0;

    QString currentKeyword_;
    ClipboardItem::ContentType currentTypeFilter_ = ClipboardItem::All;

    static constexpr int INITIAL_LOAD_BATCH_SIZE = 24;
    static constexpr int LOAD_BATCH_SIZE = 16;
    static constexpr int LOAD_MORE_THRESHOLD_PX = 640;

};

#endif // SCROLLITEMSWIDGET_H
