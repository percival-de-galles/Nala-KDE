#pragma once
#include <QStringList>

class QApplication;
class QQuickWindow;
class Backend;
class Mascot;
class Orbits;
class Theme;

// Headless acceptance pass. Returns the process exit code: 0 when every check
// passes. Run with `ctest` or `nala --self-test`.
// Render one PNG per form into `directory`, for comparing against the
// reference animation. Needs a real display; the offscreen plugin has no GPU.
int capturePoses(QApplication &app, Mascot &mascot, Orbits &orbits,
                 QQuickWindow *window, const QString &directory);

int runSelfTest(QApplication &app, Backend &backend, Mascot &mascot,
                Orbits &orbits, Theme &theme, QQuickWindow *window,
                const QStringList &warnings, const QString &captureDir);
