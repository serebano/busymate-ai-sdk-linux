/* WebKitGTK integration tests: a real WebKitWebView runs a page under an
 * https base URI (no network) and talks to the real bridge and microphone
 * adapter through the isolated-world transport. Needs a display (CI: xvfb).
 */
#include <busymate/busymate.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  GtkWindow     *window;
  WebKitWebView *view;
  GMainLoop     *loop;
  char          *title;
  const char    *want_prefix;
  guint          prompts;
  gboolean       grant;
  gboolean       closed;
  gboolean       account_changed_sent;
  BusymateBridge *bridge;
} Fixture;

static void
on_title (WebKitWebView *view, GParamSpec *pspec, Fixture *f)
{
  const char *title = webkit_web_view_get_title (view);

  if (title == NULL || f->want_prefix == NULL || !g_str_has_prefix (title, f->want_prefix))
    return;
  g_free (f->title);
  f->title = g_strdup (title + strlen (f->want_prefix));
  g_main_loop_quit (f->loop);
}

static gboolean
on_timeout (gpointer data)
{
  Fixture *f = data;

  g_main_loop_quit (f->loop);
  return G_SOURCE_REMOVE;
}

static void
fixture_setup (Fixture *f, gconstpointer data)
{
  f->view = WEBKIT_WEB_VIEW (webkit_web_view_new ());
  f->window = GTK_WINDOW (gtk_window_new ());
  gtk_window_set_child (f->window, GTK_WIDGET (f->view));
  gtk_window_set_default_size (f->window, 400, 300);
  gtk_window_present (f->window);
  f->loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (f->view, "notify::title", G_CALLBACK (on_title), f);
}

static void
fixture_teardown (Fixture *f, gconstpointer data)
{
  g_clear_object (&f->bridge);
  gtk_window_destroy (f->window);
  g_main_loop_unref (f->loop);
  g_free (f->title);
}

/* Load `html` as if served from `base`, wait for a title starting with `prefix`. */
static JsonObject *
run_page (Fixture *f, const char *html, const char *base, const char *prefix)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  guint timer;

  f->want_prefix = prefix;
  g_clear_pointer (&f->title, g_free);
  if (html != NULL)
    webkit_web_view_load_html (f->view, html, base);
  timer = g_timeout_add_seconds (30, on_timeout, f);
  g_main_loop_run (f->loop);
  g_source_remove (timer);
  g_assert_nonnull (f->title);
  g_assert_true (json_parser_load_from_data (parser, f->title, -1, NULL));
  return json_object_ref (json_node_get_object (json_parser_get_root (parser)));
}

static const char BRIDGE_PAGE[] =
  "<!doctype html><html><head><title>start</title></head><body><script>\n"
  "var out = { pageSeesHandler: typeof (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.BusymateBridge),\n"
  "            hasBridge: typeof window.BusymateBridge };\n"
  "var phase = 'done:';\n"
  "function show() { document.title = phase + JSON.stringify(out); }\n"
  "if (!window.BusymateBridge) { show(); } else {\n"
  "  window.BusymateBridge.addEventListener('message', function (e) {\n"
  "    var m = JSON.parse(e.data);\n"
  "    if (m.ev) { out['ev_' + m.ev] = m; phase = 'event:'; show(); return; }\n"
  "    out[m.id] = m;\n"
  "    if (out.h && out.s && out.m && out.a && out.u) show();\n"
  "  });\n"
  "  var send = function (o) { window.BusymateBridge.postMessage(JSON.stringify(o)); };\n"
  "  send({ bm: 2, id: 'h', op: 'hello' });\n"
  "  send({ bm: 2, id: 's', op: 'state' });\n"
  "  send({ bm: 2, id: 'm', op: 'mint', p: { nonce: 'n1', aud: 'busymate-ai', reason: 'test', origin: 'https://evil.example' } });\n"
  "  send({ bm: 2, id: 'a', op: 'action', p: { name: 'close' } });\n"
  "  send({ bm: 2, id: 'u', op: 'teleport' });\n"
  "}\n"
  "</script></body></html>";

static char *
account (BusymateBridge *bridge, gpointer data)
{
  return g_strdup ("user-1");
}

static gboolean
mint (BusymateBridge *bridge, BusymateMintRequest *request, gpointer data)
{
  g_assert_cmpstr (busymate_mint_request_get_origin (request), ==, "https://acme.busymate.ai");
  g_assert_cmpstr (busymate_mint_request_get_assistant (request), ==, "acme");
  g_assert_cmpstr (busymate_mint_request_get_audience (request), ==, "busymate-ai");
  busymate_mint_request_return_token (request, "token-1", busymate_mint_request_get_nonce (request));
  return TRUE;
}

