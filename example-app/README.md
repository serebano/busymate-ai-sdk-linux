# GTK4 example app

A real GTK4 application consuming the **SDK library in the parent directory** (no copied or mock SDK code). It installs the identity bridge v2 (build 2.0.0) and, optionally, the microphone adapter 1.0.0 before loading the hosted chat in a `WebKitWebView`.

## Build and run

From the repository root, on a Linux desktop with the dependencies from the [README](../README.md):

```sh
meson setup _build
meson compile -C _build
_build/example-app/busymate-sdk-example
```

`--smoke-test` opens the window, loads the configured chat, prints the outcome and exits (CI runs it under Xvfb). The app is not installed by `meson install`.

## Try the live guest flow

The default URL is the real hosted chat `https://busymate.ai/support/busyproxy?channel=desktop&locale=en`, with assistant slug `busyproxy` and the explicit extra origin `https://busymate.ai`. This is a guest support assistant, not an authenticated example workspace or a local stub; its availability, knowledge and enabled features belong to that tenant. `channel=desktop` is the hosted product's desktop-shell channel.

Send a message, then tap dictation or voice if the hosted chat enables them. The adapter asks with its dialog only in response to that tap; the **Events** pane shows the permission step and WebKit's media decision. Real capture depends on your PipeWire/PulseAudio setup (see [microphone integration](../docs/microphone.md)).

## Settings and actual SDK controls

Open **Settings**, change controls, then **Apply and reload**. Applying uninstalls the previous bridge and adapter from the same web view, installs the new configuration and reloads.

| Control | What it configures |
|---|---|
| Assistant slug | `BusymateBridge:assistant`; validated slug. |
| Chat HTTPS URL | The URL loaded in the web view, keeping your tenant's query parameters. No credentials or fragment. |
| Locale | Sets or replaces the chat URL's `locale` parameter (`en`, `de`, `pt-BR`, …). |
| Exact HTTPS origins | `BusymateBridge:origins` and the microphone origin set. No wildcard, path, query or fragment; the chat origin must be present. Navigation outside them is refused. |
| Identified mode | Off: guest; the `mint` callback answers `not_signed_in` with no backend request. On: the `mint` callback POSTs to your backend. |
| Account ID | What the `get-account` callback returns in identified mode. **Not** a login or authentication proof. |
| Mint endpoint | Your authenticated HTTPS endpoint. |
| Backend session token | Optional Bearer credential for your existing backend, held in memory only. Never a signing key, admin key or service-role credential. |
| Native microphone | Installs or removes the microphone adapter. Off: the host denies WebKit user-media requests. |
| Close via action | Connects the `action` callback, which handles `close` only (`ready` is logged; other actions stay unhandled). |
| Close fallback | Connects the `close` callback. Off: no handler, so a `close` action is reported unhandled. |
| Forward accountChanged() | Applies the identity fields and calls the real `busymate_bridge_account_changed()`. No token is fabricated or pushed. |
| Revoke microphone | Calls `busymate_microphone_revoke()`: the next tap asks again. |
| Reset | Restores guest defaults in the dialog. It does not change any OS audio setting. |

Window activation sends `resume` automatically. A web-process crash is handled by the SDK (reload, then `restored`); the Events pane logs it. Closing chat navigates away from the chat document to end its media lifetime.

## Your authenticated backend

To test identified mode, use your real assistant/chat URL and your tenant backend. Each mint POSTs JSON with the SDK's `nonce`, optional `aud` and `reason`, plus the bridge-supplied `assistant` and `origin`, with `Cache-Control: no-store` and redirects refused (`SOUP_MESSAGE_NO_REDIRECT`), so authentication never travels to another endpoint. The response must be a 2xx JSON `{"token":"…","nonce":"<same nonce>"}`. 401/403 mean signed out; other errors, empty tokens, redirects and mismatched nonces fail closed (guest chat). The demo contains no backend implementation, login shortcut, signing key or fabricated identity.

## Events and verification

**Events** lists SDK callbacks and bridge events (`resume`, `state`, `restored`), requests by operation name, mint outcomes and media decisions. It logs no payloads, account ids, tokens, nonces or audio, keeps at most 100 entries and is not persisted.

`meson test -C _build --suite unit` runs `test-demo-config` against the same validator, callback factory and mint client compiled into the app, including a loopback HTTP server that proves redirects are refused and the session credential reaches only the configured endpoint. Real authenticated service use, the microphone dialog with a person, and audio capture need your own desktop and backend verification.
