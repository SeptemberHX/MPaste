// input: Depends on Qt delegate painting APIs and ClipboardBoardModel roles.
// output: Exposes the card painter used by the delegate-based clipboard board view.
// pos: Widget-layer delegate that draws clipboard cards without per-row widgets.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDCARDDELEGATE_H
#define MPASTE_CLIPBOARDCARDDELEGATE_H

#include <QColor>
#include <QStyledItemDelegate>

class ClipboardCardDelegate : public QStyledItemDelegate {
public:
    explicit ClipboardCardDelegate(const QColor &borderColor, QObject *parent = nullptr);

    void setLoadingPhase(int phase);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QColor borderColor_;
    int loadingPhase_ = 0;
};

#endif // MPASTE_CLIPBOARDCARDDELEGATE_H
