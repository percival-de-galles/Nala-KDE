#pragma once
#include <QColor>
#include <QPointF>
#include <QVector>
#include <cmath>

// Shared geometry for the coloured strokes Nala draws around herself: the
// orbit arcs and the trail she leaves when she dashes. Both are tapered,
// fading polylines, so both build the same triangles.
namespace stroke {

struct Vertex {
  float x, y;
  uchar r, g, b, a;
};

inline uchar toByte(qreal v) {
  return uchar(qBound(0.0, v, 1.0) * 255.0 + 0.5);
}

// Append one quad of stroke between `from` and `to`. Widths and alphas are
// given per end so a run of segments can taper and fade along its length.
// Colours are premultiplied, which is what QSGVertexColorMaterial expects.
inline void appendSegment(QVector<Vertex> &out, const QPointF &from,
                          const QPointF &to, qreal halfFrom, qreal halfTo,
                          const QColor &colour, qreal alphaFrom,
                          qreal alphaTo) {
  QPointF along = to - from;
  const qreal length = std::hypot(along.x(), along.y());
  if (length < 1e-4)
    return;
  along /= length;
  const QPointF normal(-along.y(), along.x());

  const auto push = [&](const QPointF &at, qreal alpha) {
    alpha = qBound(0.0, alpha, 1.0);
    out.append({float(at.x()), float(at.y()),
                toByte(colour.redF() * alpha), toByte(colour.greenF() * alpha),
                toByte(colour.blueF() * alpha), toByte(alpha)});
  };

  const QPointF a0 = from + normal * halfFrom, a1 = from - normal * halfFrom;
  const QPointF b0 = to + normal * halfTo, b1 = to - normal * halfTo;
  push(a0, alphaFrom);
  push(a1, alphaFrom);
  push(b0, alphaTo);
  push(b0, alphaTo);
  push(a1, alphaFrom);
  push(b1, alphaTo);
}

} // namespace stroke
