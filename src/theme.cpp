#include "theme.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

QString readFile(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll())
                                        : QString();
}

QColor mix(const QColor &a, const QColor &b, qreal t) {
  return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                          a.greenF() * (1 - t) + b.greenF() * t,
                          a.blueF() * (1 - t) + b.blueF() * t);
}

qreal luminance(const QColor &c) {
  return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

} // namespace

Theme::Theme(QString configRoot, QString stateRoot, QObject *parent)
    : QObject(parent), m_config(std::move(configRoot)),
      m_state(std::move(stateRoot)) {
  // Editors and generators often replace files rather than writing in place,
  // which produces a burst of notifications; collapse them into one reload.
  m_debounce.setSingleShot(true);
  m_debounce.setInterval(90);
  connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
  const auto touched = [this] { m_debounce.start(); };
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, touched);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, touched);
  reload();
}

void Theme::watchPaths() {
  const QStringList paths{m_config + "/gtk-4.0",
                          m_config + "/gtk-4.0/noctalia.css",
                          m_state + "/noctalia",
                          m_state + "/noctalia/settings.toml"};
  for (const QString &path : paths) {
    if (!QFileInfo::exists(path))
      continue;
    if (m_watcher.files().contains(path) ||
        m_watcher.directories().contains(path))
      continue;
    m_watcher.addPath(path);
  }
}

bool Theme::loadPalette() {
  const QString css = readFile(m_config + "/gtk-4.0/noctalia.css");
  static const QRegularExpression token(
      QStringLiteral("@define-color\\s+(\\w+)\\s+(#[0-9a-fA-F]{6})\\s*;"));

  QHash<QString, QColor> found;
  auto it = token.globalMatch(css);
  while (it.hasNext()) {
    const auto match = it.next();
    found.insert(match.captured(1), QColor(match.captured(2)));
  }

  const QStringList required{"window_bg_color", "window_fg_color",
                             "card_bg_color", "accent_bg_color",
                             "accent_fg_color"};
  for (const QString &key : required)
    if (!found.contains(key))
      return false;

  const QColor surface = found.value("window_bg_color");
  const QColor text = found.value("window_fg_color");
  const QColor card = found.value("card_bg_color");

  m_colors = {
      {"surface", surface},
      {"text", text},
      {"card", card},
      {"accent", found.value("accent_bg_color")},
      {"onAccent", found.value("accent_fg_color")},
      {"muted", mix(text, surface, 0.38)},
      {"outline", mix(card, text, 0.16)},
      {"hover", mix(card, text, 0.07)},
      {"pressed", mix(card, text, 0.14)},
      {"error", found.value("error_bg_color", QColor("#ed8796"))},
  };
  return true;
}

void Theme::loadShellSettings() {
  m_font = QStringLiteral("Adwaita Sans");
  m_radius = 1.0;
  m_motion = 1.0;
  bool animations = true;

  static const QRegularExpression entry(
      QStringLiteral("^(\\w+)\\s*=\\s*(.+)$"));

  QString section;
  const QString toml = readFile(m_state + "/noctalia/settings.toml");
  for (QString line : toml.split(u'\n')) {
    line = line.trimmed();
    if (line.startsWith(u'[')) {
      section = line;
      continue;
    }
    const auto match = entry.match(line);
    if (!match.hasMatch())
      continue;

    const QString key = match.captured(1);
    QString value = match.captured(2).split(u'#').first().trimmed();

    if (section == "[shell]" && key == "font_family" && value.size() > 1 &&
        value.startsWith(u'"') && value.endsWith(u'"'))
      m_font = value.mid(1, value.size() - 2);

    bool ok = false;
    const qreal number = value.toDouble(&ok);
    if (section == "[shell]" && key == "corner_radius_scale" && ok)
      m_radius = qBound(0.0, number, 2.0);
    if (section == "[shell.animation]" && key == "speed" && ok)
      m_motion = qBound(0.1, number, 3.0);
    if (section == "[shell.animation]" && key == "enabled")
      animations = value != "false";
  }

  if (!animations)
    m_motion = 0.0;
}

void Theme::reload() {
  watchPaths();

  const QVariantMap oldColors = m_colors;
  const QString oldFont = m_font;
  const qreal oldRadius = m_radius, oldMotion = m_motion;
  const bool wasAvailable = m_available;

  const bool parsed = loadPalette();
  m_available = parsed;
  if (!parsed && m_colors.isEmpty()) {
    // Sensible standalone defaults so Nala still looks deliberate on a desktop
    // that is not running Noctalia.
    const QColor surface("#1c1d22"), text("#e6e6ea"), card("#2a2c33");
    m_colors = {{"surface", surface},   {"text", text},
                {"card", card},         {"accent", QColor("#b7bdf8")},
                {"onAccent", QColor("#24273a")},
                {"muted", mix(text, surface, 0.38)},
                {"outline", mix(card, text, 0.16)},
                {"hover", mix(card, text, 0.07)},
                {"pressed", mix(card, text, 0.14)},
                {"error", QColor("#ed8796")}};
  }

  loadShellSettings();

  if (oldColors != m_colors || oldFont != m_font || oldRadius != m_radius ||
      oldMotion != m_motion || wasAvailable != m_available)
    emit changed();
}

QColor Theme::mascotColor() const {
  // Nala is a solid silhouette, so she needs a colour that reads against the
  // desktop rather than against the settings surface. The accent carries the
  // wallpaper's character; darken or lighten it just enough to stay legible.
  const QColor accent = m_colors.value("accent").value<QColor>();
  if (!accent.isValid())
    return QColor("#0a090c");

  QColor result = accent;
  const qreal l = luminance(accent);
  if (l > 0.55)
    result = mix(accent, QColor(Qt::black), 0.45); // pale accent -> deepen
  else if (l < 0.12)
    result = mix(accent, QColor(Qt::white), 0.18); // near-black -> lift
  return result;
}
