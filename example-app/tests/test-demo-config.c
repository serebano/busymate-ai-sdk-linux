/* Tests for the example app's real settings validation, callback factory and
 * mint client, including a loopback server that proves redirects are refused.
 */
#include "demo-config.h"

#include <json-glib/json-glib.h>
#include <string.h>

static DemoValidated *
validate (DemoConfig *config, int expected_code)
{
  g_autoptr(GError) error = NULL;
  DemoValidated *valid = demo_config_validate (config, &error);

  if (expected_code < 0)
    {
      g_assert_no_error (error);
      g_assert_nonnull (valid);
    }
  else
    {
      g_assert_error (error, DEMO_CONFIG_ERROR, expected_code);
      g_assert_null (valid);
    }
  return valid;
}

static void
test_defaults (void)
{
  g_autoptr(DemoConfig) config = demo_config_new_default ();
  g_autoptr(DemoValidated) valid = validate (config, -1);

  g_assert_cmpstr (valid->assistant, ==, "busyproxy");
  g_assert_cmpstr (valid->chat_url, ==, "https://busymate.ai/support/busyproxy?channel=desktop&locale=en");
  g_assert_cmpuint (g_strv_length (valid->origins), ==, 1);
  g_assert_cmpstr (valid->origins[0], ==, "https://busymate.ai");
  g_assert_null (valid->mint_url);
  g_assert_false (config->signed_in);
  g_assert_true (config->microphone_enabled);
}

static void
test_locale (void)
{
  g_autofree char *a = demo_url_with_locale ("https://busymate.ai/support/x?channel=desktop&locale=en", "de");
  g_autofree char *b = demo_url_with_locale ("https://busymate.ai/support/x?channel=desktop", "pt-BR");
  g_autofree char *c = demo_url_with_locale ("https://busymate.ai/support/x", "fr");
  g_autofree char *d = demo_url_with_locale ("https://busymate.ai/support/x?locale=en&channel=desktop&locale=it", "es");
  g_autoptr(DemoConfig) config = demo_config_new_default ();

  g_assert_cmpstr (a, ==, "https://busymate.ai/support/x?channel=desktop&locale=de");
  g_assert_cmpstr (b, ==, "https://busymate.ai/support/x?channel=desktop&locale=pt-BR");
  g_assert_cmpstr (c, ==, "https://busymate.ai/support/x?locale=fr");
  g_assert_cmpstr (d, ==, "https://busymate.ai/support/x?locale=es&channel=desktop");
  g_free (config->locale);
  config->locale = g_strdup ("en&admin=1");
  g_assert_null (validate (config, DEMO_CONFIG_ERROR_LOCALE));
}

static void
test_invalid (void)
{
  struct { const char *field, *value; int code; } cases[] = {
    { "assistant", "Bad Slug", DEMO_CONFIG_ERROR_ASSISTANT },
    { "chat_url", "http://busymate.ai/support/busyproxy", DEMO_CONFIG_ERROR_CHAT_URL },
    { "chat_url", "https://user:pw@busymate.ai/", DEMO_CONFIG_ERROR_CHAT_URL },
    { "chat_url", "https://busymate.ai/#x", DEMO_CONFIG_ERROR_CHAT_URL },
    { "origins", "https://*.busymate.ai", DEMO_CONFIG_ERROR_ORIGIN },
    { "origins", "https://busymate.ai/", DEMO_CONFIG_ERROR_ORIGIN },
    { "origins", "https://busymate.ai?x=1", DEMO_CONFIG_ERROR_ORIGIN },
    { "origins", "http://busymate.ai", DEMO_CONFIG_ERROR_ORIGIN },
    { "origins", "https://support.acme.com", DEMO_CONFIG_ERROR_MISSING_ORIGIN },
    { "mint_url", "http://backend.acme.com/mint", DEMO_CONFIG_ERROR_BACKEND },
  };

  for (gsize i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autoptr(DemoConfig) config = demo_config_new_default ();
      char **slot = g_str_equal (cases[i].field, "assistant") ? &config->assistant
                  : g_str_equal (cases[i].field, "chat_url") ? &config->chat_url
                  : g_str_equal (cases[i].field, "origins") ? &config->origins : &config->mint_url;
      g_free (*slot);
      *slot = g_strdup (cases[i].value);
      g_assert_null (validate (config, cases[i].code));
    }
}

static void
test_origins_normalized (void)
{
  g_autoptr(DemoConfig) config = demo_config_new_default ();
  g_autoptr(DemoValidated) valid = NULL;

  g_free (config->origins);
  config->origins = g_strdup (" https://Busymate.AI:443 ,\nhttps://support.acme.com:8443, https://busymate.ai");
  valid = validate (config, -1);
  g_assert_cmpuint (g_strv_length (valid->origins), ==, 2);
  g_assert_cmpstr (valid->origins[0], ==, "https://busymate.ai");
  g_assert_cmpstr (valid->origins[1], ==, "https://support.acme.com:8443");
}

