# Changelog

## 1.0.1

The default hosted chat URL now uses `channel=desktop_sdk` (`https://busymate.ai/support/busyproxy?channel=desktop_sdk&locale=en`, `BUSYMATE_CHANNEL`). It is the hosted product's channel for third-party desktop SDK apps: the chat loads as the window's top-level page with the normal layout, has no Console title-bar inset, and applies `locale` on the first frame. `desktop` is reserved for Busymate's own Console app. Keep `channel=desktop_sdk` in your own chat URL. The identity wire v2 (build 2.0.0) and microphone adapter 1.0.0 are unchanged. Integration guide: https://busymate.ai/docs/guides/linux-sdk.

## 1.0.0

First independent Linux distribution: a GTK4 + WebKitGTK 6.0 C library (Meson, pkg-config `busymate-ai-sdk-1`, GObject Introspection `Busymate-1.0`) with the identity bridge v2 (wire 2, build 2.0.0, byte-compatible envelopes with the Android, iOS and JavaScript hosts) and the microphone adapter 1.0.0 (wire 1). The sending frame's origin is read in an isolated WebKit script world; web-process crashes reload the chat and send `restored`. No v1 identity bridge is provided on Linux. Includes a GTK4 example app with a settings dialog. Hosted chat receives updates separately; your desktop app release is needed to install or upgrade native SDK code.
