// KWin sees the pointer across native Wayland and XWayland windows. Nala
// registers the receiving session-bus endpoint while it is running.
function sendCursor() {
    const p = workspace.cursorPos;
    callDBus("org.nala.Cursor", "/Cursor", "org.nala.Cursor",
             "setCompositorPosition", p.x, p.y);
}

workspace.cursorPosChanged.connect(sendCursor);
sendCursor();