static void
test_identified_requires_backend (void)
{
  g_autoptr(DemoConfig) config = demo_config_new_default ();
  g_autoptr(DemoValidated) valid = NULL;

  config->signed_in = TRUE;
  g_assert_null (validate (config, DEMO_CONFIG_ERROR_ACCOUNT));
  g_free (config->account_id);
  config->account_id = g_strdup ("user-42");
  g_assert_null (validate (config, DEMO_CONFIG_ERROR_BACKEND));
  g_free (config->mint_url);
  config->mint_url = g_strdup ("https://backend.acme.com/busymate/mint");
  g_free (config->backend_session_token);
  config->backend_session_token = g_strdup ("abc\r\nX-Evil: 1");
  g_assert_null (validate (config, DEMO_CONFIG_ERROR_BACKEND));
  g_free (config->backend_session_token);
  config->backend_session_token = g_strdup ("session-abc");
  valid = validate (config, -1);
  g_assert_cmpstr (valid->mint_url, ==, "https://backend.acme.com/busymate/mint");
}

static void
test_callbacks (void)
{
  g_autoptr(DemoConfig) config = demo_config_new_default ();
  DemoCallbacks callbacks = demo_callbacks_for (config);

  g_assert_false (callbacks.connect_action);
  g_assert_true (callbacks.connect_close);
  config->handle_close_action = TRUE;
  config->close_callback_enabled = FALSE;
  callbacks = demo_callbacks_for (config);
  g_assert_true (callbacks.connect_action);
  g_assert_false (callbacks.connect_close);
  g_assert_true (demo_action_handles ("close"));
  g_assert_false (demo_action_handles ("ready"));
  g_assert_false (demo_action_handles ("delete_everything"));
}

static void
test_mint_body_and_decode (void)
{
  g_autofree char *body = demo_mint_body ("n1", "busymate-ai", NULL, "acme", "https://acme.busymate.ai");
  g_autofree char *token = NULL;
  const char *ok = "{\"token\":\"t1\",\"nonce\":\"n1\"}";
  const char *wrong_nonce = "{\"token\":\"t1\",\"nonce\":\"n2\"}";
  const char *empty_token = "{\"token\":\"\",\"nonce\":\"n1\"}";

  g_assert_cmpstr (body, ==, "{\"nonce\":\"n1\",\"aud\":\"busymate-ai\",\"assistant\":\"acme\",\"origin\":\"https://acme.busymate.ai\"}");
  g_assert_cmpint (demo_mint_decode (200, ok, strlen (ok), "n1", &token), ==, DEMO_MINT_TOKEN);
  g_assert_cmpstr (token, ==, "t1");
  g_clear_pointer (&token, g_free);
  g_assert_cmpint (demo_mint_decode (401, ok, strlen (ok), "n1", &token), ==, DEMO_MINT_NOT_SIGNED_IN);
  g_assert_cmpint (demo_mint_decode (403, NULL, 0, "n1", &token), ==, DEMO_MINT_NOT_SIGNED_IN);
  g_assert_cmpint (demo_mint_decode (302, ok, strlen (ok), "n1", &token), ==, DEMO_MINT_FAILED);
  g_assert_cmpint (demo_mint_decode (500, ok, strlen (ok), "n1", &token), ==, DEMO_MINT_FAILED);
  g_assert_cmpint (demo_mint_decode (200, wrong_nonce, strlen (wrong_nonce), "n1", &token), ==, DEMO_MINT_FAILED);
  g_assert_cmpint (demo_mint_decode (200, empty_token, strlen (empty_token), "n1", &token), ==, DEMO_MINT_FAILED);
  g_assert_cmpint (demo_mint_decode (200, "nope", 4, "n1", &token), ==, DEMO_MINT_FAILED);
  g_assert_null (token);
}

/* ── loopback backend ──────────────────────────────────────────────────── */

typedef struct {
  guint  mint_hits;
  char  *authorization;
  char  *cache_control;
} Backend;

