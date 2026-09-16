#pragma once
#include <QObject>
#include <QPointF>
#include <QRandomGenerator>

// Nala's behaviour. This class owns no rendering: it advances a small pile of
// animation state on `tick()` and publishes it as properties, which keeps the
// whole personality testable without a compositor or a GPU.
//
// Timings and proportions here were measured frame-by-frame from the reference
// animation (60 fps) rather than guessed -- see docs/animation.md.
class Mascot : public QObject {
  Q_OBJECT

  Q_PROPERTY(int formA READ formA NOTIFY frame)
  Q_PROPERTY(int formB READ formB NOTIFY frame)
  Q_PROPERTY(qreal formMix READ formMix NOTIFY frame)
  Q_PROPERTY(qreal dotsSpread READ dotsSpread NOTIFY frame)
  Q_PROPERTY(qreal dotsShrink READ dotsShrink NOTIFY frame)
  Q_PROPERTY(qreal dotsPhase READ dotsPhase NOTIFY frame)

  Q_PROPERTY(qreal squashX READ squashX NOTIFY frame)
  Q_PROPERTY(qreal squashY READ squashY NOTIFY frame)
  Q_PROPERTY(qreal bodyScale READ bodyScale NOTIFY frame)
  Q_PROPERTY(qreal roll READ roll NOTIFY frame)
  Q_PROPERTY(qreal bobX READ bobX NOTIFY frame)
  Q_PROPERTY(qreal bobY READ bobY NOTIFY frame)

  Q_PROPERTY(qreal eyeLeftX READ eyeLeftX NOTIFY frame)
  Q_PROPERTY(qreal eyeLeftY READ eyeLeftY NOTIFY frame)
  Q_PROPERTY(qreal eyeRightX READ eyeRightX NOTIFY frame)
  Q_PROPERTY(qreal eyeRightY READ eyeRightY NOTIFY frame)
  Q_PROPERTY(qreal eyeWidth READ eyeWidth NOTIFY frame)
  Q_PROPERTY(qreal eyeHeight READ eyeHeight NOTIFY frame)
  Q_PROPERTY(qreal eyeRound READ eyeRound NOTIFY frame)

  Q_PROPERTY(qreal badge READ badge NOTIFY frame)
  Q_PROPERTY(qreal rings READ rings NOTIFY frame)
  Q_PROPERTY(qreal time READ time NOTIFY frame)
  Q_PROPERTY(int mood READ mood NOTIFY moodChanged)
  Q_PROPERTY(bool sleeping READ sleeping NOTIFY moodChanged)
  Q_PROPERTY(bool dragging READ dragging NOTIFY moodChanged)

public:
  // Indices must match `form()` in shaders/mascot.frag.
  enum Form {
    Circle = 0,
    Egg = 1,
    Hex = 2,
    Triangle = 3,
    Exclaim = 4,
    Teardrop = 5,
    Dots = 6,
    Tiny = 7
  };
  Q_ENUM(Form)

  enum Mood { Resting, Happy, Thinking, Alert, Notifying, Asleep, Held };
  Q_ENUM(Mood)

  explicit Mascot(QObject *parent = nullptr);

  int formA() const { return m_formA; }
  int formB() const { return m_formB; }
  qreal formMix() const { return m_formMix; }
  qreal dotsSpread() const { return m_dotsSpread; }
  qreal dotsShrink() const { return m_dotsShrink; }
  qreal dotsPhase() const { return m_dotsPhase; }
  qreal squashX() const { return m_squashX; }
  qreal squashY() const { return m_squashY; }
  qreal bodyScale() const { return m_scale; }
  qreal roll() const { return m_roll; }
  qreal bobX() const { return m_bobX; }
  qreal bobY() const { return m_bobY; }
  qreal eyeLeftX() const { return m_eyeCentre.x() - m_eyeGap * 0.5; }
  qreal eyeLeftY() const { return m_eyeCentre.y(); }
  qreal eyeRightX() const { return m_eyeCentre.x() + m_eyeGap * 0.5; }
  qreal eyeRightY() const { return m_eyeCentre.y(); }
  qreal eyeWidth() const { return m_eyeWidth; }
  qreal eyeHeight() const { return m_eyeHeight; }
  qreal eyeRound() const { return m_eyeRound; }
  qreal badge() const { return m_badge; }
  qreal rings() const { return m_rings; }
  qreal time() const { return m_time; }
  int mood() const { return m_mood; }
  bool sleeping() const { return m_mood == Asleep; }
  bool dragging() const { return m_mood == Held; }

