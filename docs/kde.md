# KDE Plasma Wayland

This build runs Nala as a borderless, unmanaged XWayland overlay. KWin's
`nala-cursor` script supplies global pointer coordinates, so gaze, hover,
dragging, and monitor crossing work over native Wayland and XWayland apps.

## Install dependencies (Fedora)

```bash
sudo dnf install cmake gcc-c++ make \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtshadertools-devel \
  layer-shell-qt-devel libX11-devel
```

## Build and run

```bash
./scripts/build.sh
./scripts/run.sh
```

The KDE launcher is:

```bash
./bin/nala-kde
```

It installs/enables `kwin/nala-cursor`, then starts the XWayland overlay.
Re-running it is safe because Nala is single-instance. Native layer-shell mode
is available for experiments with `NALA_WAYLAND=1`, but XWayland is the
supported KDE path.

Run the complete test suite with:

```bash
./scripts/test.sh
```

## Application launcher entry

Create `~/.local/share/applications/nala.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=Nala
Comment=Desktop companion for KDE Plasma
Exec=/absolute/path/to/Nala/bin/nala-kde
Terminal=false
Categories=Utility;
```

Replace the `Exec` path with the checkout's absolute path. Refresh the KDE
menu with `kbuildsycoca6 --noincremental` if needed.

## Keyboard shortcut

Open **System Settings → Keyboard → Shortcuts → Custom Shortcuts**, add a
**Command or Script**, set its command to the same absolute `bin/nala-kde`
path, assign a key, and apply.

## Setup and troubleshooting

The first launcher run installs and enables the KWin script. Verify it with:

```bash
kpackagetool6 --type=KWin/Script --list
grep -n nala-cursorEnabled ~/.config/kwinrc
```

If KWin needs reloading:

```bash
kwriteconfig6 --file kwinrc --group Plugins --key nala-cursorEnabled true
qdbus-qt6 org.kde.KWin /KWin reconfigure
```

Verify Nala's D-Bus endpoint while it is running:

```bash
gdbus introspect --session --dest org.nala.Cursor --object-path /Cursor
```

The endpoint should expose `setCompositorPosition`. If gaze only works over
some windows, restart Nala through `bin/nala-kde`; launching the upstream
binary directly does not install or use the KWin bridge.

Nala's display choice and placement are stored in
`~/.config/nala/preferences.json`. Use Nala's settings or drag her across the
monitor boundary to change it.

## Architecture and syncing

Upstream now includes the KDE integration directly: QtDBus receives KWin's
global pointer, XCB Shape defines input-only regions without clipping the
visual surface, and XWayland provides the decoration-free always-on-top
window. The original Hyprland layer-shell path remains available.

Pull updates normally with `git pull`, then rebuild. Do not edit generated
files under `build/`.
