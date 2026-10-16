# Changelog

## 1.0.2

Release packaging: the microphone adapter source (`src/busymate-microphone.c` sha256 `0e88a59d4dbf6a784f3ff98a623121d84ede366054d996fae47430c3f1081141`, `src/busymate/busymate-microphone.h` sha256 `09916d80daf51eaeb96fcf6ee99d88275e19ff09cc1f67cfd98ce328b54a46d1`; adapter 1.0.0, wire 1) is attached to every GitHub release and listed in `SHA256SUMS`; docs link the release (serebano/busymate-ai-src#2156). This repository and its releases are the single source of the adapter; busymate.ai no longer hosts adapter copies. No adapter or bridge code change: the identity wire v2 (build 2.0.0), the microphone adapter 1.0.0, the contract and every hashed wire file are byte-identical to 1.0.1.

## 1.0.1

The default hosted chat URL now uses `channel=desktop_sdk` (`https://busymate.ai/support/busyproxy?channel=desktop_sdk&locale=en`, `BUSYMATE_CHANNEL`). It is the hosted product's channel for third-party desktop SDK apps: the chat loads as the window's top-level page with the normal layout, has no Console title-bar inset, and applies `locale` on the first frame. `desktop` is reserved for Busymate's own Console app. Keep `channel=desktop_sdk` in your own chat URL. The identity wire v2 (build 2.0.0) and microphone adapter 1.0.0 are unchanged. Integration guide: https://busymate.ai/docs/guides/linux-sdk.

## 1.0.0

First independent Linux distribution: a GTK4 + WebKitGTK 6.0 C library (Meson, pkg-config `busymate-ai-sdk-1`, GObject Introspection `Busymate-1.0`) with the identity bridge v2 (wire 2, build 2.0.0, byte-compatible envelopes with the Android, iOS and JavaScript hosts) and the microphone adapter 1.0.0 (wire 1). The sending frame's origin is read in an isolated WebKit script world; web-process crashes reload the chat and send `restored`. No v1 identity bridge is provided on Linux. Includes a GTK4 example app with a settings dialog. Hosted chat receives updates separately; your desktop app release is needed to install or upgrade native SDK code.
