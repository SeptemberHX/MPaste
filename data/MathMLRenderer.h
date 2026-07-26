// input: A MathML XML string (Presentation MathML, optionally wrapped in
//        <semantics>...<annotation>).
// output: A QPixmap rendered using QPainter for use as a card thumbnail.
// pos: data layer — pure rendering helper, no I/O, no Qt non-GUI deps
//      beyond Widgets/Xml.
// update: If I change, update my folder README.md.
#ifndef MPASTE_MATHMLRENDERER_H
#define MPASTE_MATHMLRENDERER_H

#include <QPixmap>
#include <QString>

class MathMLRenderer {
public:
    // Render a MathML XML string into a QPixmap sized to the canonical card
    // preview dimensions (kCardPreviewWidth × kCardPreviewHeight). Returns a
    // null QPixmap on parse error or fully unsupported content. Coverage
    // targets the common Presentation MathML subset (mi/mn/mo/mtext, mrow,
    // msub/msup/msubsup, mfrac, msqrt/mroot, munder/mover/munderover,
    // mfenced, mtable/mtr/mtd) — sufficient for ~80-90% of casually copied
    // MathType / MathML formulas. Anything more exotic falls back
    // gracefully (degenerates to row layout).
    static QPixmap render(const QString &mathmlSource, bool darkTheme = false);

    // Render at a custom target size (in logical pixels). The font size is
    // scaled up so the formula fills the canvas at high resolution — useful
    // for the preview dialog, where naively upscaling the card thumbnail
    // looks blurry.
    static QPixmap renderAt(const QString &mathmlSource, const QSize &targetSize, bool darkTheme = false);
};

#endif  // MPASTE_MATHMLRENDERER_H
