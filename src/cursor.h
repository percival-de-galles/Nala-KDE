#pragma once
#include <QObject>
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
  Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
  explicit Cursor(QObject *parent = nullptr);

  bool available() const { return m_available; }
  QPoint position() const { return m_position; }

  void setActive(bool active);

  // Test seam: pretend the compositor reported this position.
  void inject(QPoint position);

signals:
  void moved(QPoint position);
  void availableChanged();

private:
  void poll();
  void setAvailable(bool available);

  QString m_socket;
  QPoint m_position;
  QTimer m_timer;
  bool m_available = false;
  bool m_active = false;
  int m_failures = 0;
};
