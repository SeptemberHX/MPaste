// input: Depends on CardData and CardRenderUtils.
// output: Paints the middle of a card in the Poster style: each kind of content set as a poster of its own.
// pos: Widget-layer card painter, used by ClipboardCardDelegate when the card style is Poster.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_POSTERCARD_H
#define MPASTE_POSTERCARD_H

#include <QColor>
#include <QModelIndex>
#include <QPainterPath>
#include <QRect>
#include <QString>

class QPainter;
class ClipboardCardDelegate;
struct CardData;

namespace PosterCard {

// What the delegate has worked out about the item.
struct Words {
    QString kind;       // "Code", "Link", "Image"...
    QString count;      // "58 chars", "1400 × 1550", a file's size and folder
    bool code = false;
    bool darkTheme = false;   // rich text's picture is drawn in the theme's ink
};

// Paints the poster into `panel`, the card below its header down to its foot
// (the delegate draws the header and lays the caption pill over the foot, the
// same on every card, and clips the painter to the panel). A poster carries its own colours, the same in
// the light and the dark theme.
void paint(QPainter *painter, const QRect &panel, const CardData &card, const Words &words,
           const QColor &appTone, int scale, qreal dpr, int loadingPhase,
           const QModelIndex &index, const ClipboardCardDelegate *delegate);

} // namespace PosterCard

#endif // MPASTE_POSTERCARD_H
