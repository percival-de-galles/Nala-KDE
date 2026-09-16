#include "cursor.h"
#include <QCursor>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <unistd.h>

Cursor::Cursor(QObject *parent) : QObject(parent) {
  m_nativeGlobal = QGuiApplication::platformName() == QStringLiteral("xcb");
  const auto env = QProcessEnvironment::systemEnvironment();
  const QString signature = env.value("HYPRLAND_INSTANCE_SIGNATURE");
  const QString runtime = env.value(
      "XDG_RUNTIME_DIR",
      QStringLiteral("/run/user/%1").arg(::getuid()));
  if (!signature.isEmpty())
    m_socket = runtime + "/hypr/" + signature + "/.socket.sock";

  m_timer.setInterval(55); // ~18 Hz: smooth for a glance, invisible in `top`
  connect(&m_timer, &QTimer::timeout, this, &Cursor::poll);

  // This is intentionally best-effort: on non-KDE desktops no caller exists
  // and the existing compositor/X11 paths remain the fallback.
  if (QDBusConnection::sessionBus().registerService("org.nala.Cursor"))
    QDBusConnection::sessionBus().registerObject(
        "/Cursor", this, QDBusConnection::ExportAllSlots);
}

void Cursor::setActive(bool active) {
  if (m_active == active)
    return;
  m_active = active;
  if (active && (m_nativeGlobal || !m_socket.isEmpty())) {
    m_failures = 0;
    m_timer.start();
    poll();
  } else {
    m_timer.stop();
  }
}

void Cursor::setPollInterval(int ms) { m_timer.setInterval(ms); }

void Cursor::refresh() {
  if (m_active && (m_nativeGlobal || !m_socket.isEmpty()))
    poll();
}

void Cursor::setAvailable(bool available) {
  if (m_available == available)
    return;
  m_available = available;
  emit availableChanged();
}

void Cursor::poll() {
  // Do not overwrite fresh compositor coordinates with XWayland's partial
  // pointer view.  Fall back if the KWin script is later stopped.
  if (m_compositorClock.isValid() && m_compositorClock.elapsed() < 500)
    return;

  if (m_nativeGlobal) {
    const QPoint position = QCursor::pos();
    m_failures = 0;
    setAvailable(true);
    report(position);
    return;
  }

  QLocalSocket socket;
  socket.connectToServer(m_socket);
  if (!socket.waitForConnected(20)) {
    if (++m_failures > 5) {
      setAvailable(false);
      m_timer.stop(); // stop knocking on a door that is not there
    }
    return;
  }

  socket.write("cursorpos");
  socket.flush();
  if (!socket.waitForReadyRead(25)) {
    ++m_failures;
    return;
  }

  const QString reply = QString::fromUtf8(socket.readAll()).trimmed();
  const QStringList parts = reply.split(u',');
  if (parts.size() != 2)
    return;

  bool okX = false, okY = false;
  const int x = parts[0].trimmed().toInt(&okX);
  const int y = parts[1].trimmed().toInt(&okY);
  if (!okX || !okY)
    return;

  m_failures = 0;
  setAvailable(true);
  const QPoint position(x, y);
  if (position == m_position)
    return;
  report(position);
}

void Cursor::inject(QPoint position) {
  setAvailable(true);
  m_position = position;
  emit moved(m_position);
}

void Cursor::setCompositorPosition(int x, int y) {
  m_compositorClock.restart();
  setAvailable(true);
  report(QPoint(x, y));
}

void Cursor::report(QPoint position) {
  if (position == m_position)
    return;
  m_position = position;
  emit moved(m_position);
}
