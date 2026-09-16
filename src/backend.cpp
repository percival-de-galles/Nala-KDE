#include "backend.h"
#include "cursor.h"
#include "mascot.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <cstdlib>
#include <iterator>
#include <QRegion>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>

#ifdef NALA_HAVE_XCB
#ifdef NALA_HAVE_XCB_SHAPE
#include <xcb/shape.h>
#endif
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#endif

#ifdef NALA_HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

namespace {

constexpr qreal kMinSize = 0.55;
constexpr qreal kMaxSize = 2.20;

// The window is deliberately larger than the body so morphs that reach beyond
// the idle silhouette -- the exclamation mark, the orbit rings -- are not
// clipped. The reference reel uses the same headroom.
constexpr qreal kCanvasToBody = 1.89; // 1 / 0.529, the measured body radius
constexpr int kBaseBody = 116; // logical pixels at size 1.0

// Flight. Below this release speed she simply drops where she is put.
constexpr qreal kThrowSpeed = 900.0; // pixels per second
constexpr qreal kDrag = 1.5;         // per second
constexpr qreal kBounce = 0.62;      // energy kept when she hits an edge

QString autostartPath() {
  return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
         "/autostart/nala.desktop";
}

bool moveX11Window(QWindow *window, int x, int y) {
#ifdef NALA_HAVE_XCB
  if (QGuiApplication::platformName() != QStringLiteral("xcb"))
    return false;
  if (auto *native =
          qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
    const uint32_t values[]{uint32_t(x), uint32_t(y)};
    xcb_configure_window(native->connection(), window->winId(),
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    xcb_flush(native->connection());
    return true;
  }
#endif
  return false;
}

bool x11WindowCentre(QWindow *window, QPointF *centre) {
#ifdef NALA_HAVE_XCB
  if (QGuiApplication::platformName() != QStringLiteral("xcb"))
    return false;
  if (auto *native =
          qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
    xcb_connection_t *connection = native->connection();
    const xcb_screen_t *screen =
        xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    const auto cookie = xcb_translate_coordinates(
        connection, window->winId(), screen->root, 0, 0);
    xcb_translate_coordinates_reply_t *reply =
        xcb_translate_coordinates_reply(connection, cookie, nullptr);
    if (!reply)
      return false;
    *centre = QPointF(reply->dst_x + window->width() * 0.5,
                      reply->dst_y + window->height() * 0.5);
    std::free(reply);
    return true;
  }
#endif
  return false;
}

} // namespace
bool setX11InputRegion(QWindow *window, const QRegion &region) {
#if defined(NALA_HAVE_XCB) && defined(NALA_HAVE_XCB_SHAPE)
  if (QGuiApplication::platformName() != QStringLiteral("xcb"))
    return false;
  if (auto *native =
          qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
    QVector<xcb_rectangle_t> rectangles;
    rectangles.reserve(region.rectCount());
    for (const QRect &rect : region)
      rectangles.push_back({int16_t(rect.x()), int16_t(rect.y()),
                            uint16_t(rect.width()), uint16_t(rect.height())});
    xcb_shape_rectangles(native->connection(), XCB_SHAPE_SO_SET,
                         XCB_SHAPE_SK_INPUT, XCB_CLIP_ORDERING_UNSORTED,
                         window->winId(), 0, 0, rectangles.size(),
                         rectangles.constData());
    xcb_flush(native->connection());
    return true;
  }
#else
  Q_UNUSED(window);
  Q_UNUSED(region);
#endif
  return false;
}


Backend::Backend(QString configPath, bool preview, bool testing, Mascot *mascot,
                 Theme *theme, Cursor *cursor, QObject *parent)
    : QObject(parent), m_configPath(std::move(configPath)), m_preview(preview),
      m_testing(testing), m_mascot(mascot), m_theme(theme), m_cursor(cursor) {
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(400); // coalesce slider drags into one write
  connect(&m_saveTimer, &QTimer::timeout, this, &Backend::save);

  load();

  if (m_theme)
    connect(m_theme, &Theme::changed, this, [this] { emit changed(); });

  if (m_cursor && m_mascot) {
    connect(m_cursor, &Cursor::moved, this, [this](QPoint position) {
      if (!m_followCursor)
        return;
      // Scale the gaze to the screen, not to her body. Normalising by her
      // 58 px radius spends 96% of the eye travel within 232 px and leaves
      // the rest of a desktop with no response at all, which reads as the
      // eyes being frozen in one pose.
      const qreal radius = bodyRadius();
      if (radius <= 0.0)
        return;
      const QPointF centre = centreOnScreen();
      // A fixed distance, tied to her size rather than to the desktop: scaling
      // the gain by the screen made the response depend on the monitor layout,
      // which is why it felt right at one distance and wrong either side of it.
      // Half a glance lands at three body radii and keeps climbing after that.
      const qreal gain = std::max(1.0, radius * 3.0);
      m_mascot->lookAt((position.x() - centre.x()) / gain,
                       (position.y() - centre.y()) / gain);
    });

    if (m_mascot)
      connect(m_cursor, &Cursor::moved, this, &Backend::dragToCursor);
  }

  // Connect before applying preferences: setActive() polls once synchronously,
  // and the position it reports must reach the mascot.
  applyToMascot();

  if (!m_testing) {
    connect(qApp, &QGuiApplication::screenAdded, this,
            &Backend::screensChanged);
    connect(qApp, &QGuiApplication::screenRemoved, this,
            &Backend::screensChanged);
  }
}

// --- preferences -----------------------------------------------------------

void Backend::load() {
  QFile file(m_configPath);
  if (!file.open(QIODevice::ReadOnly))
    return;
  const QJsonObject json =
      QJsonDocument::fromJson(file.readAll()).object();

  m_size = qBound(kMinSize, json.value("size").toDouble(m_size), kMaxSize);
  const QString mode = json.value("colorMode").toString(m_colorMode);
  if (mode == "ink" || mode == "theme")
    m_colorMode = mode;
  m_followCursor = json.value("followCursor").toBool(m_followCursor);
  m_idleAntics = json.value("idleAntics").toBool(m_idleAntics);
  m_sleepWhenIdle = json.value("sleepWhenIdle").toBool(m_sleepWhenIdle);
  m_reducedMotion = json.value("reducedMotion").toBool(m_reducedMotion);
  m_stayOnTop = json.value("stayOnTop").toBool(m_stayOnTop);
  m_monitor = json.value("monitor").toString(m_monitor);
  m_place = QPointF(
      qBound(0.0, json.value("x").toDouble(m_place.x()), 1.0),
      qBound(0.0, json.value("y").toDouble(m_place.y()), 1.0));
}

void Backend::save() {
  if (m_testing)
    return; // a measured run must never overwrite real preferences

  QDir().mkpath(QFileInfo(m_configPath).absolutePath());
  const QJsonObject json{
      {"size", m_size},
      {"colorMode", m_colorMode},
      {"followCursor", m_followCursor},
      {"idleAntics", m_idleAntics},
      {"sleepWhenIdle", m_sleepWhenIdle},
      {"reducedMotion", m_reducedMotion},
      {"stayOnTop", m_stayOnTop},
      {"monitor", m_monitor},
      {"x", m_place.x()},
      {"y", m_place.y()},
  };

  QSaveFile file(m_configPath);
  if (!file.open(QIODevice::WriteOnly)) {
    note(QStringLiteral("Could not save preferences."));
    return;
  }
  file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
  if (!file.commit())
    note(QStringLiteral("Could not save preferences."));
}

void Backend::note(const QString &message) {
  m_feedback = message;
  emit feedbackChanged();
}

void Backend::applyToMascot() {
  if (!m_mascot)
    return;
  m_mascot->setReducedMotion(m_reducedMotion);
  m_mascot->setSleepWhenIdle(m_sleepWhenIdle);
  m_mascot->setIdleAntics(m_idleAntics);
  if (m_cursor)
    m_cursor->setActive(m_followCursor && !m_testing);
  if (!m_followCursor)
    m_mascot->lookIdle();
}

void Backend::configure(const QString &key, const QVariant &value) {
  if (key == "size") {
    const qreal wanted = value.toDouble();
    if (wanted < kMinSize || wanted > kMaxSize)
      return; // out of range: keep the current value rather than clamping
    m_size = wanted;
  } else if (key == "colorMode") {
    const QString mode = value.toString();
    if (mode != "ink" && mode != "theme")
      return;
    m_colorMode = mode;
  } else if (key == "followCursor") {
    m_followCursor = value.toBool();
  } else if (key == "idleAntics") {
    m_idleAntics = value.toBool();
  } else if (key == "sleepWhenIdle") {
    m_sleepWhenIdle = value.toBool();
  } else if (key == "reducedMotion") {
    m_reducedMotion = value.toBool();
  } else if (key == "stayOnTop") {
    m_stayOnTop = value.toBool();
  } else if (key == "monitor") {
    m_monitor = value.toString();
  } else {
    return;
  }

  applyToMascot();
  applyPlacement();
  emit changed();
  m_saveTimer.start();
}

QColor Backend::mascotColor() const {
  if (m_colorMode == "theme" && m_theme)
    return m_theme->mascotColor();
  return QColor("#0a090c"); // the reference silhouette
}

// --- placement -------------------------------------------------------------

QStringList Backend::screens() const {
  QStringList names;
  for (const QScreen *screen : QGuiApplication::screens())
    names << screen->name();
  return names;
}

QRect Backend::screenGeometry() const {
  const QList<QScreen *> all = QGuiApplication::screens();
  for (QScreen *screen : all)
    if (!m_monitor.isEmpty() && screen->name() == m_monitor)
      return screen->geometry();
  QScreen *primary = QGuiApplication::primaryScreen();
  if (primary)
    return primary->geometry();
  return all.isEmpty() ? QRect(0, 0, 1920, 1080) : all.first()->geometry();
}

qreal Backend::windowSize() const {
  return kBaseBody * kCanvasToBody * m_size;
}

qreal Backend::bodyRadius() const {
  return windowSize() / kCanvasToBody * 0.5;
}

// Where Nala actually is, in compositor coordinates.
//
// Deliberately derived from the placement we asked for rather than from
// m_window->x()/y(): a layer-shell surface is positioned by the shell, and the
// QWindow's own coordinates do not describe where it ended up. Reading them
// here is what made the gaze aim at the wrong point.
QPointF Backend::centreOnScreen() const {
  // An ordinary window is placed by the compositor, so ask it where it ended
  // up. A layer-shell surface is not: its QWindow coordinates say nothing
  // about where the shell actually put it, so use the placement we asked for.
  QPointF nativeCentre;
  if (m_window && !m_layered && x11WindowCentre(m_window, &nativeCentre))
    return nativeCentre;

  if (m_window && !m_layered &&
      QGuiApplication::platformName() != QStringLiteral("xcb"))
    return QPointF(m_window->x() + m_window->width() * 0.5,
                   m_window->y() + m_window->height() * 0.5);

  const QRect screen = screenGeometry();
  const qreal extent = windowSize();
  return QPointF(
      screen.x() + m_place.x() * (screen.width() - extent) + extent * 0.5,
      screen.y() + m_place.y() * (screen.height() - extent) + extent * 0.5);
}

void Backend::attach(QQuickWindow *window) {
  m_window = window;
  if (!m_window)
    return;

#ifdef NALA_HAVE_XCB
  if (QGuiApplication::platformName() == QStringLiteral("xcb") &&
      qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
    m_window->setFlag(Qt::X11BypassWindowManagerHint, true);
#endif

  m_window->setFlag(Qt::FramelessWindowHint, true);
  m_window->setColor(Qt::transparent);

#ifdef NALA_HAVE_LAYER_SHELL
  // Must happen while the window is still hidden: LayerShellQt can only give a
  // QWindow the layer-surface role before its platform surface exists. This is
  // what makes Nala a free-floating companion instead of a tiled window.
  if (!m_preview &&
      QGuiApplication::platformName() == QStringLiteral("wayland")) {
    if (auto *layer = LayerShellQt::Window::get(m_window)) {
      layer->setScope(QStringLiteral("nala"));
      layer->setAnchors({LayerShellQt::Window::AnchorTop |
                         LayerShellQt::Window::AnchorLeft});
      layer->setExclusiveZone(-1); // never reserve space from other windows
      layer->setKeyboardInteractivity(
          LayerShellQt::Window::KeyboardInteractivityNone);
      layer->setCloseOnDismissed(false);
      m_layered = true;
    }
  }
#endif

  applyPlacement();
  m_window->setVisible(true);
  if (!m_layered)
    QTimer::singleShot(100, this, [this] { applyPlacement(); });
  applyInputRegion(false);
}

void Backend::applyPlacement() {
  if (!m_window)
    return;

  const int extent = int(std::lround(windowSize()));
  const QRect screen = screenGeometry();
  const int x = int(std::lround(m_place.x() * (screen.width() - extent)));
  const int y = int(std::lround(m_place.y() * (screen.height() - extent)));

#ifdef NALA_HAVE_LAYER_SHELL
  if (m_layered) {
    if (auto *layer = LayerShellQt::Window::get(m_window)) {
      QScreen *target = nullptr;
      for (QScreen *candidate : QGuiApplication::screens())
        if (!m_monitor.isEmpty() && candidate->name() == m_monitor)
          target = candidate;
      // Always name an output. Left to itself the compositor may choose any
      // screen, and a client is never told which one it got, so
      // centreOnScreen() would then be a monitor out and the gaze with it.
      if (!target)
        target = QGuiApplication::primaryScreen();
      if (target)
        layer->setScreen(target);

      layer->setLayer(m_stayOnTop ? LayerShellQt::Window::LayerTop
                                  : LayerShellQt::Window::LayerBottom);
      // A layer surface is sized and placed by the shell, not by x/y.
      layer->setDesiredSize(QSize(extent, extent));
      layer->setMargins(QMargins(x, y, 0, 0));
      if (m_window->width() != extent || m_window->height() != extent)
        m_window->resize(extent, extent);
      applyInputRegion(m_dragging);
      return;
    }
  }
#endif

  if (m_window->width() != extent || m_window->height() != extent)
    m_window->resize(extent, extent);
  const QPoint position(screen.x() + x, screen.y() + y);
  m_window->setPosition(position);
  moveX11Window(m_window, position.x(), position.y());
  applyInputRegion(m_dragging);
}

void Backend::applyInputRegion(bool wholeWindow) {
  if (!m_window)
    return;
  const int extent = m_window->width();
  if (extent <= 0)
    return;

  if (wholeWindow) {
    // While she is being dragged the pointer wanders outside her silhouette;
    // widen the input region so the motion and the release still arrive.
    const QRegion region(0, 0, extent, extent);
    if (!setX11InputRegion(m_window, region))
      m_window->setMask(region);
    return;
  }

  // Otherwise only the mascot herself swallows clicks, so the transparent
  // margin around her stays click-through and never blocks the desktop.
  const int body = int(std::lround(extent / kCanvasToBody));
  const int inset = (extent - body) / 2;
  const QRegion region(inset, inset, body, body, QRegion::Ellipse);
  if (!setX11InputRegion(m_window, region))
    m_window->setMask(region);
}

void Backend::resetPlace() {
  m_place = QPointF(0.86, 0.74);
  applyPlacement();
  emit changed();
  m_saveTimer.start();
}

// --- drag ------------------------------------------------------------------

void Backend::grabDrag() {
  if (!m_window || m_dragging)
    return;
  m_dragging = true;
  m_flying = false;
  m_dragSpeed = 0.0;
  m_dragVelocity = QPointF();
  m_dragClock.restart();
  m_haveSample = false;

  // Ask for the global pointer at mouse-event rate. Where the compositor has
  // actually put the surface does not enter into it.
  if (m_cursor) {
    m_cursor->setPollInterval(8);
    m_cursor->setActive(true);
    m_cursor->refresh();
    if (m_cursor->available())
      beginPointerDrag(m_cursor->position());
  }
  applyInputRegion(true);
  if (m_mascot)
    m_mascot->beginDrag();
}

// Follow the global pointer. Because this never consults the surface's own
// coordinates it cannot feed back on itself: a compositor that applies the new
// margin late changes where the window *is*, not where the pointer is.
void Backend::beginPointerDrag(QPoint position) {
  const QPointF centre = centreOnScreen();
  const int extent = m_window->width();
  m_grabOffset = position -
                 QPoint(qRound(centre.x() - extent * 0.5),
                        qRound(centre.y() - extent * 0.5));
  m_lastSample = position;
  m_lastSampleMs = m_dragClock.isValid() ? m_dragClock.elapsed() : 0;
  m_haveSample = true;
}

void Backend::dragToCursor(QPoint position) {
  if (!m_dragging || !m_window)
    return;

  if (!m_haveSample) {
    beginPointerDrag(position);
    return;
  }

  const int extent = m_window->width();
  const QPoint wantedTopLeft = position - m_grabOffset;
  QScreen *screen = QGuiApplication::screenAt(
      wantedTopLeft + QPoint(extent / 2, extent / 2));
  if (!screen)
    screen = QGuiApplication::screenAt(position);
  if (!screen)
    screen = QGuiApplication::primaryScreen();
  if (!screen)
    return;

  m_monitor = screen->name();
  const QRect geometry = screen->geometry();
  const qreal spanX = std::max(1, geometry.width() - extent);
  const qreal spanY = std::max(1, geometry.height() - extent);
  const QPoint local = wantedTopLeft - geometry.topLeft();

  // Velocity from real positions and real intervals, so a pause before the
  // release reads as a drop rather than a throw.
  const qint64 now = m_dragClock.isValid() ? m_dragClock.elapsed() : 0;
  const qreal dt = (now - m_lastSampleMs) / 1000.0;
  if (dt >= 0.2)
    m_dragVelocity = QPointF();
  else if (dt > 0.004)
    m_dragVelocity =
        m_dragVelocity * 0.5 +
        QPointF((position.x() - m_lastSample.x()) / dt,
                (position.y() - m_lastSample.y()) / dt) *
            0.5;
  m_dragSpeed = 0.6 * m_dragSpeed +
                0.4 * std::hypot(qreal(position.x() - m_lastSample.x()),
                                 qreal(position.y() - m_lastSample.y()));
  m_lastSample = position;
  m_lastSampleMs = now;

  const QPointF next(qBound(0.0, local.x() / spanX, 1.0),
                     qBound(0.0, local.y() / spanY, 1.0));
  if (next == m_place)
    return;
  m_place = next;
  applyPlacement();
  emit changed();
}

void Backend::dragBy(qreal dx, qreal dy) {
  if (!m_dragging || !m_window)
    return;
  // With a live global pointer the deltas are ignored entirely; they are only
  // a fallback for a session with no bridge.
  if (m_cursor && m_cursor->available())
    return;

  const QRect screen = screenGeometry();
  const int extent = m_window->width();
  const qreal spanX = std::max(1, screen.width() - extent);
  const qreal spanY = std::max(1, screen.height() - extent);

  m_place = QPointF(qBound(0.0, m_place.x() + dx / spanX, 1.0),
                    qBound(0.0, m_place.y() + dy / spanY, 1.0));

  // Exponential average: a single jittery event should not read as a throw.
  m_dragSpeed = m_dragSpeed * 0.6 + std::hypot(dx, dy) * 0.4;

  // Velocity needs the interval between events, not just their size -- the
  // same 20 px means very different things 5 ms and 80 ms apart.
  const qreal elapsed =
      m_dragClock.isValid() ? m_dragClock.restart() / 1000.0 : 0.0;
  if (elapsed > 0.001 && elapsed < 0.2) {
    const QPointF sample(dx / elapsed, dy / elapsed);
    m_dragVelocity = m_dragVelocity * 0.55 + sample * 0.45;
  }

  applyPlacement();
  emit changed();
}

void Backend::releaseDrag() {
  if (!m_dragging)
    return;
  m_dragging = false;
  applyInputRegion(false);

  if (m_cursor) {
    m_cursor->setPollInterval(55);
    m_cursor->setActive(m_followCursor && !m_testing);
  }

  // Let go of her hard enough and she does not just drop -- she streaks off.
  const qreal speed = std::hypot(m_dragVelocity.x(), m_dragVelocity.y());
  if (speed > kThrowSpeed) {
    launch(m_dragVelocity.x(), m_dragVelocity.y(), speed);
    return;
  }

  if (m_mascot)
    m_mascot->endDrag(qBound(0.0, m_dragSpeed / 26.0, 1.0));
  m_saveTimer.start();
}

void Backend::launch(qreal dx, qreal dy, qreal speed) {
  if (!m_window)
    return;
  const QRect screen = screenGeometry();
  const int extent = m_window->width();
  const qreal spanX = std::max(1, screen.width() - extent);
  const qreal spanY = std::max(1, screen.height() - extent);

  m_flying = true;
  m_dragging = false;
  // Convert pixels per second into the place fractions the window moves in.
  m_flightVelocity = QPointF(dx / spanX, dy / spanY);
  if (m_mascot)
    m_mascot->beginDash(std::atan2(-dy, dx),
                        qBound(0.0, speed / (kThrowSpeed * 4.0), 1.0));
}

void Backend::advance(qreal dt) {
  if (!m_flying || !m_window)
    return;

  const QRect screen = screenGeometry();
  const int extent = m_window->width();
  const qreal spanX = std::max(1.0, qreal(screen.width() - extent));
  const qreal spanY = std::max(1.0, qreal(screen.height() - extent));

  QPointF place = m_place + m_flightVelocity * dt;

  // Bounce off the edges of the screen rather than sticking to them.
  if (place.x() < 0.0 || place.x() > 1.0) {
    place.setX(qBound(0.0, place.x() < 0.0 ? -place.x() : 2.0 - place.x(), 1.0));
    m_flightVelocity.setX(-m_flightVelocity.x() * kBounce);
  }
  if (place.y() < 0.0 || place.y() > 1.0) {
    place.setY(qBound(0.0, place.y() < 0.0 ? -place.y() : 2.0 - place.y(), 1.0));
    m_flightVelocity.setY(-m_flightVelocity.y() * kBounce);
  }
  m_place = place;

  // Air resistance.
  m_flightVelocity *= std::max(0.0, 1.0 - kDrag * dt);

  const qreal pixelsPerSecond =
      std::hypot(m_flightVelocity.x() * spanX, m_flightVelocity.y() * spanY);
  if (m_mascot)
    m_mascot->updateDash(
        std::atan2(-m_flightVelocity.y() * spanY, m_flightVelocity.x() * spanX),
        qBound(0.0, pixelsPerSecond / (kThrowSpeed * 4.0), 1.0));

  applyPlacement();
  emit changed();

  if (pixelsPerSecond < kThrowSpeed * 0.35) {
    m_flying = false;
    m_flightVelocity = QPointF();
    if (m_mascot)
      m_mascot->endDash();
    m_saveTimer.start();
  }
}

void Backend::injectCursor(int x, int y) {
  if (m_cursor)
    m_cursor->inject(QPoint(x, y));
}

// --- commands --------------------------------------------------------------

void Backend::openSettings() { emit settingsRequested(); }

void Backend::quit() { QCoreApplication::quit(); }

void Backend::command(const QString &name) {
  if (!m_mascot)
    return;
  if (name == "poke")
    m_mascot->poke();
  else if (name == "think")
    m_mascot->think();
  else if (name == "alert")
    m_mascot->alert();
  else if (name == "notify")
    m_mascot->notify();
  else if (name == "wake")
    m_mascot->wake();
  else if (name == "rest")
    m_mascot->rest();
  else if (name == "wink")
    m_mascot->wink();
  else if (name == "scatter")
    m_mascot->scatter();
  else if (name == "dash") {
    // Off in some direction of her own choosing.
    const qreal angle = QRandomGenerator::global()->generateDouble() * 2 * M_PI;
    launch(std::cos(angle) * kThrowSpeed * 2.2,
           std::sin(angle) * kThrowSpeed * 2.2, kThrowSpeed * 2.2);
  }
  else if (name == "settings")
    openSettings();
  else if (name == "reset")
    resetPlace();
  else if (name == "quit")
    quit();
}

QString Backend::status() const {
  static const char *moods[] = {"resting",   "happy",  "thinking", "alert",
                                "notifying", "asleep", "held",     "dashing"};
  const int mood = m_mascot ? m_mascot->mood() : 0;
  const int moodCount = int(std::size(moods));
  return QStringLiteral("Nala is running — size %1%, %2, at %3%/%4%, %5, %6 "
                        "(form %7→%8 %9%)")
      .arg(int(std::lround(m_size * 100)))
      .arg(m_colorMode == "theme" ? "following the desktop theme" : "ink")
      .arg(int(std::lround(m_place.x() * 100)))
      .arg(int(std::lround(m_place.y() * 100)))
      .arg(mood >= 0 && mood < moodCount ? moods[mood] : "?")
      .arg(!m_followCursor        ? "gaze free"
           : m_cursor && m_cursor->available()
               ? "tracking the cursor"
               : "cursor unavailable")
      .arg(m_mascot ? m_mascot->formA() : -1)
      .arg(m_mascot ? m_mascot->formB() : -1)
      .arg(m_mascot ? int(std::lround(m_mascot->formMix() * 100)) : 0);
}

// --- autostart -------------------------------------------------------------

bool Backend::startAtLogin() const { return QFile::exists(autostartPath()); }

void Backend::setStartAtLogin(bool enabled) {
  const QString path = autostartPath();
  if (!enabled) {
    if (QFile::exists(path) && !QFile::remove(path))
      note(QStringLiteral("Could not remove the autostart entry."));
    emit changed();
    return;
  }

  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    note(QStringLiteral("Could not create the autostart entry."));
    emit changed();
    return;
  }
  const QString entry = QStringLiteral("[Desktop Entry]\n"
                                       "Type=Application\n"
                                       "Name=Nala\n"
                                       "Comment=Desktop companion\n"
                                       "Exec=%1\n"
                                       "Terminal=false\n"
                                       "X-GNOME-Autostart-enabled=true\n")
                            .arg(QCoreApplication::applicationFilePath());
  file.write(entry.toUtf8());
  if (!file.commit())
    note(QStringLiteral("Could not create the autostart entry."));
  emit changed();
}
