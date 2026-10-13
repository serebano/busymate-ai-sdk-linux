# Changelog

## 1.0.0

First independent Linux distribution: a GTK4 + WebKitGTK 6.0 C library (Meson, pkg-config `busymate-ai-sdk-1`, GObject Introspection `Busymate-1.0`) with the identity bridge v2 (wire 2, build 2.0.0, byte-compatible envelopes with the Android, iOS and JavaScript hosts) and the microphone adapter 1.0.0 (wire 1). The sending frame's origin is read in an isolated WebKit script world; web-process crashes reload the chat and send `restored`. No v1 identity bridge is provided on Linux. Includes a GTK4 example app with a settings dialog. Hosted chat receives updates separately; your desktop app release is needed to install or upgrade native SDK code.