static void
on_mint_path (SoupServer *server, SoupServerMessage *message, const char *path, GHashTable *query, gpointer data)
{
  Backend *backend = data;
  SoupMessageBody *request = soup_server_message_get_request_body (message);
  g_autoptr(GBytes) bytes = soup_message_body_flatten (request);
  g_autoptr(JsonParser) parser = json_parser_new ();
  g_autofree char *reply = NULL;
  const char *nonce;

  backend->mint_hits++;
  g_free (backend->authorization);
  backend->authorization = g_strdup (soup_message_headers_get_one (soup_server_message_get_request_headers (message), "Authorization"));
  g_free (backend->cache_control);
  backend->cache_control = g_strdup (soup_message_headers_get_one (soup_server_message_get_request_headers (message), "Cache-Control"));
  g_assert_true (json_parser_load_from_data (parser, g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes), NULL));
  nonce = json_object_get_string_member (json_node_get_object (json_parser_get_root (parser)), "nonce");
  reply = g_strdup_printf ("{\"token\":\"fresh-token\",\"nonce\":\"%s\"}", nonce);
  soup_server_message_set_status (message, 200, NULL);
  soup_server_message_set_response (message, "application/json", SOUP_MEMORY_COPY, reply, strlen (reply));
}

static void
on_redirect_path (SoupServer *server, SoupServerMessage *message, const char *path, GHashTable *query, gpointer data)
{
  soup_server_message_set_status (message, 307, NULL);
  soup_message_headers_replace (soup_server_message_get_response_headers (message), "Location", "/mint");
}

static void
on_signed_out_path (SoupServer *server, SoupServerMessage *message, const char *path, GHashTable *query, gpointer data)
{
  soup_server_message_set_status (message, 401, NULL);
}

typedef struct {
  GMainLoop       *loop;
  DemoMintOutcome  outcome;
  char            *token;
} Wait;

static void
on_sent (GObject *source, GAsyncResult *result, gpointer data)
{
  Wait *wait = data;

  wait->outcome = demo_mint_send_finish (result, &wait->token);
  g_main_loop_quit (wait->loop);
}

static DemoMintOutcome
send_to (SoupSession *session, const char *base, const char *path, char **token)
{
  g_autofree char *url = g_strconcat (base, path, NULL);
  g_autofree char *body = demo_mint_body ("nonce-7", "busymate-ai", "test", "acme", "https://acme.busymate.ai");
  Wait wait = { .loop = g_main_loop_new (NULL, FALSE) };

  demo_mint_send (session, url, body, "session-abc", "nonce-7", NULL, on_sent, &wait);
  g_main_loop_run (wait.loop);
  g_main_loop_unref (wait.loop);
  *token = wait.token;
  return wait.outcome;
}

static void
test_mint_loopback (void)
{
  Backend backend = { 0 };
  g_autoptr(SoupServer) server = soup_server_new (NULL, NULL);
  g_autoptr(SoupSession) session = soup_session_new ();
  g_autoptr(GError) error = NULL;
  GSList *uris;
  g_autofree char *base = NULL, *token = NULL;

  soup_server_add_handler (server, "/mint", on_mint_path, &backend, NULL);
  soup_server_add_handler (server, "/redirect", on_redirect_path, &backend, NULL);
  soup_server_add_handler (server, "/signed-out", on_signed_out_path, &backend, NULL);
  g_assert_true (soup_server_listen_local (server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error));
  g_assert_no_error (error);
  uris = soup_server_get_uris (server);
  base = g_uri_to_string (uris->data);
  if (g_str_has_suffix (base, "/"))
    base[strlen (base) - 1] = '\0';
  g_slist_free_full (uris, (GDestroyNotify) g_uri_unref);

  /* A redirect is refused: the session credential never reaches /mint. */
  g_assert_cmpint (send_to (session, base, "/redirect", &token), ==, DEMO_MINT_FAILED);
  g_assert_null (token);
  g_assert_cmpuint (backend.mint_hits, ==, 0);

  g_assert_cmpint (send_to (session, base, "/signed-out", &token), ==, DEMO_MINT_NOT_SIGNED_IN);
  g_assert_null (token);

  g_assert_cmpint (send_to (session, base, "/mint", &token), ==, DEMO_MINT_TOKEN);
  g_assert_cmpstr (token, ==, "fresh-token");
  g_assert_cmpuint (backend.mint_hits, ==, 1);
  g_assert_cmpstr (backend.authorization, ==, "Bearer session-abc");
  g_assert_cmpstr (backend.cache_control, ==, "no-store");
  g_free (backend.authorization);
  g_free (backend.cache_control);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/demo/defaults", test_defaults);
  g_test_add_func ("/demo/locale", test_locale);
  g_test_add_func ("/demo/invalid", test_invalid);
  g_test_add_func ("/demo/origins", test_origins_normalized);
  g_test_add_func ("/demo/identified", test_identified_requires_backend);
  g_test_add_func ("/demo/callbacks", test_callbacks);
  g_test_add_func ("/demo/mint/decode", test_mint_body_and_decode);
  g_test_add_func ("/demo/mint/loopback", test_mint_loopback);
  return g_test_run ();
}
