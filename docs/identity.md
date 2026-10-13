# Identity and authentication

Use `BusymateBridge` (identity wire v2, build 2.0.0). Linux has no v1 identity bridge. Install exactly one bridge per web view, before the first load.

Your authenticated backend mints a fresh single-use launch assertion for each `mint` signal. Never cache or reuse a token/nonce pair. Never put a signing key, Busymate admin key or service-role credential in the app. Use your existing authenticated HTTPS backend API. Answer with `busymate_mint_request_return_token (request, token, nonce)`, `busymate_mint_request_return_not_signed_in (request)` or `busymate_mint_request_return_failed (request)`, now or later on the main thread; keep a reference (`g_object_ref`) to answer later. Unanswered after 15 seconds, the bridge answers `timeout` itself. Preserve the nonce requested by v2 and bind assistant, audience and origin server-side: `busymate_mint_request_get_assistant` and `busymate_mint_request_get_origin` are set by the bridge, never by the page. Do not invent a public Busymate mint endpoint for your app: implement your tenant's authenticated backend endpoint using the official [identity guide](https://busymate.ai/docs/guides/mobile-in-app-support).

```c
static char *
current_account (BusymateBridge *bridge, gpointer app)
{
  return my_session_user_id_or_null (app);      /* (transfer full); NULL = signed out */
}

static gboolean
mint (BusymateBridge *bridge, BusymateMintRequest *request, gpointer app)
{
  my_backend_mint_async (app, busymate_mint_request_get_nonce (request),
                         busymate_mint_request_get_audience (request),
                         busymate_mint_request_get_assistant (request),
                         busymate_mint_request_get_origin (request),
                         on_minted, g_object_ref (request));   /* answer in on_minted */
  return TRUE;
}
```

`get-account` is read synchronously whenever the chat asks `state` or `mint`. Its hash — SHA-256(`app-id:account`), first 128 bits — identifies account changes; the raw id is never sent. `app-id` defaults to the default `GApplication` id, then the program name; set it explicitly (`g_object_set (bridge, "app-id", "com.acme.Desktop", NULL)`) so the key is stable. Call `busymate_bridge_account_changed()` after login, logout or an account switch; v2 sends a `state` event and the hosted chat owns identity refresh and clearing. Connect `close` to dismiss your host and `action` only for a fixed app-owned set of actions, returning whether handled. Do not expose arbitrary native execution.

Only these origins may ask: exactly `https://<assistant>.busymate.ai` plus the exact `https` origins you list. Another tenant's chat, a busymate.ai artifact or any other page is answered `denied` and never reaches your mint. The bridge verifies the **sending frame's** origin: WebKitGTK does not tell the UI process which frame posted a script message, so the native handler is registered in an isolated script world (`busymate`) the page cannot reach, and a document-start script in that world reads the frame's real `location.origin` and forwards it with the envelope. The page-facing `window.BusymateBridge` object (`postMessage` + `addEventListener("message")`, the Android/Electron shape) is injected only into documents whose host is allowed, and every envelope is still checked against the exact origin (scheme, host and port). A sandboxed frame (opaque origin `null`) is always refused. Events the app sends by itself (`resume`, `state`, `restored`) go only to the frame that last said `hello`.

Keep navigation restricted to your trusted chat URLs (the example refuses navigations outside its configured origins in `decide-policy`). External URLs open through the bridge's `open-uri` signal — unhandled, the SDK opens them with `GtkUriLauncher`. Identity availability failures keep chat anonymous (`not_signed_in`, `mint_failed`, `unsupported`); never log tokens, nonces, raw account ids or audio while troubleshooting.

The byte-frozen [v2 wire contract](../CONTRACT-v2.md) defines hello/state/mint/open/action, error envelopes, version negotiation and events. On Linux `hello` reports `platform: "linux"`, `transport: "message"` and `webview: "WebKitGTK/<version>"`. Its no-new-permissions statement applies to the identity bridge alone; the optional microphone adapter adds microphone access.

The example's authenticated-backend client refuses redirects (`SOUP_MESSAGE_NO_REDIRECT`) so the app session credential is never forwarded. Configure the final HTTPS mint endpoint directly. The example's identified-mode controls change only the native account reader; your own app authentication and backend session logout remain your responsibility.
