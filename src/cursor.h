#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QPoint>
#include <QTimer>

// Global pointer position, so Nala can follow the cursor even when it is
// nowhere near her window.
//
// Wayland deliberately denies clients the global pointer, so this asks the
// compositor instead: Hyprland answers a `cursorpos` request on its IPC socket.
// Everywhere else the tracker simply reports itself unavailable and Nala falls
// back to looking at the cursor only while it is over her.
class Cursor : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.nala.Cursor")
  Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
  explicit Cursor(QObject *parent = nullptr);

  bool available() const { return m_available; }
  QPoint position() const { return m_position; }

  void setActive(bool active);

  // Dragging needs a fresh global pointer position at mouse-event rate, not at
  // the 18 Hz that is ample for a glance.
  void setPollInterval(int ms);
  void refresh();

  // Test seam: pretend the compositor reported this position.
  void inject(QPoint position);

public slots:
  // KWin owns the only global pointer on a Wayland desktop.  Its tiny script
  // calls this over the session bus, including while another app owns focus.
  void setCompositorPosition(int x, int y);

signals:
  void moved(QPoint position);
  void availableChanged();

private:
  void poll();
  void report(QPoint position);
  void setAvailable(bool available);

  QString m_socket;
  QPoint m_position;
  QTimer m_timer;
  QElapsedTimer m_compositorClock;
  bool m_nativeGlobal = false;
  bool m_available = false;
  bool m_active = false;
  int m_failures = 0;
};
