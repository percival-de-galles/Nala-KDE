#include "mascot.h"
#include <QScopeGuard>
#include <QtMath>
#include <algorithm>

namespace {

// Rest pose, measured from the reference: the eye pair sits slightly above and
// right of centre, separated by just under half the body radius.
constexpr qreal kEyeRestX = 0.19;
constexpr qreal kEyeRestY = -0.13;
constexpr qreal kEyeGap = 0.47;
constexpr qreal kEyeWidth = 0.145;
constexpr qreal kEyeHeight = 0.25;

// A blink is two frames of closing and two of opening at 60 fps.
constexpr qreal kBlinkSeconds = 0.155;

constexpr qreal kSleepAfter = 75.0;

qreal easeInOut(qreal t) {
  t = qBound(0.0, t, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

} // namespace

Mascot::Mascot(QObject *parent)
    : QObject(parent), m_random(QRandomGenerator::securelySeeded()) {
  scheduleBlink();
}

qreal Mascot::random(qreal lo, qreal hi) {
  return lo + m_random.generateDouble() * (hi - lo);
}

void Mascot::settle(qreal &value, qreal target, qreal dt, qreal rate) const {
  // Frame-rate independent exponential approach. `rate` is roughly "how many
  // e-folds per second", so higher is snappier.
  if (m_reduced)
    rate *= 2.4;
  const qreal k = 1.0 - std::exp(-rate * dt);
  value += (target - value) * k;
}

void Mascot::setMood(Mood mood) {
  if (m_mood == mood)
    return;
  m_mood = mood;
  emit moodChanged();
}

void Mascot::morphTo(int form, qreal seconds) {
  if (m_reduced)
    seconds = std::min(seconds, 0.12);

  if (m_formMix < 1.0) {
    // A morph is already in flight. Let it land rather than snapping, and
    // remember where we actually wanted to go.
    if (form != m_formB) {
      m_queuedForm = form;
      m_queuedSeconds = seconds;
    }
    return;
  }
  if (form == m_formB) {
    m_queuedForm = -1;
    return;
  }

  m_formA = m_formB;
  m_formB = form;
  m_formMix = 0.0;
  m_morphRate = 1.0 / std::max(seconds, 0.01);
  m_queuedForm = -1;
}

void Mascot::changeForm(int form, qreal seconds) { morphTo(form, seconds); }

void Mascot::snapForm(int form) {
  m_queuedForm = -1;
  m_formA = m_formB = form;
  m_formMix = 1.0;
  m_morphRate = 0.0;
}

void Mascot::advanceMorph(qreal dt) {
  if (m_formMix < 1.0) {
    m_formMix = std::min(1.0, m_formMix + m_morphRate * dt);
    if (m_formMix >= 1.0) {
      m_formA = m_formB;
      if (m_queuedForm >= 0) {
        const int form = m_queuedForm;
        const qreal seconds = m_queuedSeconds;
        m_queuedForm = -1;
        morphTo(form, seconds);
      }
    }
  }

  // The "..." run is a parameter of the dots form rather than a separate
  // shape, so the collapse reads as one continuous motion.
  const bool wantDots = m_formB == Dots;
  m_dotsSpreadTarget = wantDots ? 1.0 : 0.0;
  m_dotsShrinkTarget = wantDots ? 1.0 : 0.0;
  settle(m_dotsSpread, m_dotsSpreadTarget, dt, 9.0);
  settle(m_dotsShrink, m_dotsShrinkTarget, dt, 7.5);
  // Roughly one pulse per dot per second, as in the reference.
  if (m_dotsSpread > 0.01)
    m_dotsPhase += dt * 4.4;
}

void Mascot::scheduleBlink() { m_nextBlink = random(2.2, 5.4); }

void Mascot::advanceBlink(qreal dt) {
  if (m_mood == Asleep)
    return;

  if (m_blinkPhase >= 0.0) {
    m_blinkPhase += dt / kBlinkSeconds;
    if (m_blinkPhase >= 1.0) {
      m_blinkPhase = -1.0;
      scheduleBlink();
    }
  } else {
    m_nextBlink -= dt;
    if (m_nextBlink <= 0.0)
      m_blinkPhase = 0.0;
  }
}

void Mascot::advanceIdle(qreal dt) {
  if (!m_idleAntics || m_mood != Resting || m_reduced)
    return;

  m_antic -= dt;
  if (m_antic > 0.0)
    return;
  m_antic = random(9.0, 18.0);

  // Amusing herself is not the same as being interacted with: the gestures
  // below run through the same entry points as a real interaction, so preserve
  // the idle clock across them or she would never settle down to sleep.
  const qreal idleBefore = m_idle;
  const auto keepIdleClock = qScopeGuard([this, idleBefore] {
    m_idle = idleBefore;
  });

  // A small repertoire of things to do when left alone, weighted towards the
  // quiet ones so she never feels busy.
  const int pick = int(random(0.0, 10.0));
  if (pick < 3) {
    m_eyeTarget = QPointF(random(-0.30, 0.42), random(-0.42, 0.18));
    m_lookingAtCursor = false;
  } else if (pick < 5) {
    m_squashXTarget = 1.07;
    m_squashYTarget = 0.93;
    m_hold = 0.22;
  } else if (pick < 7) {
    morphTo(Egg, 0.45);
    m_hold = 1.4;
  } else if (pick < 9) {
    morphTo(Hex, 0.45);
    m_hold = 1.4;
  } else {
    think(2.4);
  }
}

void Mascot::tick(qreal dt) {
  dt = qBound(0.0, dt, 0.1); // survive a stalled frame without a lurch
  m_time += dt;
  m_idle += dt;
  if (m_cooldown > 0.0)
    m_cooldown = std::max(0.0, m_cooldown - dt);

  advanceMorph(dt);
  advanceBlink(dt);
  advanceIdle(dt);

  // Mood timers.
  if (m_hold > 0.0) {
    m_hold -= dt;
    if (m_hold <= 0.0) {
      m_hold = 0.0;
      if (m_mood != Held && m_mood != Asleep) {
        setMood(Resting);
        morphTo(Circle, 0.38);
        m_ringsTarget = 0.0;
        m_badgeTarget = 0.0;
        m_squashXTarget = m_squashYTarget = 1.0;
        m_eyeWidthTarget = kEyeWidth;
        m_eyeHeightTarget = kEyeHeight;
        m_eyeRoundTarget = 1.0;
        m_rollTarget = 0.0;
        m_scaleTarget = 1.0;
      }
    }
  }

  // Drift off after a long stretch of being left alone.
  if (m_sleepWhenIdle && m_mood == Resting && m_idle > kSleepAfter) {
    setMood(Asleep);
    morphTo(Tiny, 0.6);
    m_scaleTarget = 1.0;
    m_eyeHeightTarget = 0.0;
  }

  // Springs and easings.
  settle(m_squashX, m_squashXTarget, dt, 11.0);
  settle(m_squashY, m_squashYTarget, dt, 11.0);
  settle(m_roll, m_rollTarget, dt, 8.0);
  settle(m_badge, m_badgeTarget, dt, 12.0);
  settle(m_rings, m_ringsTarget, dt, 4.5);

  // Body scale uses a real spring so pops overshoot the way the reference does.
  {
    const qreal stiffness = m_reduced ? 420.0 : 210.0;
    const qreal damping = m_reduced ? 42.0 : 19.0;
    m_scaleVelocity += ((m_scaleTarget - m_scale) * stiffness -
                        m_scaleVelocity * damping) *
                       dt;
    m_scale += m_scaleVelocity * dt;
  }

  // Eyes.
  if (!m_lookingAtCursor && m_mood == Resting && m_idle < kSleepAfter) {
    // Tiny wander so the gaze never looks frozen.
    const qreal wander = m_reduced ? 0.0 : 0.02;
    m_eyeTarget.setX(m_eyeTarget.x() + std::sin(m_time * 0.37) * wander * dt);
  }
  qreal ex = m_eyeCentre.x(), ey = m_eyeCentre.y();
  settle(ex, m_eyeTarget.x(), dt, 7.0);
  settle(ey, m_eyeTarget.y(), dt, 7.0);
  m_eyeCentre = QPointF(ex, ey);

  settle(m_eyeWidth, m_eyeWidthTarget, dt, 13.0);
  settle(m_eyeRound, m_eyeRoundTarget, dt, 10.0);

  qreal height = m_eyeHeightTarget;
  if (m_blinkPhase >= 0.0) {
    // Close fast, open slightly slower -- a symmetric curve reads mechanical.
    const qreal t = m_blinkPhase;
    const qreal closed = t < 0.42 ? t / 0.42 : 1.0 - (t - 0.42) / 0.58;
    height = m_eyeHeightTarget * (1.0 - 0.94 * easeInOut(closed));
  }
  settle(m_eyeHeight, height, dt, m_blinkPhase >= 0.0 ? 42.0 : 13.0);

  // Idle float and the decaying shake that follows an alert.
  if (m_reduced) {
    m_bobX = m_bobY = 0.0;
  } else {
    const qreal breathe = m_mood == Asleep ? 0.0 : 1.0;
    m_bobY = std::sin(m_time * 1.45) * 0.014 * breathe;
    m_bobX = std::sin(m_time * 0.93 + 1.1) * 0.006 * breathe;
  }
  if (m_wobble > 0.001) {
    m_wobble = std::max(0.0, m_wobble - dt * 1.6);
    const qreal shake = std::sin(m_time * 38.0) * m_wobble;
    m_bobX += shake * 0.05;
    m_roll += shake * 0.16;
  }

  // Slow tumble while she is thinking.
  if (m_rings > 0.02 && !m_reduced)
    m_roll += dt * 0.55 * m_rings;

  emit frame();
}

void Mascot::wake() {
  m_idle = 0.0;
  if (m_mood == Asleep) {
    setMood(Resting);
    morphTo(Circle, 0.42);
    m_scale = 0.75;
    m_scaleVelocity = 0.0;
    m_scaleTarget = 1.0;
    m_eyeHeightTarget = kEyeHeight;
    m_hold = 0.0;
  }
}

void Mascot::poke() {
  wake();
  if (m_cooldown > 0.0)
    return;
  m_cooldown = 0.28;

  setMood(Happy);
  // Squash on impact, then bounce past the rest size on the way back.
  m_squashXTarget = 1.16;
  m_squashYTarget = 0.86;
  m_scale = 0.9;
  m_scaleVelocity = 1.6;
  m_scaleTarget = 1.0;
  // Eyes go round and wide -- the "delighted" pose from the reference.
  m_eyeWidthTarget = 0.20;
  m_eyeHeightTarget = 0.225;
  m_eyeRoundTarget = 1.0;
  morphTo(Triangle, 0.30);
  m_hold = 0.85;

  QMetaObject::invokeMethod(
      this,
      [this] {
        m_squashXTarget = 1.0;
        m_squashYTarget = 1.0;
      },
      Qt::QueuedConnection);
}

void Mascot::think(qreal seconds) {
  wake();
  setMood(Thinking);
  m_ringsTarget = 1.0;
  morphTo(Triangle, 0.42);
  m_eyeWidthTarget = kEyeWidth;
  m_eyeHeightTarget = kEyeHeight;
  m_hold = std::max(seconds, 0.6);
}

void Mascot::alert() {
  wake();
  setMood(Alert);
  morphTo(Exclaim, 0.34);
  m_wobble = 1.0;
  m_scale = 1.12;
  m_scaleVelocity = 0.0;
  m_scaleTarget = 1.0;
  m_hold = 2.1;
}

void Mascot::notify() {
  wake();
  setMood(Notifying);
  morphTo(Circle, 0.3);
  m_badgeTarget = 1.0;
  // Wide, round, surprised eyes.
  m_eyeWidthTarget = 0.215;
  m_eyeHeightTarget = 0.235;
  m_eyeRoundTarget = 1.0;
  m_eyeTarget = QPointF(-0.02, 0.06);
  m_scale = 0.94;
  m_scaleVelocity = 1.1;
  m_scaleTarget = 1.0;
  m_hold = 3.4;
}

void Mascot::beginDrag() {
  wake();
  setMood(Held);
  m_hold = 0.0;
  m_squashXTarget = 0.92;
  m_squashYTarget = 1.10; // stretched, as if hanging from the cursor
  m_eyeWidthTarget = 0.155;
  m_eyeHeightTarget = 0.275;
}

void Mascot::endDrag(qreal throwSpeed) {
  setMood(Resting);
  m_idle = 0.0;
  m_squashXTarget = 1.0;
  m_squashYTarget = 1.0;
  m_eyeWidthTarget = kEyeWidth;
  m_eyeHeightTarget = kEyeHeight;
  m_eyeTarget = QPointF(kEyeRestX, kEyeRestY);
  m_lookingAtCursor = false;

  // Landing squash, scaled by how hard she was thrown.
  const qreal impact = qBound(0.0, throwSpeed, 1.0);
  m_squashX = 1.0 + 0.18 * impact;
  m_squashY = 1.0 - 0.16 * impact;
  m_scaleVelocity = -1.1 * impact;
  m_scaleTarget = 1.0;
}

void Mascot::setHovered(bool hovered) {
  if (m_hovered == hovered)
    return;
  m_hovered = hovered;
  if (hovered) {
    wake();
    if (m_mood == Resting) {
      m_eyeWidthTarget = 0.165;
      m_eyeHeightTarget = 0.27;
    }
  } else if (m_mood == Resting) {
    m_eyeWidthTarget = kEyeWidth;
    m_eyeHeightTarget = kEyeHeight;
  }
}

void Mascot::lookAt(qreal x, qreal y) {
  m_lookingAtCursor = true;
  // Compress the cursor offset into the small range the eyes can travel, so
  // she glances rather than swivelling wildly.
  const qreal reach = 0.34;
  const qreal nx = std::tanh(x * 0.55) * reach;
  const qreal ny = std::tanh(y * 0.55) * reach;
  m_eyeTarget = QPointF(kEyeRestX * 0.45 + nx, kEyeRestY * 0.45 + ny);
}

void Mascot::lookIdle() {
  m_lookingAtCursor = false;
  m_eyeTarget = QPointF(kEyeRestX, kEyeRestY);
}

void Mascot::setReducedMotion(bool reduced) {
  if (m_reduced == reduced)
    return;
  m_reduced = reduced;
  if (reduced) {
    m_wobble = 0.0;
    m_bobX = m_bobY = 0.0;
  }
}

void Mascot::setSleepWhenIdle(bool enabled) {
  m_sleepWhenIdle = enabled;
  if (!enabled && m_mood == Asleep)
    wake();
}

void Mascot::setIdleAntics(bool enabled) { m_idleAntics = enabled; }

void Mascot::rest() {
  setMood(Resting);
  snapForm(Circle);
  m_hold = 0.0;
  m_idle = 0.0;
  m_wobble = 0.0;
  m_rings = m_ringsTarget = 0.0;
  m_badge = m_badgeTarget = 0.0;
  m_dotsSpread = m_dotsSpreadTarget = 0.0;
  m_dotsShrink = m_dotsShrinkTarget = 0.0;
  m_dotsPhase = 0.0;
  m_squashX = m_squashY = m_squashXTarget = m_squashYTarget = 1.0;
  m_scale = m_scaleTarget = 1.0;
  m_scaleVelocity = 0.0;
  m_roll = m_rollTarget = 0.0;
  m_bobX = m_bobY = 0.0;
  m_blinkPhase = -1.0;
  scheduleBlink();
  m_eyeWidth = m_eyeWidthTarget = kEyeWidth;
  m_eyeHeight = m_eyeHeightTarget = kEyeHeight;
  m_eyeRound = m_eyeRoundTarget = 1.0;
  m_eyeCentre = m_eyeTarget = QPointF(kEyeRestX, kEyeRestY);
  m_lookingAtCursor = false;
  emit frame();
}
