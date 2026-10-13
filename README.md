# Busymate AI SDK for Linux — 1.0.1

Official independent Linux SDK for Busymate AI hosted chat: a C library on GTK4 and WebKitGTK 6.0 (`webkitgtk-6.0`), built with Meson, with GObject Introspection data (`Busymate-1.0`) for Python, JavaScript (GJS), Vala and Rust bindings. It provides `BusymateBridge` (identity wire v2, immutable build 2.0.0) and `BusymateMicrophone` (microphone wire 1, adapter 1.0.0). There is no v1 identity bridge on Linux.

## Install

Requirements: GTK ≥ 4.10, WebKitGTK 6.0 ≥ 2.42, GLib ≥ 2.76, json-glib ≥ 1.6, Meson ≥ 1.0 (the example also needs libsoup 3). Ubuntu 24.04: `sudo apt install meson ninja-build gcc pkg-config libgtk-4-dev libwebkitgtk-6.0-dev libjson-glib-dev libsoup-3.0-dev gobject-introspection libgirepository1.0-dev`. Fedora: `gtk4-devel webkitgtk6.0-devel json-glib-devel libsoup3-devel gobject-introspection-devel`.

**As a Meson subproject (pinned).** Add `subprojects/busymate-ai-sdk-linux.wrap`:

```ini
[wrap-git]
url = https://github.com/serebano/busymate-ai-sdk-linux.git
revision = 1.0.1
depth = 1

[provide]
busymate-ai-sdk-1 = busymate_dep
```

then `dependency('busymate-ai-sdk-1', default_options: ['example=false', 'tests=false'])` in your `meson.build`.

**System install.** `git clone --branch 1.0.1 --depth 1 https://github.com/serebano/busymate-ai-sdk-linux.git && cd busymate-ai-sdk-linux && meson setup _build -Dexample=false && meson compile -C _build && sudo meson install -C _build`. Consumers use `pkg-config busymate-ai-sdk-1` and `#include <busymate/busymate.h>`; the typelib is `Busymate-1.0`. The [1.0.1 release](https://github.com/serebano/busymate-ai-sdk-linux/releases/tag/1.0.1) also attaches the source tarball and an Ubuntu 24.04 x86_64 build of the library (`usr/` tree with headers, `.pc`, `.gir`, `.typelib`), with `SHA256SUMS`. No distribution package (deb/rpm/Flatpak module) is claimed.

## Integrate

```c
#include <busymate/busymate.h>

const char *origins[] = { "https://support.acme.com", NULL };     /* your mapped support host, if any */
BusymateBridge *bridge = busymate_bridge_new ("acme", origins);
g_signal_connect (bridge, "get-account", G_CALLBACK (current_account_or_null), app);
g_signal_connect (bridge, "mint", G_CALLBACK (ask_your_backend), app);  /* answer with busymate_mint_request_return_token() */
busymate_bridge_install (bridge, web_view);

const char *chat[] = { "https://acme.busymate.ai", NULL };
BusymateMicrophone *microphone = busymate_microphone_new (web_view, chat);   /* optional */

webkit_web_view_load_uri (web_view, "https://acme.busymate.ai/?channel=desktop_sdk");
```

1. Read [identity and authentication](docs/identity.md).
2. Configure the [complete SDK reference](docs/configuration.md).
3. Follow [microphone and voice integration](docs/microphone.md), including PipeWire, Flatpak and Snap behaviour.
4. Validate on your target desktops using the acceptance checklist.

Install both before the first load, once per web view. The microphone permission step runs only when the user taps the chat's microphone or voice control; opening the chat never asks. The adapter grants WebKit capture for audio only, for an allowed origin, after that grant. Renderer (web process) crashes are handled by the bridge: it reloads the chat and sends `restored`.

## Run the example app

```sh
meson setup _build && meson compile -C _build
_build/example-app/busymate-sdk-example
```

The GTK4 example consumes the library from this repository. Its **Settings** dialog sets the chat URL, locale, exact origins, guest vs identified mode (the mint callback against your own HTTPS backend), the microphone adapter, close callbacks, `accountChanged()` and microphone revoke; the **Events** pane logs SDK callbacks without payloads. The default guest chat is `https://busymate.ai/support/busyproxy?channel=desktop_sdk&locale=en`. See the [example guide](example-app/README.md).

## Build, test and versions

`python3 scripts/verify-release.py`, then `meson setup _build -Dintrospection=enabled && meson compile -C _build && meson test -C _build`. The `webkit` test suite needs a display (use `xvfb-run`).

CI (`sdk-check`, Ubuntu 24.04) builds the library, introspection data, tests and example; runs the unit tests (envelope parsing, origin checks, example settings and mint client with a loopback redirect test), WebKitGTK integration tests under Xvfb (a real web view talking to the bridge and microphone adapter through the isolated-world transport, origin refusals, and recovery after the web process is killed, over a local HTTPS server), a Python GI check, an Xvfb smoke run of the example against the live guest chat, and a consumer build against the installed SDK. It does not verify a real microphone, PipeWire capture, portal or Flatpak sandbox behaviour, or your backend identity flow: those need a physical Linux desktop.

Distribution 1.0.1 differs from identity wire 2/build 2.0.0 and microphone wire 1/adapter 1.0.0. The [release manifest](release-manifest.json) pins the source hashes; new bytes require a new SDK version. This repository owns Linux SDK releases; human integration guides on [busymate.ai](https://busymate.ai/docs/guides/linux-sdk) are published by the hosted product. See the [changelog](CHANGELOG.md) and the [v2 wire contract](CONTRACT-v2.md).
