#include "mascot.h"
#include <QScopeGuard>
#include <QtMath>
#include <algorithm>

namespace {

// The eyes are features on a sphere just under the surface of her body, not
// decals sliding across a flat face. That is what the reference does, and it
// is why a glance carries them so far, draws them together as they approach
// the edge, and tilts the pair.
//
// Fitted to the reference by least squares over the frames where the eyes are
// normally open, solving (x/s)^2 + (gap/g0)^2 = 1. It reproduces the measured
// separation across the whole sweep:
//
//   |pair x|   0.00-0.12  0.12-0.25  0.25-0.38  0.38-0.75
//   measured       0.560      0.519      0.468      0.432
//   model          0.555      0.540      0.483      0.426
constexpr qreal kEyeSphere = 0.654;
constexpr qreal kEyeHalfGap = 0.442; // half the angular separation, radians

// Rest orientation, and how far a glance can carry the gaze. The reference
// sweeps the pair centre across roughly ±0.63 R horizontally and ±0.50 R
// vertically -- far further than a small translation would suggest.
constexpr qreal kRestYaw = 0.343;
constexpr qreal kRestPitch = -0.311;
// Capped so the outer eye never swings past the limb and vanishes. At the
// limit the pair centre reaches 0.53 R, which is where the reference's own
// wide glances sit (its rare 0.66 R outliers are morph frames, not glances).
constexpr qreal kMaxYaw = 0.95;
constexpr qreal kMaxPitch = 0.88;

// How much of the sphere's natural tilt to keep. The full rotation would swing
// the pair to 0.32 R of tilt at the extremes; the reference never exceeds
// 0.185, so only part of it is applied.
constexpr qreal kTiltDamping = 0.55;

// The slits lean with the sphere's surface. The reference shows the sign
// clearly -- leaning right when she looks up, left when she looks down -- but
// the magnitude is noisy, so only part of the geometric angle is applied.
constexpr qreal kSlitRotation = 0.45;

// Base half-extents. The reference keeps a near-constant height/width ratio of
// about 1.55 whatever direction she looks in.
constexpr qreal kEyeWidth = 0.170;
constexpr qreal kEyeHeight = 0.263;

// Measured over the 34 blinks in the reference: a full cycle runs 243 ms,
// spent 121 ms closing, ~50 ms fully shut, then 84 ms opening. The lids close
// more slowly than they open, which is the opposite of the obvious guess, and
// they shut completely rather than leaving a slit.
// The cycle is a touch longer than the 243 ms measured, because that figure
// is taken between the 90%-open crossings rather than end to end.
constexpr qreal kBlinkSeconds = 0.29;
constexpr qreal kBlinkClose = 0.47; // fraction of the cycle spent closing
constexpr qreal kBlinkHold = 0.75;  // ... and fully shut, up to this point

constexpr qreal kSleepAfter = 75.0;

qreal easeInOut(qreal t) {
  t = qBound(0.0, t, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

} // namespace

Mascot::Mascot(QObject *parent)
    : QObject(parent), m_random(QRandomGenerator::securelySeeded()) {
  m_yaw = m_yawTarget = kRestYaw;
  m_pitch = m_pitchTarget = kRestPitch;
  placeEyes();
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

// Carry each eye around the sphere and project it back to the screen.
//
// Yaw is applied before pitch on purpose: in that order the separation axis
// itself tilts once the gaze is both sideways and up or down, which is the
// asymmetry the reference shows (the pair measures -0.02 tilt looking up and
// +0.13 level). Pitch-then-yaw would keep the pair perfectly level.
void Mascot::placeEyes() {
  const qreal cy = std::cos(m_yaw), sy = std::sin(m_yaw);
  const qreal cp = std::cos(m_pitch), sp = std::sin(m_pitch);

  const auto carry = [&](qreal side) {
    // Start on the front of the sphere, offset sideways by the half-gap.
    const qreal a = side * kEyeHalfGap;
    qreal x = std::sin(a), y = 0.0, z = std::cos(a);

    // Yaw about the vertical axis, then pitch about the horizontal one.
    qreal nx = x * cy + z * sy;
    qreal nz = -x * sy + z * cy;
    x = nx;
    z = nz;
    qreal ny = y * cp + z * sp;
    nz = -y * sp + z * cp;
    y = ny;
    z = nz;

    Eye eye;
    eye.position = QPointF(x * kEyeSphere, y * kEyeSphere);

    // Foreshortening is uniform, not a true tangent-plane projection. The
    // reference holds the eye's height/width ratio at about 1.5 whichever way
    // she looks (1.54 near centre, 1.48 far to the side), so both axes shrink
    // together as an eye nears the limb. Softening z by this much reproduces
    // the measured 0.324 -> 0.272 narrowing across the sweep.
    const qreal facing = 0.55 + 0.45 * std::max(0.0, z);
    eye.scale = QPointF(facing, facing);

    // Lean the slit along the sphere's local "up". The tangent frame at a
    // point e on the unit sphere has up = e x normalise(worldUp x e), whose
    // screen projection is (-xy/L, L) with L = hypot(x, z).
    const qreal L = std::hypot(x, z);
    if (L > 1e-4)
      eye.angle = std::atan2(-x * y / L, L) * kSlitRotation;

    return eye;
  };

  m_left = carry(-1.0);
  m_right = carry(1.0);

  // Damp the tilt towards the pair's own level without disturbing where the
  // pair as a whole is looking.
  const qreal level = (m_left.position.y() + m_right.position.y()) * 0.5;
  const auto damp = [&](Eye &eye) {
    eye.position.setY(level +
                      (eye.position.y() - level) * kTiltDamping);
  };
  damp(m_left);
  damp(m_right);
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
  //
  // These follow the morph exactly rather than settling on their own clock. An
  // independent spring lets them race ahead of the silhouette, which leaves
  // the blended shape reaching well past the dots it is supposed to be made
  // of -- and that showed up as the body greying out on the way in.
  // Strictly linear in the morph. Easing these ahead of the blend puts the
  // side dots inside a body that has not shrunk yet, and they grey it.
  const auto dotsness = [](int form) { return form == Dots ? 1.0 : 0.0; };
  const qreal amount = dotsness(m_formA) * (1.0 - m_formMix) +
                       dotsness(m_formB) * m_formMix;
  m_dotsSpread = m_dotsSpreadTarget = amount;
  m_dotsShrink = m_dotsShrinkTarget = amount;
  // Roughly one pulse per dot per second, as in the reference.
  if (m_dotsSpread > 0.01)
    m_dotsPhase += dt * 4.4;
}

// The reference blinks every 2.85 s on average, but as little as 0.6 s and as
// much as 6.4 s apart.
void Mascot::scheduleBlink() { m_nextBlink = random(0.7, 5.2); }

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
    m_yawTarget = random(-kMaxYaw * 0.75, kMaxYaw * 0.85);
    m_pitchTarget = random(-kMaxPitch * 0.8, kMaxPitch * 0.55);
    m_lookingAtCursor = false;
  } else if (pick < 5) {
    m_squashXTarget = 1.07;
    m_squashYTarget = 0.93;
    m_hold = 0.22;
  } else if (pick < 7) {
    morphTo(Egg, 0.26);
    m_hold = 1.4;
  } else if (pick < 9) {
    morphTo(Hex, 0.26);
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
        morphTo(Circle, 0.24);
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
    morphTo(Tiny, 0.30);
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

  // Gaze. Saccades are quick, so this settles faster than the body does.
  qreal yaw = m_yawTarget, pitch = m_pitchTarget;
  if (!m_lookingAtCursor && m_mood == Resting && m_idle < kSleepAfter &&
      !m_reduced) {
    // A little drift so a held gaze never looks painted on.
    yaw += std::sin(m_time * 0.37) * 0.035;
    pitch += std::sin(m_time * 0.51 + 2.1) * 0.025;
  }
  settle(m_yaw, yaw, dt, 9.0);
  settle(m_pitch, pitch, dt, 9.0);
  placeEyes();

  settle(m_eyeWidth, m_eyeWidthTarget, dt, 13.0);
  settle(m_eyeRound, m_eyeRoundTarget, dt, 10.0);

  if (m_blinkPhase >= 0.0) {
    const qreal t = m_blinkPhase;
    qreal closed; // 0 = wide open, 1 = shut
    if (t < kBlinkClose)
      closed = t / kBlinkClose;
    else if (t < kBlinkHold)
      closed = 1.0;
    else
      closed = 1.0 - (t - kBlinkHold) / (1.0 - kBlinkHold);
    // Drive the lid directly: the curve above *is* the timing, so smoothing it
    // again would stretch the blink past the 243 ms it is supposed to take.
    m_eyeHeight = m_eyeHeightTarget * (1.0 - easeInOut(closed));
  } else {
    settle(m_eyeHeight, m_eyeHeightTarget, dt, 13.0);
  }

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
    morphTo(Circle, 0.26);
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
  morphTo(Triangle, 0.20);
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
  morphTo(Triangle, 0.26);
  m_eyeWidthTarget = kEyeWidth;
  m_eyeHeightTarget = kEyeHeight;
  m_hold = std::max(seconds, 0.6);
}

void Mascot::alert() {
  wake();
  setMood(Alert);
  morphTo(Exclaim, 0.22);
  m_wobble = 1.0;
  m_scale = 1.12;
  m_scaleVelocity = 0.0;
  m_scaleTarget = 1.0;
  m_hold = 2.1;
}

void Mascot::notify() {
  wake();
  setMood(Notifying);
  morphTo(Circle, 0.20);
  m_badgeTarget = 1.0;
  // Wide, round, surprised eyes.
  m_eyeWidthTarget = 0.215;
  m_eyeHeightTarget = 0.235;
  m_eyeRoundTarget = 1.0;
  m_yawTarget = 0.0;
  m_pitchTarget = 0.12; // looking straight out, a touch downward
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
  m_yawTarget = kRestYaw;
  m_pitchTarget = kRestPitch;
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
  // Turn the cursor's offset into an orientation. tanh keeps a cursor far off
  // to one side from pinning the gaze at the very limit of its travel.
  m_yawTarget = std::tanh(x * 0.45) * kMaxYaw;
  m_pitchTarget = std::tanh(y * 0.45) * kMaxPitch;
}

void Mascot::lookIdle() {
  m_lookingAtCursor = false;
  m_yawTarget = kRestYaw;
  m_pitchTarget = kRestPitch;
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
  m_yaw = m_yawTarget = kRestYaw;
  m_pitch = m_pitchTarget = kRestPitch;
  placeEyes();
  m_lookingAtCursor = false;
  emit frame();
}
