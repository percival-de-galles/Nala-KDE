#include "trail.h"
#include "stroke.h"

#include <QRandomGenerator>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QtMath>

namespace {
constexpr int kSamples = 26; // points along each ribbon
} // namespace

Trail::Trail(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);

  auto *rng = QRandomGenerator::global();
  const int count = 6;
  m_ribbons.reserve(count);
  for (int i = 0; i < count; ++i) {
    Ribbon ribbon;
    // All of them bow the same way and sit close together. The reference's
    // trail is one slender blade, not a fan opening to both sides.
    ribbon.offset = 0.03 + rng->generateDouble() * 0.09;
    ribbon.thickness = 0.032 + rng->generateDouble() * 0.038;
    // Negative, so each ribbon begins ahead of her and sweeps past: the
    // reference's blade straddles her, with her about a third along it.
    ribbon.lead = -0.48 + rng->generateDouble() * 0.16;
    // Same palette as the orbit arcs.
    ribbon.color = QColor::fromHsvF(qreal(i) / count, 0.55, 0.84);
    m_ribbons.append(ribbon);
  }
}

void Trail::setAngle(qreal angle) {
  if (qFuzzyCompare(m_angle + 1.0, angle + 1.0))
    return;
  m_angle = angle;
  emit changed();
  update();
}

void Trail::setLength(qreal length) {
  length = qBound(0.0, length, 1.0);
  if (qFuzzyCompare(m_length + 1.0, length + 1.0))
    return;
  m_length = length;
  emit changed();
  update();
}

void Trail::setIntensity(qreal intensity) {
  intensity = qBound(0.0, intensity, 1.0);
  if (qFuzzyCompare(m_intensity + 1.0, intensity + 1.0))
    return;
  m_intensity = intensity;
  emit changed();
  update();
}

QSGNode *Trail::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
  auto *node = static_cast<QSGGeometryNode *>(old);
  if (!node) {
    node = new QSGGeometryNode;
    auto *geometry =
        new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new QSGVertexColorMaterial);
    node->setFlag(QSGNode::OwnsMaterial);
  }

  QSGGeometry *geometry = node->geometry();
  if (m_intensity <= 0.004 || m_length <= 0.01 || width() <= 0) {
    geometry->allocate(0);
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
  }

  const qreal unit = std::min(width(), height()) * 0.5;
  const QPointF centre(width() * 0.5, height() * 0.5);
  // Backwards along the direction of travel, and the perpendicular.
  const QPointF back(-std::cos(m_angle), std::sin(m_angle));
  const QPointF side(-back.y(), back.x());

  QVector<stroke::Vertex> vertices;
  vertices.reserve(m_ribbons.size() * kSamples * 6);

  for (const Ribbon &ribbon : m_ribbons) {
    const qreal reach = m_length * unit * 1.2;
    QPointF previous;
    qreal previousHalf = 0.0, previousAlpha = 0.0;

    for (int i = 0; i < kSamples; ++i) {
      const qreal t = qreal(i) / (kSamples - 1);
      // Arc out to the side and come back, so the ribbon reads as a sweep
      // rather than a straight smear.
      const qreal lateral = ribbon.offset * std::sin(M_PI * t) * reach;
      const QPointF at = centre + back * (ribbon.lead * unit + t * reach) +
                         side * lateral;

      const qreal taper = std::sin(M_PI * t);
      const qreal half = ribbon.thickness * unit * 0.5 * (0.25 + 0.75 * taper);
      // Brightest near her, fading away down the tail.
      const qreal alpha = m_intensity * taper * (1.0 - t * 0.55);

      if (i > 0)
        stroke::appendSegment(vertices, previous, at, previousHalf, half,
                              ribbon.color, previousAlpha, alpha);
      previous = at;
      previousHalf = half;
      previousAlpha = alpha;
    }
  }

  geometry->allocate(vertices.size());
  if (!vertices.isEmpty()) {
    auto *target = geometry->vertexDataAsColoredPoint2D();
    for (int i = 0; i < vertices.size(); ++i) {
      const stroke::Vertex &v = vertices[i];
      target[i].set(v.x, v.y, v.r, v.g, v.b, v.a);
    }
  }
  node->markDirty(QSGNode::DirtyGeometry);
  return node;
}
