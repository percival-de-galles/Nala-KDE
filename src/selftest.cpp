#include "selftest.h"
#include "backend.h"
#include "mascot.h"
#include "orbits.h"
#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QQuickWindow>
#include <QRect>
#include <algorithm>
#include <utility>
#include <QTest>
#include <QTextStream>

namespace {

// Smallest rectangle covering pixels that pass `accept`.
QRect boundsOf(const QImage &image, bool (*accept)(const QColor &)) {
  QRect bounds;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      if (accept(image.pixelColor(x, y)))
        bounds |= QRect(x, y, 1, 1);
  return bounds;
}

bool isOpaque(const QColor &c) { return c.alpha() > 120; }
bool isEye(const QColor &c) {
  return c.alpha() > 120 && c.red() > 200 && c.green() > 200 && c.blue() > 200;
}

// Bounds of just the left or right eye, split at the midpoint of the pair.
QRect eyeBounds(const QImage &image, const QRect &pair, bool left) {
  const int mid = pair.center().x();
  QRect bounds;
  for (int y = pair.top(); y <= pair.bottom(); ++y)
    for (int x = pair.left(); x <= pair.right(); ++x) {
      if (left ? x > mid : x <= mid)
        continue;
      if (isEye(image.pixelColor(x, y)))
        bounds |= QRect(x, y, 1, 1);
    }
  return bounds;
}

} // namespace

int capturePoses(QApplication &app, Mascot &mascot, Orbits &orbits,
                 QQuickWindow *window, const QString &directory) {
  QDir().mkpath(directory);
  QTest::qWait(500);

  const auto shoot = [&](const QString &name) {
    // Never catch her mid-blink: a captured pose should be comparable.
    for (int i = 0; i < 60 && mascot.eyeHeight() < 0.2; ++i)
      mascot.tick(1.0 / 60.0);
    orbits.setIntensity(mascot.rings()); // never inherit the previous pose
    window->requestUpdate();
    QTest::qWait(90);
    const QImage image = window->grabWindow();
    const bool ok = image.save(directory + "/" + name + ".png");
    QTextStream(stdout) << (ok ? "saved " : "FAILED ") << name << "\n";
  };

  mascot.setReducedMotion(true);
  struct Pose {
    const char *name;
    int form;
  };
  for (const Pose &pose : {Pose{"circle", Mascot::Circle},
                           Pose{"egg", Mascot::Egg},
                           Pose{"hex", Mascot::Hex},
                           Pose{"triangle", Mascot::Triangle},
                           Pose{"exclaim", Mascot::Exclaim},
                           Pose{"teardrop", Mascot::Teardrop},
                           Pose{"dots", Mascot::Dots},
                           Pose{"tiny", Mascot::Tiny}}) {
    mascot.rest();
    mascot.snapForm(pose.form);
    for (int i = 0; i < 40; ++i)
      mascot.tick(0.04); // let the dots spread and the springs settle
    shoot(pose.name);
  }

  // Transition sequences: five evenly-spaced frames through a morph, so the
  // in-between shapes can be checked against the reference's own frames.
  mascot.setReducedMotion(false);
  for (const auto &t : {std::pair<const char *, int>{"seq-dots", Mascot::Dots},
                        {"seq-exclaim", Mascot::Exclaim},
                        {"seq-triangle", Mascot::Triangle}}) {
    mascot.rest();
    mascot.changeForm(t.second);
    // The morph runs 0.22 s; sample it at 0, 25, 50, 75 and 100 per cent.
    const qreal step = 0.22 / 4.0;
    for (int i = 0; i < 5; ++i) {
      shoot(QStringLiteral("%1-%2").arg(t.first).arg(i));
      for (int f = 0; f < 4; ++f)
        mascot.tick(step / 4.0);
    }
  }
  mascot.setReducedMotion(true);

  // Gaze poses, for checking the eyes against the reference.
  for (const auto &g : {std::pair<const char *, std::pair<qreal, qreal>>
                            {"gaze-ahead", {0.0, 0.0}},
                        {"gaze-left", {-6.0, 0.0}},
                        {"gaze-right", {6.0, 0.0}},
                        {"gaze-up", {0.0, -6.0}},
                        {"gaze-down", {0.0, 6.0}},
                        {"gaze-upright", {4.0, -4.0}},
                        {"gaze-rest", {0.0, 0.0}}}) {
    mascot.rest();
    if (QString::fromLatin1(g.first) == "gaze-rest")
      mascot.lookIdle();
    else
      mascot.lookAt(g.second.first, g.second.second);
    for (int i = 0; i < 60; ++i)
      mascot.tick(0.04);
    shoot(g.first);
  }

  // Expression states on the idle body.
  mascot.rest();
  mascot.notify();
  for (int i = 0; i < 40; ++i)
    mascot.tick(0.04);
  shoot("notify");

  mascot.rest();
  mascot.setReducedMotion(false);
  mascot.think(6.0);
  for (int i = 0; i < 60; ++i) {
    mascot.tick(0.04);
    orbits.setIntensity(mascot.rings());
    orbits.advance(0.04);
  }
  shoot("thinking");

  // Mid-blink, caught deliberately rather than by luck: run until the eyes
  // are at their most closed, then grab that frame.
  mascot.rest();
  mascot.setReducedMotion(true);
  for (int i = 0; i < 1200; ++i) {
    mascot.tick(1.0 / 60.0);
    if (mascot.eyeHeight() < 0.05)
      break;
  }
  shoot("blink");

  app.exit(0);
  return 0;
}

