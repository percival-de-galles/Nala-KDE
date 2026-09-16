#pragma once
#include <QColor>
#include <QQuickItem>
#include <QVector>

// The comet trail Nala leaves when she dashes across the desktop.
//
// A fan of coloured ribbons streaming back from her, each arcing out to one
// side and tapering at both ends. It is drawn in her own window rather than
// across the screen: the reference's trail is only about one body-diameter
// long, which fits inside the window she already has.
class Trail : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(qreal angle READ angle WRITE setAngle NOTIFY changed)
  Q_PROPERTY(qreal length READ length WRITE setLength NOTIFY changed)
  Q_PROPERTY(qreal intensity READ intensity WRITE setIntensity NOTIFY changed)
  QML_ELEMENT

public:
  explicit Trail(QQuickItem *parent = nullptr);

  qreal angle() const { return m_angle; }     // direction of travel, radians
  qreal length() const { return m_length; }   // 0..1 of the item's half-extent
  qreal intensity() const { return m_intensity; }

  void setAngle(qreal angle);
  void setLength(qreal length);
  void setIntensity(qreal intensity);

signals:
  void changed();

protected:
  QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
  struct Ribbon {
    qreal offset;    // how far it swings to the side, as a fraction of length
    qreal thickness; // stroke width, fraction of the item's half-extent
    qreal lead;      // where along the trail it starts
    QColor color;
  };
  QVector<Ribbon> m_ribbons;
  qreal m_angle = 0.0, m_length = 0.0, m_intensity = 0.0;
};
