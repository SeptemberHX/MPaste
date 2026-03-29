// input: Depends on Qt painting/XML APIs and CardPreviewMetrics constants.
// output: Provides lightweight mxGraphModel XML to QImage rendering.
// pos: utils layer draw.io diagram renderer.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_MXGRAPHRENDERER_H
#define MPASTE_MXGRAPHRENDERER_H

#include <QImage>
#include <QString>

namespace MxGraphRenderer {

bool isMxGraphContent(const QString &text);
QString extractMxGraphXml(const QString &text);
QImage render(const QString &mxGraphXml, const QSize &pixelSize, qreal devicePixelRatio, bool darkMode = false);

} // namespace MxGraphRenderer

#endif // MPASTE_MXGRAPHRENDERER_H