int runSelfTest(QApplication &app, Backend &backend, Mascot &mascot,
                Orbits &orbits, Theme &theme, QQuickWindow *window,
                const QStringList &warnings, const QString &captureDir) {
  int failures = 0;
  QTextStream out(stdout);

  const auto check = [&](bool condition, const char *name) {
    out << (condition ? "PASS " : "FAIL ") << name << "\n";
    if (!condition)
      ++failures;
  };

  if (!captureDir.isEmpty())
    QDir().mkpath(captureDir);

  const auto capture = [&](const QString &name) {
    // The scene only rebuilds when the model has published a frame, so make
    // sure one has been emitted and presented before grabbing.
    window->requestUpdate();
    QTest::qWait(60);
    const QImage image = window->grabWindow();
    if (!captureDir.isEmpty())
      image.save(captureDir + "/" + name + ".png");
    return image;
  };

  // Let the window map and the first frames land before measuring anything.
  QTest::qWait(400);
  check(window->isVisible(), "the companion window is up");

  // --- preferences ---------------------------------------------------------
  check(qFuzzyCompare(backend.size(), 1.0), "default size is 100%");
  backend.configure("size", 1.4);
  check(qFuzzyCompare(backend.size(), 1.4), "size applies");
  backend.configure("size", 9.0);
  check(qFuzzyCompare(backend.size(), 1.4), "out-of-range size is rejected");
  backend.configure("size", 1.0);

  check(backend.colorMode() == "ink", "default colour is ink");
  check(backend.mascotColor() == QColor("#0a090c"),
        "ink mode uses the reference silhouette colour");
  backend.configure("colorMode", "theme");
  check(backend.mascotColor() != QColor("#0a090c"),
        "wallpaper mode takes its colour from the theme");
  backend.configure("colorMode", "ink");

  // --- Noctalia theme ------------------------------------------------------
  check(theme.available(), "Noctalia palette parsed");
  check(theme.colors().value("surface").value<QColor>() == QColor("#24273a"),
        "surface colour comes from noctalia.css");
  check(theme.colors().value("accent").value<QColor>() == QColor("#b7bdf8"),
        "accent colour comes from noctalia.css");
  check(theme.fontFamily() == "Google Sans Flex", "shell font is honoured");
  check(qAbs(theme.radius() - 0.75) < 0.001, "corner radius scale is honoured");
  check(qAbs(theme.motionScale() - 0.6) < 0.001, "animation speed is honoured");

  // --- placement and drag --------------------------------------------------
  backend.resetPlace();
  QTest::qWait(40);
  check(qAbs(backend.nx() - 0.86) < 0.001 && qAbs(backend.ny() - 0.74) < 0.001,
        "reset restores the default corner");

  const qreal startX = backend.nx();
  backend.grabDrag();
  check(backend.dragging() && mascot.mood() == Mascot::Held,
        "pressing her starts a drag");
  backend.dragBy(-400.0, 30.0);
  check(backend.nx() < startX - 0.02, "dragging left moves Nala left");
  check(backend.ny() > 0.0, "dragging down moves Nala down");
  backend.releaseDrag();
  check(!backend.dragging() && mascot.mood() != Mascot::Held,
        "releasing ends the drag");

  // She must never be pushed off the edge of the screen.
  backend.grabDrag();
  backend.dragBy(-99999.0, -99999.0);
  backend.releaseDrag();
  check(backend.nx() >= 0.0 && backend.ny() >= 0.0,
        "she cannot be dragged off the screen");
  backend.resetPlace();

  // --- gaze ----------------------------------------------------------------
  mascot.setReducedMotion(true);
  mascot.lookAt(3.0, 0.0);
  for (int i = 0; i < 12; ++i)
    mascot.tick(0.05);
  const qreal lookRight = (mascot.eyeLeftX() + mascot.eyeRightX()) * 0.5;
  mascot.lookAt(-3.0, 0.0);
  for (int i = 0; i < 12; ++i)
    mascot.tick(0.05);
  const qreal lookLeft = (mascot.eyeLeftX() + mascot.eyeRightX()) * 0.5;
  check(lookRight > lookLeft, "eyes track the cursor horizontally");

  mascot.lookAt(0.0, 3.0);
  for (int i = 0; i < 12; ++i)
    mascot.tick(0.05);
  const qreal lookDown = (mascot.eyeLeftY() + mascot.eyeRightY()) * 0.5;
  mascot.lookAt(0.0, -3.0);
  for (int i = 0; i < 12; ++i)
    mascot.tick(0.05);
  check(lookDown > (mascot.eyeLeftY() + mascot.eyeRightY()) * 0.5,
        "eyes track the cursor vertically");
  // The compositor -> backend -> mascot path has arithmetic of its own (the
  // offset is converted into units of her radius), so exercise it end to end
  // rather than only calling lookAt() directly.
  const QPointF centre = backend.centreOnScreen();
  backend.injectCursor(int(centre.x()) + 600, int(centre.y()));
  for (int i = 0; i < 12; ++i)
    mascot.tick(0.05);
  const qreal trackedRight = (mascot.eyeLeftX() + mascot.eyeRightX()) * 0.5;
  backend.injectCursor(int(centre.x()) - 600, int(centre.y()));
  for (int i = 0; i < 12; ++i)
    mascot.tick(0.05);
  check(trackedRight > (mascot.eyeLeftX() + mascot.eyeRightX()) * 0.5,
        "a cursor reported by the compositor moves her gaze");

  // A cursor sitting exactly on her should leave the gaze pointing straight
  // out; if the centre were computed from the wrong origin this would skew.
  backend.injectCursor(int(centre.x()), int(centre.y()));
  for (int i = 0; i < 20; ++i)
    mascot.tick(0.05);
  const qreal centredX = (mascot.eyeLeftX() + mascot.eyeRightX()) * 0.5;
  const qreal centredY = (mascot.eyeLeftY() + mascot.eyeRightY()) * 0.5;
  check(qAbs(centredX) < 0.06 && qAbs(centredY) < 0.06,
        "a cursor on top of her leaves the gaze centred");

  backend.injectCursor(int(centre.x()), int(centre.y()) + 600);
  for (int i = 0; i < 20; ++i)
    mascot.tick(0.05);
  check((mascot.eyeLeftY() + mascot.eyeRightY()) * 0.5 > centredY + 0.05,
        "a cursor below her pulls the gaze down");

  // The eyes are carried on a sphere, so the reference shows three things a
  // flat translation cannot produce. All three were measured off the video.
  {
    const auto settleGaze = [&] {
      for (int i = 0; i < 40; ++i)
        mascot.tick(0.05);
    };

    mascot.lookAt(0.0, 0.0);
    settleGaze();
    const qreal gapAhead = mascot.eyeRightX() - mascot.eyeLeftX();

    mascot.lookAt(6.0, 0.0);
    settleGaze();
    const qreal gapAside = mascot.eyeRightX() - mascot.eyeLeftX();
    const qreal reach = (mascot.eyeLeftX() + mascot.eyeRightX()) * 0.5;

    check(gapAside < gapAhead * 0.85,
          "the eyes draw together as the gaze swings aside");
    // The old flat-eye model topped out at 0.34 R, which read as barely
    // moving. The fitted sphere carries the pair to about 0.48 R.
    check(reach > 0.42,
          "a glance carries the eyes as far as the reference does");
    check(mascot.eyeRightScaleX() < 0.9,
          "an eye foreshortens as it approaches the edge");

    mascot.lookAt(4.0, 4.0);
    settleGaze();
    check(qAbs(mascot.eyeRightY() - mascot.eyeLeftY()) > 0.02,
          "the pair tilts when the gaze is both aside and down");

    mascot.lookAt(0.0, 0.0);
    settleGaze();
    check(qAbs(mascot.eyeRightY() - mascot.eyeLeftY()) < 0.02,
          "the pair is level when looking straight ahead");
  }

  mascot.lookIdle();
  mascot.setReducedMotion(false);

  // --- morphing ------------------------------------------------------------
  // Transitions in the reference run 0.20 s on average and never exceed
  // 0.33 s, so nothing here should be slower than that.
  {
    mascot.rest();
    const qreal step = 1.0 / 60.0;
    mascot.changeForm(Mascot::Triangle);
    int frames = 0;
    while (mascot.formMix() < 1.0 && frames < 120) {
      mascot.tick(step);
      ++frames;
    }
    const qreal seconds = frames * step;
    out << "   default morph " << int(seconds * 1000)
        << " ms (reference 200, never over 333)\n";
    check(seconds <= 0.34, "a morph is no slower than the reference's");
    check(seconds >= 0.12, "a morph is not instant");
  }

  mascot.rest();
  mascot.changeForm(Mascot::Hex, 0.4);
  mascot.tick(0.06);
  check(mascot.formMix() > 0.0 && mascot.formMix() < 1.0,
        "a morph blends over time rather than snapping");
  check(mascot.formA() == Mascot::Circle && mascot.formB() == Mascot::Hex,
        "a morph interpolates between the two forms");
  for (int i = 0; i < 20; ++i)
    mascot.tick(0.04);
  check(mascot.formMix() >= 1.0 && mascot.formA() == Mascot::Hex,
        "a morph settles on its target");

  // Interrupting mid-morph must not tear the silhouette.
  mascot.changeForm(Mascot::Triangle, 0.4);
  mascot.tick(0.05);
  mascot.changeForm(Mascot::Circle, 0.4);
  check(mascot.formB() == Mascot::Triangle,
        "an interrupted morph lands before the next one starts");
  for (int i = 0; i < 30; ++i)
    mascot.tick(0.04);
  check(mascot.formB() == Mascot::Circle, "the queued morph then runs");

  // --- blink ---------------------------------------------------------------
  //
  // Timings measured over the 34 blinks in the reference: a 243 ms cycle that
  // shuts completely, spending longer closing than opening.
  {
    mascot.rest();
    mascot.lookIdle();
    const qreal open = mascot.eyeHeight();
    const qreal step = 1.0 / 60.0;

    int closing = 0, opening = 0, shut = 0;
    bool seen = false, past = false;
    qreal minimum = open;
    for (int i = 0; i < 1200 && !past; ++i) { // up to 20 s
      mascot.tick(step);
      const qreal h = mascot.eyeHeight();
      minimum = std::min(minimum, h);
      if (h < open * 0.92) {
        seen = true;
        if (h < open * 0.06)
          ++shut;
        else if (shut == 0)
          ++closing;
        else
          ++opening;
      } else if (seen) {
        past = true;
      }
    }

    check(seen, "she blinks");
    check(minimum < open * 0.05, "a blink shuts her eyes completely");

    const qreal cycle = (closing + shut + opening) * step;
    out << "   blink cycle " << int(cycle * 1000) << " ms (reference 243), "
        << "closing " << int(closing * step * 1000) << " ms vs opening "
        << int(opening * step * 1000) << " ms (reference 121 vs 84)\n";
    check(cycle > 0.18 && cycle < 0.32,
          "a blink takes about as long as the reference's");
    check(closing > opening,
          "her lids close more slowly than they open, as the reference's do");
  }

  // --- reactions -----------------------------------------------------------
  mascot.poke();
  mascot.tick(0.05);
  check(mascot.mood() == Mascot::Happy, "a click makes her happy");
  check(mascot.squashX() > 1.0 && mascot.squashY() < 1.0,
        "a click squashes her on impact");

  mascot.think(4.0);
  for (int i = 0; i < 30; ++i)
    mascot.tick(0.04);
  check(mascot.rings() > 0.4, "thinking raises the orbit rings");
  orbits.setIntensity(mascot.rings());
  const qreal phaseBefore = orbits.rings().first().phase;
  orbits.advance(0.25);
  check(!qFuzzyCompare(orbits.rings().first().phase, phaseBefore),
        "orbit rings advance while she thinks");

  for (int i = 0; i < 30; ++i) // let the thinking morph land
    mascot.tick(0.04);
  mascot.alert();
  mascot.tick(0.05);
  check(mascot.formB() == Mascot::Exclaim, "an alert becomes an exclamation");
  check(mascot.mood() == Mascot::Alert, "an alert changes her mood");
  for (int i = 0; i < 40; ++i)
    mascot.tick(0.04);

  mascot.notify();
  for (int i = 0; i < 20; ++i)
    mascot.tick(0.04);
  check(mascot.badge() > 0.5, "a notification shows the badge");
  check(mascot.eyeWidth() > 0.19, "a notification widens her eyes");

  // --- sleep ---------------------------------------------------------------
  mascot.setSleepWhenIdle(true);
  mascot.lookIdle();
  for (int i = 0; i < 900; ++i) // 90 s of being left alone, at the dt cap
    mascot.tick(0.1);
  check(mascot.sleeping(), "she falls asleep when left alone");
  mascot.wake();
  check(!mascot.sleeping(), "she wakes on interaction");
  for (int i = 0; i < 30; ++i)
    mascot.tick(0.04);

  // --- rendering -----------------------------------------------------------
  mascot.setReducedMotion(true);
  mascot.rest();
  orbits.setIntensity(0.0);
  for (int i = 0; i < 25; ++i)
    mascot.tick(0.04);

  // The body is a GPU shader. The `offscreen` QPA plugin has no swapchain, so
  // the shader never compiles there and every pixel assertion below would fail
  // for a reason that has nothing to do with Nala. Detect that and skip rather
  // than cry wolf -- `ctest` covers behaviour, a display covers appearance.
  auto *shader = window->findChild<QObject *>("bodyShader");
  check(shader != nullptr, "the body shader exists");
  const bool rendered =
      shader && shader->property("status").toInt() == 0; // 0 == Compiled
  if (!rendered) {
    out << "SKIP appearance checks — no GPU surface (run with a display "
           "attached to verify them)\n";
    if (shader && !shader->property("log").toString().isEmpty())
      out << "   shader log: " << shader->property("log").toString() << "\n";
  }

  const QImage idle = capture("idle");
  if (rendered)
    check(!idle.isNull() && idle.width() > 8, "the window grab is usable");
  if (rendered) {
    const QRect bodyBox = boundsOf(idle, isOpaque);
    const QRect eyeBox = boundsOf(idle, isEye);

    check(qAlpha(idle.pixel(2, 2)) < 20, "the corners stay transparent");
    check(bodyBox.width() > idle.width() / 3 &&
              bodyBox.height() > idle.height() / 3,
          "the body renders");
    check(qAbs(bodyBox.width() - bodyBox.height()) <= 3,
          "the idle body is round");

    // Proportions measured from the reference animation. The drawing stage is
    // the largest square inside the window, so compare against that -- a
    // window manager may well have given us a non-square window.
    const int canvas = std::min(idle.width(), idle.height());
    const qreal bodyRatio = qreal(bodyBox.width()) / canvas;
    out << "   body/canvas " << bodyRatio << " (reference "
        << 1.0 / 1.89 << ")\n";
    check(qAbs(bodyRatio - 1.0 / 1.89) < 0.06,
          "the body matches the reference proportion");

    check(eyeBox.width() > 4 && eyeBox.height() > 4, "the eyes render");
    check(bodyBox.contains(eyeBox), "the eyes stay inside the body");
    check(eyeBox.center().y() < bodyBox.center().y(),
          "the eyes sit above the body's centre");

    const QRect leftEye = eyeBounds(idle, eyeBox, true);
    const QRect rightEye = eyeBounds(idle, eyeBox, false);
    check(leftEye.isValid() && rightEye.isValid(), "there are two eyes");
    if (leftEye.isValid() && rightEye.isValid()) {
      check(leftEye.height() > leftEye.width(),
            "a single idle eye is taller than it is wide");
      // The reference slit is about 1.7 times as tall as it is wide.
      const qreal aspect = qreal(leftEye.height()) / leftEye.width();
      check(aspect > 1.35 && aspect < 2.15,
            "the eye slit matches the reference aspect");
      const qreal radius = bodyBox.width() / 2.0;
      const qreal gap = (rightEye.center().x() - leftEye.center().x()) / radius;
      out << "   eye gap " << gap << "R (reference 0.47), slit aspect "
          << qreal(leftEye.height()) / leftEye.width()
          << " (reference 1.71)\n";
      check(qAbs(gap - 0.47) < 0.12,
            "the eyes sit the reference distance apart");
    }
  }

  // The morph must never blink out of existence mid-transition.
  mascot.setReducedMotion(false);
  for (int form : {int(Mascot::Dots), int(Mascot::Exclaim),
                   int(Mascot::Triangle), int(Mascot::Circle)}) {
    mascot.changeForm(form, 0.35);
    for (int step = 0; step < 3; ++step) {
      for (int i = 0; i < 3; ++i)
        mascot.tick(0.04);
      const QImage frame = capture(QStringLiteral("morph-%1-%2")
                                       .arg(form)
                                       .arg(step));
      if (!rendered)
        continue;
      const QRect box = boundsOf(frame, isOpaque);
      check(box.width() > 6 && box.height() > 6,
            "the silhouette survives every morph frame");
    }
    for (int i = 0; i < 20; ++i)
      mascot.tick(0.04);
  }

  // --- preferences window --------------------------------------------------
  backend.openSettings();
  QTest::qWait(400);
  auto *settings = window->findChild<QQuickWindow *>("settingsWindow");
  check(settings != nullptr, "the preferences window exists");
  if (settings && !captureDir.isEmpty())
    settings->grabWindow().save(captureDir + "/preferences.png");

  // --- autostart -----------------------------------------------------------
  const bool hadAutostart = backend.startAtLogin();
  backend.setStartAtLogin(true);
  check(backend.startAtLogin(), "autostart can be enabled");
  backend.setStartAtLogin(false);
  check(!backend.startAtLogin(), "autostart can be disabled");
  if (hadAutostart)
    backend.setStartAtLogin(true);

  check(warnings.isEmpty(), "no QML warnings");
  for (const QString &warning : warnings)
    QTextStream(stderr) << warning << "\n";

  out << "RESULT " << failures << " failures\n";
  app.exit(failures ? 1 : 0);
  return failures ? 1 : 0;
}