static gboolean
mint_any (BusymateBridge *bridge, BusymateMintRequest *request, gpointer data)
{
  busymate_mint_request_return_token (request, "token-1", busymate_mint_request_get_nonce (request));
  return TRUE;
}

static void
on_close (BusymateBridge *bridge, Fixture *f)
{
  f->closed = TRUE;
}

static const char *
reply_string (JsonObject *out, const char *id, const char *path)
{
  JsonObject *reply = json_object_get_object_member (out, id);
  JsonObject *r = json_object_get_object_member (reply, "r");

  return json_object_get_string_member (r, path);
}

static void
test_bridge_allowed (Fixture *f, gconstpointer data)
{
  g_autoptr(JsonObject) out = NULL;
  g_autoptr(JsonObject) event = NULL;
  g_autofree char *key = busymate_account_key ("ai.busymate.sdk.test", "user-1");
  JsonObject *hello;

  f->bridge = busymate_bridge_new ("acme", NULL);
  g_object_set (f->bridge, "app-id", "ai.busymate.sdk.test", "app-version", "1.0.0", NULL);
  g_signal_connect (f->bridge, "get-account", G_CALLBACK (account), f);
  g_signal_connect (f->bridge, "mint", G_CALLBACK (mint), f);
  g_signal_connect (f->bridge, "close", G_CALLBACK (on_close), f);
  g_assert_true (busymate_bridge_install (f->bridge, f->view));

  out = run_page (f, BRIDGE_PAGE, "https://acme.busymate.ai/support", "done:");
  g_assert_cmpstr (json_object_get_string_member (out, "pageSeesHandler"), ==, "undefined");
  g_assert_cmpstr (json_object_get_string_member (out, "hasBridge"), ==, "object");

  hello = json_object_get_object_member (json_object_get_object_member (out, "h"), "r");
  g_assert_true (json_object_get_boolean_member (json_object_get_object_member (out, "h"), "ok"));
  g_assert_cmpstr (json_object_get_string_member (hello, "platform"), ==, "linux");
  g_assert_cmpstr (json_object_get_string_member (hello, "build"), ==, "2.0.0");
  g_assert_cmpstr (json_object_get_string_member (hello, "origin"), ==, "https://acme.busymate.ai");
  g_assert_cmpstr (json_object_get_string_member (hello, "assistant"), ==, "acme");
  g_assert_true (g_str_has_prefix (json_object_get_string_member (hello, "webview"), "WebKitGTK/"));
  g_assert_cmpstr (reply_string (out, "s", "account"), ==, key);
  g_assert_cmpstr (reply_string (out, "m", "token"), ==, "token-1");
  g_assert_cmpstr (reply_string (out, "m", "nonce"), ==, "n1");
  g_assert_true (json_object_get_boolean_member (json_object_get_object_member (json_object_get_object_member (out, "a"), "r"), "handled"));
  g_assert_true (f->closed);
  g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (out, "u"), "e"), ==, "unsupported");

  /* An event the app sends by itself reaches the frame that said hello. */
  busymate_bridge_account_changed (f->bridge);
  event = run_page (f, NULL, NULL, "event:");
  g_assert_true (json_object_get_boolean_member (json_object_get_object_member (json_object_get_object_member (event, "ev_state"), "r"), "signedIn"));
}

static void
test_bridge_wrong_port_denied (Fixture *f, gconstpointer data)
{
  const char *origins[] = { "https://support.acme.com:8443", NULL };
  g_autoptr(JsonObject) out = NULL;

  /* The host matches the script allow-list, the exact origin does not: refused natively. */
  f->bridge = busymate_bridge_new ("acme", origins);
  g_signal_connect (f->bridge, "get-account", G_CALLBACK (account), f);
  g_signal_connect (f->bridge, "mint", G_CALLBACK (mint), f);
  g_assert_true (busymate_bridge_install (f->bridge, f->view));
  out = run_page (f, BRIDGE_PAGE, "https://support.acme.com/", "done:");
  g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (out, "h"), "e"), ==, "denied");
  g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (out, "m"), "e"), ==, "denied");
}

static void
test_bridge_other_host_not_injected (Fixture *f, gconstpointer data)
{
  g_autoptr(JsonObject) out = NULL;

  f->bridge = busymate_bridge_new ("acme", NULL);
  g_assert_true (busymate_bridge_install (f->bridge, f->view));
  out = run_page (f, BRIDGE_PAGE, "https://other.busymate.ai/", "done:");
  g_assert_cmpstr (json_object_get_string_member (out, "hasBridge"), ==, "undefined");
}

