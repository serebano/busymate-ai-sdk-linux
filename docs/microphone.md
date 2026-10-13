# Native microphone integration for Linux — adapter 1.0.0

This guide adds microphone access to the hosted chat in your GTK4 + WebKitGTK 6.0 app. A user taps the chat microphone or voice button; the chat sends a request to the app; the adapter runs its permission step; the chat starts capture only after a grant. Opening the chat never asks.

## How permission works on Linux

iOS and Android have an OS microphone permission an app can request. A desktop Linux app has none it can request:

- **Unsandboxed apps** (distribution packages, your own builds) can open any microphone through PipeWire or PulseAudio. There is no per-app runtime prompt.
- **Flatpak.** Audio capture needs the `--socket=pulseaudio` finish-arg (served by `pipewire-pulse` on PipeWire systems). It is granted at install time, not asked at runtime. xdg-desktop-portal has a Camera portal (cameras only, which this adapter denies anyway) and no microphone portal an app can call; the Device portal's microphone access is not callable by apps. Without the socket, capture fails and the chat shows its error. WebKitGTK runs its own web-process sandbox inside Flatpak (via `flatpak-spawn --sandbox`); no extra permission is needed for that.
- **Snap.** The `audio-record` interface must be connected (`snap connect <your-snap>:audio-record`); without it, capture fails. This is an OS-level gate the adapter cannot ask for or detect.

So the adapter's **permission step is an in-app consent**: when the chat asks, it emits `request-permission`; unhandled, it shows a modal `GtkAlertDialog` ("Allow the microphone?" — Don't Allow / Allow) on the web view's window. A grant lasts for the adapter's lifetime unless you call `busymate_microphone_revoke()`. You can take the step over with your own UI or a persisted app setting: return `TRUE` from the signal and call `busymate_microphone_complete_permission()`.

Capture itself goes through WebKitGTK's GStreamer backend to PipeWire (or PulseAudio). Desktop-level mutes or privacy switches can still make capture silent or fail; the SDK cannot see them.

## Install

Install the adapter **before the first chat load**, once per web view, and keep it alive for the web view's lifetime. Allow only the exact HTTPS origins serving your assistant. Include the actual chat frame origin and, when the chat is embedded in another page, that page's origin too (see the media check below).

```c
#include <busymate/busymate.h>

const char *origins[] = { "https://YOUR_ASSISTANT.busymate.ai", NULL };
BusymateMicrophone *microphone = busymate_microphone_new (web_view, origins);
webkit_settings_set_enable_webrtc (webkit_web_view_get_settings (web_view), TRUE); /* voice mode */
/* Configure your identity bridge before loading the chat too. */
webkit_web_view_load_uri (web_view, "https://YOUR_ASSISTANT.busymate.ai/?channel=desktop_sdk");
```

This is native app code: integrating it requires your app's own release. Apps without the adapter keep WebKitGTK's default behaviour, which **denies** media requests that no `permission-request` handler allows.

The adapter connects to the web view's `permission-request` signal and decides only `WebKitUserMediaPermissionRequest`s. It allows one only when **all** hold: audio only (no camera, no screen capture), the permission step granted, and the top-level document is an allowed origin. Other permission types are left to your handlers. Do not also decide user-media requests elsewhere; connect your own fallback with `g_signal_connect_after` as the example does.

WebKitGTK does not report which frame asked for user media, so the media check uses the top-level document's origin. A cross-origin chat iframe can only ask when its (allowed) parent page delegates the microphone through its iframe Permissions Policy (`allow="microphone"`), so the parent controls which frame may capture. The wire request itself is checked against the sending frame's own origin, read in the isolated script world.

## Event and reply contract

The handler is named `BusymateMicrophone`, registered in the isolated `busymate` script world. The page sees `window.BusymateMicrophone` (`postMessage` + `addEventListener("message")`), injected only into documents whose host is allowed, and posts a JSON string:

```json
{"type":"busymate.microphone.v1.request","id":"mic-unique-id","source":"dictation"}
```

`source` is `dictation` or `voice`. The adapter accepts messages only from allowed origins and bounds message size to 4096. The reply is a JSON string delivered as a `message` event, with the same id and a boolean `granted`:

```json
{"id":"mic-unique-id","granted":true}
```

Do not invoke the adapter from a page-load script or ask on opening chat: the chat owns the user-tap trigger and request correlation.

The chat waits before `getUserMedia` or voice session creation. A refusal, invalid reply or 60-second timeout shows the blocked notice. **Try again** asks again (a new tap, so a new prompt; there is no automatic prompt loop). Concurrent requests share one prompt. Each later activation rechecks the grant, so after `busymate_microphone_revoke()` the next tap asks again and WebKit capture is denied until then. Cancel dictation with another microphone tap, or leave voice mode with its keyboard control; a late permission result cannot start capture after cancel or unmount. Cancelling chat does not dismiss an open prompt or undo a grant.

## Troubleshooting

| Symptom | Check |
|---|---|
| No prompt on tap | A grant may already be held (call `revoke` to test). Confirm the adapter was installed before load and the exact chat origin is allowed. |
| Allowed, but the chat cannot record | Check the `media-decided` signal; confirm `enable-media-stream` is on, the top document's origin is allowed and no camera was requested. Check PipeWire/PulseAudio has an input device; under Flatpak, `--socket=pulseaudio`; under Snap, `audio-record` is connected. |
| Voice mode unavailable | Enable WebRTC (`enable-webrtc`); some distribution WebKitGTK builds ship without WebRTC. |
| Request times out | Confirm a `request-permission` handler that returns `TRUE` also calls `complete_permission`. |
| `already registered` warning | One adapter per content manager; uninstall the previous one first. |
| Camera or screen request denied | Expected: this adapter provides microphone access only. |

## Acceptance checklist for your app release

Verify on the desktops you support (at least one PipeWire system), with your packaging (deb/rpm, Flatpak, Snap):

1. Open chat: no prompt. Tap **microphone**, see the prompt, allow, dictate successfully. Repeat for **voice mode**.
2. Deny: see the blocked notice, no capture, no automatic prompt loop. Tap **Try again**, allow, and capture works.
3. Already granted: another activation starts without a prompt. Revoke and confirm the next activation asks again.
4. While the prompt is open, cancel dictation or leave voice mode; then allow and confirm capture does not start from the cancelled action.
5. Kill the web process (`kill` the `WebKitWebProcess`), confirm the chat reloads, and repeat an activation.
6. Confirm untrusted origins, camera and screen-capture requests are refused, and that the Flatpak/Snap build captures with the documented permissions and fails cleanly without them.

The SDK's CI runs a real WebKitGTK web view under Xvfb that exercises the request/reply path, origin checks and shared prompts with a scripted permission handler. It does not run the dialog with a person, open a microphone, or exercise PipeWire, portals, Flatpak or Snap. Record those results with your own release.

## Platform references

- [WebKitGTK: WebKitUserContentManager](https://webkitgtk.org/reference/webkitgtk/stable/class.UserContentManager.html) and [WebKitUserMediaPermissionRequest](https://webkitgtk.org/reference/webkitgtk/stable/class.UserMediaPermissionRequest.html)
- [Flatpak sandbox permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html)
- [XDG Desktop Portal: Camera](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html)
- [Snap audio-record interface](https://snapcraft.io/docs/audio-record-interface)
