# Configuration reference

## `BusymateBridge` (identity v2, build 2.0.0)

| Setting | Behaviour |
|---|---|
| `assistant` (construct) | Required assistant slug; adds exactly `https://<assistant>.busymate.ai`. Obtain the actual assistant URL from Busymate; a marketing page is not a chat URL. |
| `origins` (construct) | Extra exact HTTPS origins for mapped support hosts/frames. No wildcards, paths or `http:`; invalid entries are ignored. `allowed-origins` / `busymate_bridge_get_allowed_origins()` returns the normalized list. |
| `app-id`, `app-version`, `app-build` | Reported in `hello`; `app-id` also salts the account key. |
| `get-account` signal | Returns the current account id, or `NULL` when signed out. Read when `state` or `mint` is asked. |
| `mint` signal | Required for identity: ask your authenticated backend for a fresh token/nonce for every request; never a cached assertion. Return `TRUE` and answer the `BusymateMintRequest`. Unhandled = `mint_failed`. |
| `action` signal (detailed by name) | Optional fixed app-action handler; return `TRUE` only when handled. |
| `close` signal | Optional host dismissal; fallback for an unhandled `close` action (only when connected). |
| `open-uri` signal | Optional; return `TRUE` when you opened the `https` URI yourself. Unhandled, `GtkUriLauncher` opens it. |
| `event-sent`, `request-received`, `web-process-terminated` | Informational, for logs. No payloads, tokens or ids. |

`busymate_bridge_install (bridge, web_view)` registers the handler in the isolated `busymate` world, adds two document-start scripts limited to the allowed hosts, enables JavaScript, and connects `web-process-terminated`, `load-changed` and the window's `is-active` (for `resume`). Returns `FALSE` (and stays inert, guest chat) when no valid origin is configured or a `BusymateBridge` handler already exists on that content manager. `busymate_bridge_uninstall()` removes everything; pending mints answer `mint_failed`. `busymate_bridge_resume()` sends `resume` for other host-specific resumes.

Crash handling: when the web process crashes or exceeds its memory limit, the bridge forgets the old frame, fails pending mints, reloads the last chat URL in the same `WebKitWebView` (WebKitGTK starts a new web process) and sends `restored` after the chat says `hello` again. Termination you request through the API is not reloaded. Nothing to forward: you do not replace the web view as on Android.

## `BusymateMicrophone` (wire 1, adapter 1.0.0)

`busymate_microphone_new (web_view, origins)` takes only the web view and exact origins: scheme + host + optional port; no trailing slash, path, credentials, wildcard, query or fragment; hosts lower-case; `:443` dropped. Install it once before load, not on each tap. It enables `enable-media-stream` and decides WebKit `permission-request` signals for user media on that web view (other permission types are left to you). Voice mode in the hosted chat uses WebRTC: enable `webkit_settings_set_enable_webrtc()` when your WebKitGTK build supports it.

| API | Behaviour |
|---|---|
| `request-permission` signal | Emitted only after a chat tap when no grant is held; concurrent asks share one emission. Return `TRUE` and call `busymate_microphone_complete_permission()` to use your own UI. Unhandled, a modal `GtkAlertDialog` asks. |
| `busymate_microphone_get_granted()` / `granted` | Whether the user granted in this adapter's lifetime. |
| `busymate_microphone_revoke()` | Forgets the grant: capture is denied until the next tap asks again. |
| `media-decided` signal | Informational: a WebKit user-media request was allowed or denied. |
| `busymate_microphone_uninstall()` | Removes handler, scripts and permission handling; waiting asks answer `granted: false`. |

Request type `busymate.microphone.v1.request`, source `dictation` or `voice`, correlated `id`; the reply has the same `id` and a boolean `granted`. See [microphone integration](microphone.md) for denial, Retry, revocation, cancellation and sandbox behaviour.

There is no SDK setting for automatic prompts on page load, camera or screen access, timeout overrides, audio routing, provider credentials or token caching. Hosted chat appearance, theme, locale, voice availability and tenant behaviour are configured through the hosted chat/embed surface and Busymate Console. Preserve the tenant-generated chat URL with its supported query parameters; desktop hosts use `channel=desktop`. Layout, window management and audio routing belong to your host.

## Threading and lifetime

Use the SDK from the GTK main thread. Keep the bridge and adapter alive for the web view's lifetime (`g_object_unref` uninstalls on dispose). One `BusymateBridge` and one `BusymateMicrophone` per `WebKitUserContentManager`; uninstall before installing replacements on the same web view. The SDK does not create, destroy or re-parent your web view.