static const char MIC_PAGE[] =
  "<!doctype html><html><head><title>start</title></head><body><script>\n"
  "var out = { hasMic: typeof window.BusymateMicrophone, replies: [] };\n"
  "function show() { document.title = 'mic:' + JSON.stringify(out); }\n"
  "if (!window.BusymateMicrophone) { show(); } else {\n"
  "  window.BusymateMicrophone.addEventListener('message', function (e) {\n"
  "    out.replies.push(JSON.parse(e.data));\n"
  "    if (out.replies.length === 2) show();\n"
  "  });\n"
  "  var ask = function (id, source) { window.BusymateMicrophone.postMessage(JSON.stringify(\n"
  "    { type: 'busymate.microphone.v1.request', id: id, source: source })); };\n"
  "  ask('mic-1', 'dictation');\n"
  "  ask('mic-2', 'voice');\n"
  "}\n"
  "</script></body></html>";

static gboolean
complete_later (gpointer data)
{
  BusymateMicrophone *microphone = data;

  busymate_microphone_complete_permission (microphone, GPOINTER_TO_INT (g_object_get_data (data, "grant")));
  return G_SOURCE_REMOVE;
}

static gboolean
on_request_permission (BusymateMicrophone *microphone, const char *origin, const char *source, Fixture *f)
{
  g_assert_cmpstr (origin, ==, "https://acme.busymate.ai");
  f->prompts++;
  g_object_set_data (G_OBJECT (microphone), "grant", GINT_TO_POINTER (f->grant));
  g_idle_add (complete_later, microphone);
  return TRUE;
}

static void
run_microphone (Fixture *f, gboolean grant)
{
  const char *origins[] = { "https://acme.busymate.ai", NULL };
  g_autoptr(BusymateMicrophone) microphone = busymate_microphone_new (f->view, origins);
  g_autoptr(JsonObject) out = NULL;
  JsonArray *replies;

  f->grant = grant;
  g_assert_true (busymate_microphone_is_installed (microphone));
  g_signal_connect (microphone, "request-permission", G_CALLBACK (on_request_permission), f);
  /* Loading the page alone never asks. */
  g_assert_cmpuint (f->prompts, ==, 0);
  out = run_page (f, MIC_PAGE, "https://acme.busymate.ai/", "mic:");
  replies = json_object_get_array_member (out, "replies");
  g_assert_cmpuint (json_array_get_length (replies), ==, 2);
  g_assert_cmpuint (f->prompts, ==, 1); /* concurrent asks share one prompt */
  for (guint i = 0; i < 2; i++)
    {
      JsonObject *reply = json_array_get_object_element (replies, i);
      g_assert_true (g_str_has_prefix (json_object_get_string_member (reply, "id"), "mic-"));
      g_assert_cmpint (json_object_get_boolean_member (reply, "granted"), ==, grant);
    }
  g_assert_cmpint (busymate_microphone_get_granted (microphone), ==, grant);
  busymate_microphone_uninstall (microphone);
}

static void
test_microphone_granted (Fixture *f, gconstpointer data)
{
  run_microphone (f, TRUE);
}

static void
test_microphone_denied (Fixture *f, gconstpointer data)
{
  run_microphone (f, FALSE);
}

static void
test_microphone_other_origin (Fixture *f, gconstpointer data)
{
  const char *origins[] = { "https://acme.busymate.ai", NULL };
  g_autoptr(BusymateMicrophone) microphone = busymate_microphone_new (f->view, origins);
  g_autoptr(JsonObject) out = NULL;

  out = run_page (f, MIC_PAGE, "https://evil.example/", "mic:");
  g_assert_cmpstr (json_object_get_string_member (out, "hasMic"), ==, "undefined");
}


/* ── crash recovery over a real local HTTPS origin ─────────────────────── */

static void
on_page (SoupServer *server, SoupServerMessage *message, const char *path, GHashTable *query, gpointer data)
{
  soup_server_message_set_status (message, 200, NULL);
  soup_server_message_set_response (message, "text/html", SOUP_MEMORY_STATIC, BRIDGE_PAGE, strlen (BRIDGE_PAGE));
}

static void
on_terminated (BusymateBridge *bridge, WebKitWebProcessTerminationReason reason, Fixture *f)
{
  g_object_set_data (G_OBJECT (bridge), "reason", GINT_TO_POINTER (reason + 1));
}

