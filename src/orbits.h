#pragma once
#include <QColor>
#include <QQuickItem>
#include <QVector>
#include <QVector3D>

// The coloured arcs that swirl around Nala while she is thinking.
//
// Each arc is a circle in 3D with its own axis, spin and hue. Every frame the
// circle is projected to screen space; the half with z behind the body is drawn
// under her and the half in front is drawn over her, which is what sells the
// arcs as orbiting rather than as a flat halo.
class Orbits : public QObject {
  Q_OBJECT
  Q_PROPERTY(qreal intensity READ intensity WRITE setIntensity NOTIFY changed)

public:
  struct Ring {
    QVector3D axis;   // unit normal of the orbital plane
    qreal radius;     // fraction of the item's half-extent
    qreal phase;      // current rotation about `axis`
    qreal speed;      // radians per second
    qreal span;       // angular extent of the visible arc, radians
    qreal thickness;  // stroke width in item pixels at scale 1
    QColor color;
  };

  explicit Orbits(QObject *parent = nullptr);

  qreal intensity() const { return m_intensity; }
  void setIntensity(qreal value);

  Q_INVOKABLE void advance(qreal dt);
  const QVector<Ring> &rings() const { return m_rings; }

  // Sample `ring` at parameter t in [0,1] across its arc. Returns the point in
  // unit item space (-1..1) with `depth` set to the viewer-facing z component.
  static QPointF sample(const Ring &ring, qreal t, qreal *depth);

signals:
  void changed();

private:
  QVector<Ring> m_rings;
  qreal m_intensity = 0.0;
};

// Draws one depth half of the orbit system. Two of these sandwich the body.
class OrbitLayer : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(Orbits *source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(bool front READ front WRITE setFront NOTIFY frontChanged)
  Q_PROPERTY(qreal scale2 READ scale2 WRITE setScale2 NOTIFY scale2Changed)
  QML_ELEMENT

public:
  explicit OrbitLayer(QQuickItem *parent = nullptr);

  Orbits *source() const { return m_source; }
  void setSource(Orbits *source);
  bool front() const { return m_front; }
  void setFront(bool front);
  qreal scale2() const { return m_scale; }
  void setScale2(qreal scale);

signals:
  void sourceChanged();
  void frontChanged();
  void scale2Changed();

protected:
  QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
  Orbits *m_source = nullptr;
  bool m_front = false;
  qreal m_scale = 1.0;
};
