#include "orbits.h"
#include "stroke.h"
#include <QRandomGenerator>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QtMath>

namespace {

// Points sampled along each arc. 72 is where the curve stops visibly faceting
// at the sizes Nala is actually drawn.
constexpr int kSamples = 72;

// Hue sweep, saturation and value measured from the reference arcs.
constexpr qreal kSaturation = 0.55;
constexpr qreal kValue = 0.84;

QVector3D orthogonal(const QVector3D &v) {
  const QVector3D pick = std::abs(v.x()) < 0.9f ? QVector3D(1, 0, 0)
                                                : QVector3D(0, 1, 0);
  return QVector3D::crossProduct(v, pick).normalized();
}

} // namespace

Orbits::Orbits(QObject *parent) : QObject(parent) {
  auto *rng = QRandomGenerator::global();
  const int count = 7;
  m_rings.reserve(count);
  for (int i = 0; i < count; ++i) {
    Ring ring;
    // Spread the axes so no two rings share a plane, but keep every axis
    // tilted well away from the screen plane: an axis lying in it would make
    // the ring edge-on, and it would draw as a straight line rather than an
    // orbit.
    const qreal a = rng->generateDouble() * 2.0 * M_PI;
    // |z| near 1 would face the viewer as a plain circle, near 0 would be
    // edge-on; the middle of that range gives the foreshortened ellipses the
    // reference sweeps through.
    qreal z = 0.18 + rng->generateDouble() * 0.54;
    if (rng->bounded(2))
      z = -z;
    const qreal r = std::sqrt(std::max(0.0, 1.0 - z * z));
    ring.axis = QVector3D(float(r * std::cos(a)), float(r * std::sin(a)),
                          float(z))
                    .normalized();
    // A wide spread of radii: in the reference some arcs hug her and others
    // swing right out, which is what stops the cluster reading as a coil.
    ring.radius = 0.58 + rng->generateDouble() * 0.46;
    ring.phase = rng->generateDouble() * 2.0 * M_PI;
    ring.speed = (rng->generateDouble() * 0.9 + 0.55) *
                 (rng->bounded(2) ? 1.0 : -1.0);
    // Close to a complete orbit: short arcs read as scribbles rather than as
    // something circling her.
    ring.span = M_PI * (1.55 + rng->generateDouble() * 0.45);
    // A fraction of the item's half-extent, so the arcs keep their weight
    // when Nala is scaled up or down.
    // Measured against the reference: roughly 1% of the item's half-extent.
    ring.thickness = 0.010 + rng->generateDouble() * 0.011;
    ring.color = QColor::fromHsvF(qreal(i) / count, kSaturation, kValue);
    m_rings.append(ring);
  }
}

void Orbits::setIntensity(qreal value) {
  value = qBound(0.0, value, 1.0);
  if (qFuzzyCompare(m_intensity + 1.0, value + 1.0))
    return;
  m_intensity = value;
  emit changed();
}

void Orbits::advance(qreal dt) {
  if (m_intensity <= 0.001)
    return;
  for (Ring &ring : m_rings)
    ring.phase += ring.speed * dt;
  emit changed();
}

QPointF Orbits::sample(const Ring &ring, qreal t, qreal *depth) {
  // Build an orthonormal frame for the orbital plane, then walk around it.
  const QVector3D u = orthogonal(ring.axis);
  const QVector3D v = QVector3D::crossProduct(ring.axis, u);
  const qreal angle = ring.phase + (t - 0.5) * ring.span;
  const QVector3D p =
      (u * float(std::cos(angle)) + v * float(std::sin(angle))) *
      float(ring.radius);
  if (depth)
    *depth = p.z();
  return QPointF(p.x(), p.y()); // orthographic: drop z
}

OrbitLayer::OrbitLayer(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
}

void OrbitLayer::setSource(Orbits *source) {
  if (m_source == source)
    return;
  if (m_source)
    disconnect(m_source, nullptr, this, nullptr);
  m_source = source;
  if (m_source)
    connect(m_source, &Orbits::changed, this, [this] { update(); });
  emit sourceChanged();
  update();
}

void OrbitLayer::setFront(bool front) {
  if (m_front == front)
    return;
  m_front = front;
  emit frontChanged();
  update();
}

void OrbitLayer::setScale2(qreal scale) {
  if (qFuzzyCompare(m_scale + 1.0, scale + 1.0))
    return;
  m_scale = scale;
  emit scale2Changed();
  update();
}

QSGNode *OrbitLayer::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
  auto *node = static_cast<QSGGeometryNode *>(old);
  if (!node) {
    node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(
        QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGVertexColorMaterial;
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
  }

  QSGGeometry *geometry = node->geometry();
  const qreal intensity = m_source ? m_source->intensity() : 0.0;
  if (!m_source || intensity <= 0.003 || width() <= 0 || height() <= 0) {
    geometry->allocate(0);
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
  }

  const qreal cx = width() * 0.5, cy = height() * 0.5;
  const qreal unit = std::min(width(), height()) * 0.5 * m_scale;

  QVector<stroke::Vertex> vertices;
  vertices.reserve(m_source->rings().size() * kSamples * 6);

  for (const Orbits::Ring &ring : m_source->rings()) {
    // Gather the run of samples that belong to this depth half.
    QPointF previous;
    qreal previousDepth = 0.0;
    bool havePrevious = false;

    for (int i = 0; i < kSamples; ++i) {
      const qreal t = qreal(i) / (kSamples - 1);
      qreal depth = 0.0;
      const QPointF unitPoint = Orbits::sample(ring, t, &depth);
      const QPointF point(cx + unitPoint.x() * unit,
                          cy - unitPoint.y() * unit);

      // A segment belongs to this layer when its midpoint is on our side.
      const qreal midDepth = (depth + previousDepth) * 0.5;
      const bool mine = m_front ? midDepth >= 0.0 : midDepth < 0.0;
      if (havePrevious && mine) {
        // Keep the stroke weight nearly constant and fade only the last
        // stretch at each end, so each arc reads as a continuous ring.
        const qreal taper = std::sin(M_PI * t);
        const qreal ends = qBound(0.0, taper * 3.2, 1.0);
        const qreal half = ring.thickness * unit * 0.5 * (0.8 + 0.2 * ends);
        const qreal alpha = intensity * ends;

        stroke::appendSegment(vertices, previous, point, half, half,
                              ring.color, alpha, alpha);
      }

      previous = point;
      previousDepth = depth;
      havePrevious = true;
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