/* Kill our WebKitWebProcess children (CI runs without WebKit's bubblewrap sandbox, so they are direct children). */
static guint
kill_web_processes (void)
{
  g_autoptr(GDir) proc = g_dir_open ("/proc", 0, NULL);
  const char *name;
  guint killed = 0;

  while (proc != NULL && (name = g_dir_read_name (proc)) != NULL)
    {
      g_autofree char *path = g_strdup_printf ("/proc/%s/stat", name);
      g_autofree char *stat = NULL;
      char *close_paren;
      int ppid = 0;
      char state;

      if (!g_ascii_isdigit (*name) || !g_file_get_contents (path, &stat, NULL, NULL))
        continue;
      close_paren = strrchr (stat, ')');
      if (close_paren == NULL || strstr (stat, "(WebKitWebProces") == NULL ||
          sscanf (close_paren + 2, "%c %d", &state, &ppid) != 2 || ppid != getpid ())
        continue;
      if (kill (atoi (name), SIGKILL) == 0)
        killed++;
    }
  return killed;
}

static void
test_bridge_crash_restored (Fixture *f, gconstpointer data)
{
  const char *cert_path = g_getenv ("BUSYMATE_TEST_TLS_CERT");
  const char *key_path = g_getenv ("BUSYMATE_TEST_TLS_KEY");
  g_autoptr(GTlsCertificate) certificate = NULL;
  g_autoptr(SoupServer) server = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(JsonObject) first = NULL;
  g_autoptr(JsonObject) after = NULL;
  g_autofree char *origin = NULL, *url = NULL;
  const char *origins[2] = { NULL, NULL };
  GSList *uris;
  int port;

  if (cert_path == NULL || key_path == NULL)
    {
      g_test_skip ("set BUSYMATE_TEST_TLS_CERT and BUSYMATE_TEST_TLS_KEY (self-signed for localhost)");
      return;
    }
  certificate = g_tls_certificate_new_from_files (cert_path, key_path, &error);
  g_assert_no_error (error);
  server = soup_server_new ("tls-certificate", certificate, NULL);
  soup_server_add_handler (server, "/", on_page, NULL, NULL);
  g_assert_true (soup_server_listen_local (server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY | SOUP_SERVER_LISTEN_HTTPS, &error));
  g_assert_no_error (error);
  uris = soup_server_get_uris (server);
  port = g_uri_get_port (uris->data);
  g_slist_free_full (uris, (GDestroyNotify) g_uri_unref);
  origin = g_strdup_printf ("https://localhost:%d", port);
  url = g_strdup_printf ("%s/chat", origin);
  origins[0] = origin;
  webkit_network_session_allow_tls_certificate_for_host (webkit_web_view_get_network_session (f->view),
                                                         certificate, "localhost");

  f->bridge = busymate_bridge_new ("acme", origins);
  g_signal_connect (f->bridge, "get-account", G_CALLBACK (account), f);
  g_signal_connect (f->bridge, "mint", G_CALLBACK (mint_any), f);
  g_signal_connect (f->bridge, "web-process-terminated", G_CALLBACK (on_terminated), f);
  g_assert_true (busymate_bridge_install (f->bridge, f->view));

  f->want_prefix = "done:";
  webkit_web_view_load_uri (f->view, url);
  first = run_page (f, NULL, NULL, "done:");
  g_assert_true (json_object_get_boolean_member (json_object_get_object_member (first, "h"), "ok"));

  g_assert_cmpuint (kill_web_processes (), >, 0);
  after = run_page (f, NULL, NULL, "event:");
  g_assert_cmpint (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (f->bridge), "reason")), ==, WEBKIT_WEB_PROCESS_CRASHED + 1);
  g_assert_true (json_object_has_member (after, "ev_restored"));
  g_assert_true (json_object_get_boolean_member (json_object_get_object_member (after, "h"), "ok"));
  g_assert_cmpstr (webkit_web_view_get_uri (f->view), ==, url);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  gtk_init ();
  g_test_add ("/webkit/bridge/allowed", Fixture, NULL, fixture_setup, test_bridge_allowed, fixture_teardown);
  g_test_add ("/webkit/bridge/wrong-port-denied", Fixture, NULL, fixture_setup, test_bridge_wrong_port_denied, fixture_teardown);
  g_test_add ("/webkit/bridge/other-host-not-injected", Fixture, NULL, fixture_setup, test_bridge_other_host_not_injected, fixture_teardown);
  g_test_add ("/webkit/microphone/granted", Fixture, NULL, fixture_setup, test_microphone_granted, fixture_teardown);
  g_test_add ("/webkit/microphone/denied", Fixture, NULL, fixture_setup, test_microphone_denied, fixture_teardown);
  g_test_add ("/webkit/bridge/crash-restored", Fixture, NULL, fixture_setup, test_bridge_crash_restored, fixture_teardown);
  g_test_add ("/webkit/microphone/other-origin", Fixture, NULL, fixture_setup, test_microphone_other_origin, fixture_teardown);
  return g_test_run ();
}
