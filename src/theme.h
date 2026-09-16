#pragma once
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

// Live bridge to Noctalia's generated theme. Noctalia rewrites these files
// whenever the wallpaper (and therefore the palette) changes, so Nala watches
// them and re-emits `changed()` instead of sampling once at start-up.
//
//   <config>/gtk-4.0/noctalia.css   -- @define-color palette tokens
//   <state>/noctalia/settings.toml  -- font, corner radius, animation speed
class Theme : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
  Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
  Q_PROPERTY(qreal radius READ radius NOTIFY changed)
  Q_PROPERTY(qreal motionScale READ motionScale NOTIFY changed)
  Q_PROPERTY(bool available READ available NOTIFY changed)

public:
  Theme(QString configRoot, QString stateRoot, QObject *parent = nullptr);

  QVariantMap colors() const { return m_colors; }
  QString fontFamily() const { return m_font; }
  qreal radius() const { return m_radius; }
  qreal motionScale() const { return m_motion; }
  bool available() const { return m_available; }

  // Colour the mascot takes when it is set to follow the desktop theme.
  QColor mascotColor() const;

  void reload();

signals:
  void changed();

private:
  void watchPaths();
  bool loadPalette();
  void loadShellSettings();

  QString m_config, m_state;
  QString m_font = QStringLiteral("Adwaita Sans");
  QVariantMap m_colors;
  qreal m_radius = 1.0, m_motion = 1.0;
  bool m_available = false;
  QFileSystemWatcher m_watcher;
  QTimer m_debounce;
};