  // Interaction.
  Q_INVOKABLE void poke();
  Q_INVOKABLE void think(qreal seconds = 3.2);
  Q_INVOKABLE void alert();
  Q_INVOKABLE void notify();
  Q_INVOKABLE void wake();
  Q_INVOKABLE void beginDrag();
  Q_INVOKABLE void endDrag(qreal throwSpeed = 0.0);
  Q_INVOKABLE void setHovered(bool hovered);

  // `x`/`y` are the cursor position relative to Nala's centre, in units of her
  // radius; the caller decides whether that comes from the compositor or from
  // a local hover.
  Q_INVOKABLE void lookAt(qreal x, qreal y);
  Q_INVOKABLE void lookIdle();

  // Drive one frame. `dt` is seconds.
  Q_INVOKABLE void tick(qreal dt);

  void setReducedMotion(bool reduced);
  bool reducedMotion() const { return m_reduced; }
  void setSleepWhenIdle(bool enabled);
  void setIdleAntics(bool enabled);

  // Request a form directly. Like every other morph this waits for any
  // transition already in flight, so the silhouette never tears.
  Q_INVOKABLE void changeForm(int form, qreal seconds = 0.34);

  // Hard reset used by tests and by wake-up: abandon the current transition.
  Q_INVOKABLE void snapForm(int form);

  // Drop everything and return to the idle pose immediately: no rings, no
  // badge, no squash, eyes at rest. Backs the `rest` command.
  Q_INVOKABLE void rest();

signals:
  void frame();
  void moodChanged();

private:
  void setMood(Mood mood);
  void morphTo(int form, qreal seconds);
  void scheduleBlink();
  void advanceBlink(qreal dt);
  void advanceIdle(qreal dt);
  void advanceMorph(qreal dt);
  void settle(qreal &value, qreal target, qreal dt, qreal rate) const;
  qreal random(qreal lo, qreal hi);

  Mood m_mood = Resting;

  int m_formA = Circle, m_formB = Circle;
  qreal m_formMix = 1.0, m_morphRate = 0.0;
  int m_queuedForm = -1;
  qreal m_queuedSeconds = 0.0;

  qreal m_dotsSpread = 0.0, m_dotsSpreadTarget = 0.0;
  qreal m_dotsShrink = 0.0, m_dotsShrinkTarget = 0.0;
  qreal m_dotsPhase = 0.0;

  qreal m_squashX = 1.0, m_squashY = 1.0;
  qreal m_squashXTarget = 1.0, m_squashYTarget = 1.0;
  qreal m_scale = 1.0, m_scaleTarget = 1.0, m_scaleVelocity = 0.0;
  qreal m_roll = 0.0, m_rollTarget = 0.0;
  qreal m_bobX = 0.0, m_bobY = 0.0;

  QPointF m_eyeCentre{0.19, -0.13};
  QPointF m_eyeTarget{0.19, -0.13};
  qreal m_eyeGap = 0.47;
  qreal m_eyeWidth = 0.145, m_eyeHeight = 0.25, m_eyeRound = 1.0;
  qreal m_eyeWidthTarget = 0.145, m_eyeHeightTarget = 0.25;
  qreal m_eyeRoundTarget = 1.0;

  qreal m_blinkPhase = -1.0; // <0 means "not blinking"
  qreal m_nextBlink = 2.6;

  qreal m_badge = 0.0, m_badgeTarget = 0.0;
  qreal m_rings = 0.0, m_ringsTarget = 0.0;

  qreal m_time = 0.0;
  qreal m_hold = 0.0;     // seconds left in the current mood
  qreal m_idle = 0.0;     // seconds since the last interaction
  qreal m_antic = 7.0;    // seconds until the next spontaneous flourish
  qreal m_wobble = 0.0;   // decaying alert shake
  qreal m_cooldown = 0.0; // rate-limit on reactions

  bool m_reduced = false;
  bool m_sleepWhenIdle = true;
  bool m_idleAntics = true;
  bool m_hovered = false;
  bool m_lookingAtCursor = false;

  QRandomGenerator m_random;
};
