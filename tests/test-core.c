/* Unit tests for the WebKit-free pipe: envelope handling, origin checks,
 * account keys and the microphone wire. No display needed.
 */
#define BUSYMATE_COMPILATION
#include "busymate-core-private.h"

static const char * const ALLOWED[] = { "https://acme.busymate.ai", "https://support.acme.com", NULL };

static BusymateCoreConfig
config (void)
{
  BusymateCoreConfig c = {
    .assistant = "acme",
    .origins = ALLOWED,
    .app_id = "com.acme.desktop",
    .app_version = "3.1",
    .app_build = NULL,
    .webview = "WebKitGTK/2.44.0",
    .transport = "message",
  };
  return c;
}

static char *
signed_in (gpointer data)
{
  return g_strdup (data);
}

static JsonObject *
parse (const char *json)
{
  g_autoptr(JsonParser) parser = json_parser_new ();

  g_assert_true (json_parser_load_from_data (parser, json, -1, NULL));
  return json_object_ref (json_node_get_object (json_parser_get_root (parser)));
}

static BusymateRoute
handle (const char *raw, const char *origin, const char *account, char **reply, BusymateCall **call)
{
  BusymateCoreConfig c = config ();

  return busymate_core_handle (&c, raw, origin, signed_in, (gpointer) account, reply, call);
}

static void
test_normalize_origin (void)
{
  struct { const char *in, *out; } cases[] = {
    { "https://acme.busymate.ai", "https://acme.busymate.ai" },
    { "  HTTPS://Support.ACME.com/ ", "https://support.acme.com" },
    { "https://support.acme.com:443", "https://support.acme.com" },
    { "https://support.acme.com:8443", "https://support.acme.com:8443" },
    { "http://support.acme.com", NULL },
    { "https://*.busymate.ai", NULL },
    { "https://support.acme.com/path", NULL },
    { "https://user@support.acme.com", NULL },
    { "null", NULL },
    { "", NULL },
    { NULL, NULL },
  };

  for (gsize i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autofree char *value = busymate_normalize_origin (cases[i].in);
      g_assert_cmpstr (value, ==, cases[i].out);
    }
}

static void
test_allowed_origins (void)
{
  const char *extra[] = { "https://support.acme.com/", "https://*.acme.com", "http://x.com",
                          "https://acme.busymate.ai", "https://Support.Acme.com:443", NULL };
  g_auto(GStrv) list = busymate_allowed_origins ("acme", extra);
  g_auto(GStrv) bad_slug = busymate_allowed_origins ("Acme!", NULL);
  g_auto(GStrv) none = busymate_allowed_origins (NULL, NULL);

  g_assert_cmpuint (g_strv_length (list), ==, 2);
  g_assert_cmpstr (list[0], ==, "https://acme.busymate.ai");
  g_assert_cmpstr (list[1], ==, "https://support.acme.com");
  g_assert_cmpuint (g_strv_length (bad_slug), ==, 0);
  g_assert_cmpuint (g_strv_length (none), ==, 0);
}

static void
test_account_key (void)
{
  g_autofree char *key = busymate_account_key ("com.acme.desktop", "user-42");
  g_autofree char *full = g_compute_checksum_for_string (G_CHECKSUM_SHA256, "com.acme.desktop:user-42", -1);

  g_assert_cmpuint (strlen (key), ==, 32);
  g_assert_true (g_str_has_prefix (full, key));
  g_assert_null (strstr (key, "user-42"));
}

static void
test_ignored (void)
{
  const char *ignored[] = {
    "not json", "[]", "{\"bm\":1,\"id\":\"a\",\"op\":\"hello\"}", "{\"bm\":\"2\",\"id\":\"a\",\"op\":\"hello\"}",
    "{\"bm\":2,\"op\":\"hello\"}", "{\"bm\":2,\"id\":null,\"op\":\"hello\"}", NULL,
  };

  for (gsize i = 0; i < G_N_ELEMENTS (ignored); i++)
    {
      g_autofree char *reply = NULL;
      g_autoptr(BusymateCall) call = NULL;
      g_assert_cmpint (handle (ignored[i], "https://acme.busymate.ai", NULL, &reply, &call), ==, BUSYMATE_ROUTE_IGNORE);
      g_assert_null (reply);
      g_assert_null (call);
    }
}

static void
test_oversized_ignored (void)
{
  g_autoptr(GString) big = g_string_new ("{\"bm\":2,\"id\":\"a\",\"op\":\"hello\",\"pad\":\"");
  g_autofree char *reply = NULL;
  g_autoptr(BusymateCall) call = NULL;

  while (big->len <= BUSYMATE_MAX_ENVELOPE)
    g_string_append (big, "xxxxxxxxxxxxxxxx");
  g_string_append (big, "\"}");
  g_assert_cmpint (handle (big->str, "https://acme.busymate.ai", NULL, &reply, &call), ==, BUSYMATE_ROUTE_IGNORE);
}

static void
test_denied_origins (void)
{
  const char *refused[] = {
    "https://other.busymate.ai", "https://busymate.ai", "http://acme.busymate.ai", "null", "",
    "https://acme.busymate.ai.evil.com", "https://support.acme.com:444", NULL,
  };

  for (gsize i = 0; i < G_N_ELEMENTS (refused); i++)
    {
      g_autofree char *reply = NULL;
      g_autoptr(BusymateCall) call = NULL;
      g_autoptr(JsonObject) body = NULL;

      g_assert_cmpint (handle ("{\"bm\":2,\"id\":\"q1\",\"op\":\"mint\",\"p\":{\"nonce\":\"n\"}}", refused[i], "u1",
                               &reply, &call), ==, BUSYMATE_ROUTE_REPLY);
      g_assert_cmpstr (reply, ==, "{\"ok\":false,\"e\":\"denied\",\"bm\":2,\"id\":\"q1\"}");
      g_assert_null (call->request);
    }
}

static void
test_hello (void)
{
  g_autofree char *reply = NULL;
  g_autoptr(BusymateCall) call = NULL;

  g_assert_cmpint (handle ("{\"bm\":2,\"id\":\"h1\",\"op\":\"hello\",\"p\":{},\"future\":1}", "https://Support.Acme.com",
                           NULL, &reply, &call), ==, BUSYMATE_ROUTE_REPLY);
  g_assert_cmpstr (call->op, ==, "hello");
  g_assert_cmpstr (call->origin, ==, "https://support.acme.com");
  g_assert_cmpstr (reply, ==,
    "{\"ok\":true,\"r\":{\"bridge\":\"busymate\",\"v\":2,\"build\":\"2.0.0\",\"platform\":\"linux\","
    "\"transport\":\"message\",\"assistant\":\"acme\",\"origin\":\"https://support.acme.com\","
    "\"origins\":[\"https://acme.busymate.ai\",\"https://support.acme.com\"],"
    "\"caps\":[\"hello\",\"state\",\"mint\",\"open\",\"action\",\"ev:resume\",\"ev:state\",\"ev:restored\"],"
    "\"webview\":\"WebKitGTK/2.44.0\",\"app\":{\"id\":\"com.acme.desktop\",\"version\":\"3.1\",\"build\":null}},"
    "\"bm\":2,\"id\":\"h1\"}");
}

static void
test_numeric_id_echoed (void)
{
  g_autofree char *reply = NULL;
  g_autoptr(BusymateCall) call = NULL;

  handle ("{\"bm\":2.0,\"id\":7,\"op\":\"nope\"}", "https://acme.busymate.ai", NULL, &reply, &call);
  g_assert_cmpstr (reply, ==, "{\"ok\":false,\"e\":\"unsupported\",\"bm\":2,\"id\":7}");
}

static void
test_state (void)
{
  g_autofree char *out = NULL, *in = NULL, *key = busymate_account_key ("com.acme.desktop", "u1");
  g_autofree char *expected = g_strdup_printf ("{\"ok\":true,\"r\":{\"signedIn\":true,\"account\":\"%s\"},\"bm\":2,\"id\":\"s\"}", key);
  g_autoptr(BusymateCall) a = NULL;
  g_autoptr(BusymateCall) b = NULL;

  handle ("{\"bm\":2,\"id\":\"s\",\"op\":\"state\"}", "https://acme.busymate.ai", NULL, &out, &a);
  g_assert_cmpstr (out, ==, "{\"ok\":true,\"r\":{\"signedIn\":false,\"account\":null},\"bm\":2,\"id\":\"s\"}");
  handle ("{\"bm\":2,\"id\":\"s\",\"op\":\"state\"}", "https://acme.busymate.ai", "u1", &in, &b);
  g_assert_cmpstr (in, ==, expected);
}

static void
test_mint (void)
{
  g_autofree char *guest = NULL, *reply = NULL, *json = NULL;
  g_autoptr(BusymateCall) a = NULL;
  g_autoptr(BusymateCall) b = NULL;
  g_autoptr(JsonNode) node = json_node_new (JSON_NODE_OBJECT);

  g_assert_cmpint (handle ("{\"bm\":2,\"id\":\"m\",\"op\":\"mint\",\"p\":{\"nonce\":\"n1\"}}", "https://acme.busymate.ai",
                           NULL, &guest, &a), ==, BUSYMATE_ROUTE_REPLY);
  g_assert_cmpstr (guest, ==, "{\"ok\":false,\"e\":\"not_signed_in\",\"bm\":2,\"id\":\"m\"}");

  /* The page cannot choose who asked: assistant and origin are overwritten. */
  g_assert_cmpint (handle ("{\"bm\":2,\"id\":\"m\",\"op\":\"mint\",\"p\":{\"nonce\":\"n1\",\"aud\":\"busymate-ai\","
                           "\"reason\":\"launch\",\"assistant\":\"evil\",\"origin\":\"https://evil.com\"}}",
                           "https://acme.busymate.ai/", "u1", &reply, &b), ==, BUSYMATE_ROUTE_MINT);
  g_assert_null (reply);
  json_node_set_object (node, b->request);
  json = json_to_string (node, FALSE);
  g_assert_cmpstr (json, ==, "{\"nonce\":\"n1\",\"aud\":\"busymate-ai\",\"reason\":\"launch\","
                             "\"assistant\":\"acme\",\"origin\":\"https://acme.busymate.ai\"}");
}

static void
test_open (void)
{
  const char *bad[] = {
    "{\"bm\":2,\"id\":\"o\",\"op\":\"open\",\"p\":{\"url\":\"http://acme.com\"}}",
    "{\"bm\":2,\"id\":\"o\",\"op\":\"open\",\"p\":{\"url\":\"file:///etc/passwd\"}}",
    "{\"bm\":2,\"id\":\"o\",\"op\":\"open\",\"p\":{\"url\":\"https://\"}}",
    "{\"bm\":2,\"id\":\"o\",\"op\":\"open\"}",
  };
  g_autofree char *reply = NULL;
  g_autoptr(BusymateCall) call = NULL;

  for (gsize i = 0; i < G_N_ELEMENTS (bad); i++)
    {
      g_autofree char *refused = NULL;
      g_autoptr(BusymateCall) c = NULL;
      g_assert_cmpint (handle (bad[i], "https://acme.busymate.ai", NULL, &refused, &c), ==, BUSYMATE_ROUTE_REPLY);
      g_assert_cmpstr (refused, ==, "{\"ok\":false,\"e\":\"bad_request\",\"bm\":2,\"id\":\"o\"}");
    }
  g_assert_cmpint (handle ("{\"bm\":2,\"id\":\"o\",\"op\":\"open\",\"p\":{\"url\":\"https://acme.com/login\",\"mode\":\"auth\"}}",
                           "https://acme.busymate.ai", NULL, &reply, &call), ==, BUSYMATE_ROUTE_OPEN);
  g_assert_cmpstr (call->url, ==, "https://acme.com/login");
  g_assert_cmpstr (call->mode, ==, "auth");
}

static void
test_action (void)
{
  g_autofree char *reply = NULL, *bad = NULL;
  g_autoptr(BusymateCall) call = NULL;
  g_autoptr(BusymateCall) refused = NULL;
  g_autoptr(GString) long_name = g_string_new ("{\"bm\":2,\"id\":\"a\",\"op\":\"action\",\"p\":{\"name\":\"");

  g_assert_cmpint (handle ("{\"bm\":2,\"id\":\"a\",\"op\":\"action\",\"p\":{\"name\":\"close\",\"params\":{\"why\":\"done\"}}}",
                           "https://acme.busymate.ai", NULL, &reply, &call), ==, BUSYMATE_ROUTE_ACTION);
  g_assert_cmpstr (call->name, ==, "close");
  g_assert_cmpstr (call->params_json, ==, "{\"why\":\"done\"}");
  for (int i = 0; i < 65; i++)
    g_string_append_c (long_name, 'x');
  g_string_append (long_name, "\"}}");
  g_assert_cmpint (handle (long_name->str, "https://acme.busymate.ai", NULL, &bad, &refused), ==, BUSYMATE_ROUTE_REPLY);
  g_assert_cmpstr (bad, ==, "{\"ok\":false,\"e\":\"bad_request\",\"bm\":2,\"id\":\"a\"}");
}

static void
test_events (void)
{
  g_autoptr(JsonObject) state = busymate_core_state ("com.acme.desktop", NULL);
  g_autofree char *resume = busymate_core_event ("resume", NULL);
  g_autofree char *changed = busymate_core_event ("state", state);

  g_assert_cmpstr (resume, ==, "{\"bm\":2,\"ev\":\"resume\",\"r\":{}}");
  g_assert_cmpstr (changed, ==, "{\"bm\":2,\"ev\":\"state\",\"r\":{\"signedIn\":false,\"account\":null}}");
}

static void
test_transport (void)
{
  g_autofree char *origin = NULL, *frame = NULL, *data = NULL;
  gboolean poll;

  g_assert_true (busymate_transport_parse ("{\"o\":\"https://acme.busymate.ai\",\"f\":\"abc\",\"d\":\"{}\"}",
                                           &origin, &frame, &data, &poll));
  g_assert_cmpstr (origin, ==, "https://acme.busymate.ai");
  g_assert_cmpstr (frame, ==, "abc");
  g_assert_cmpstr (data, ==, "{}");
  g_assert_false (poll);
  g_clear_pointer (&origin, g_free);
  g_clear_pointer (&frame, g_free);
  g_clear_pointer (&data, g_free);
  g_assert_true (busymate_transport_parse ("{\"o\":\"null\",\"f\":\"abc\",\"poll\":true}", &origin, &frame, &data, &poll));
  g_assert_true (poll);
  g_assert_null (data);
  g_clear_pointer (&origin, g_free);
  g_clear_pointer (&frame, g_free);
  g_assert_false (busymate_transport_parse ("{\"o\":\"x\",\"d\":\"{}\"}", &origin, &frame, &data, &poll));
  g_assert_false (busymate_transport_parse ("{\"o\":\"x\",\"f\":\"a\"}", &origin, &frame, &data, &poll));
  g_assert_false (busymate_transport_parse ("{\"o\":\"x\",\"f\":\"a\",\"poll\":\"yes\"}", &origin, &frame, &data, &poll));
  g_assert_false (busymate_transport_parse (NULL, &origin, &frame, &data, &poll));
}

static void
test_microphone_origins (void)
{
  const char *configured[] = { "https://Acme.busymate.ai:443", "https://acme.busymate.ai/", "https://*.acme.com",
                               "https://a.com?x=1", "https://u@a.com", "http://a.com", "https://b.com:8443",
                               "https://acme.busymate.ai", NULL };
  g_auto(GStrv) allowed = busymate_microphone_allowed (configured);

  g_assert_cmpuint (g_strv_length (allowed), ==, 2);
  g_assert_cmpstr (allowed[0], ==, "https://acme.busymate.ai");
  g_assert_cmpstr (allowed[1], ==, "https://b.com:8443");
}

static void
test_microphone_requests (void)
{
  const char * const allowed[] = { "https://acme.busymate.ai", NULL };
  g_autofree char *id = NULL, *source = NULL, *reply = NULL;
  g_autoptr(GString) big = g_string_new ("{\"type\":\"busymate.microphone.v1.request\",\"id\":\"m\",\"x\":\"");
  const char *bad[] = {
    "{\"type\":\"busymate.microphone.v2.request\",\"id\":\"m\"}",
    "{\"type\":\"busymate.microphone.v1.request\",\"id\":\"\"}",
    "{\"type\":\"busymate.microphone.v1.request\"}",
    "{\"type\":\"busymate.microphone.v1.request\",\"id\":5}",
    "not json",
  };

  g_assert_true (busymate_microphone_parse ("{\"type\":\"busymate.microphone.v1.request\",\"id\":\"mic-1\",\"source\":\"voice\"}",
                                            "https://acme.busymate.ai", allowed, &id, &source));
  g_assert_cmpstr (id, ==, "mic-1");
  g_assert_cmpstr (source, ==, "voice");
  g_clear_pointer (&id, g_free);
  g_assert_false (busymate_microphone_parse ("{\"type\":\"busymate.microphone.v1.request\",\"id\":\"mic-1\"}",
                                             "https://other.busymate.ai", allowed, &id, NULL));
  g_assert_false (busymate_microphone_parse ("{\"type\":\"busymate.microphone.v1.request\",\"id\":\"mic-1\"}",
                                             "null", allowed, &id, NULL));
  for (gsize i = 0; i < G_N_ELEMENTS (bad); i++)
    g_assert_false (busymate_microphone_parse (bad[i], "https://acme.busymate.ai", allowed, &id, NULL));
  while (big->len <= BUSYMATE_MAX_MICROPHONE)
    g_string_append (big, "xxxxxxxx");
  g_string_append (big, "\"}");
  g_assert_false (busymate_microphone_parse (big->str, "https://acme.busymate.ai", allowed, &id, NULL));

  reply = busymate_microphone_reply ("mic-1", TRUE);
  g_assert_cmpstr (reply, ==, "{\"id\":\"mic-1\",\"granted\":true}");
}

static void
test_microphone_media (void)
{
  const char * const allowed[] = { "https://acme.busymate.ai", NULL };
  const char *top = "https://acme.busymate.ai/chat?x=1";

  g_assert_true (busymate_microphone_media_allowed (allowed, top, TRUE, FALSE, FALSE, TRUE));
  g_assert_false (busymate_microphone_media_allowed (allowed, top, TRUE, FALSE, FALSE, FALSE)); /* no grant yet */
  g_assert_false (busymate_microphone_media_allowed (allowed, top, TRUE, TRUE, FALSE, TRUE));  /* camera too */
  g_assert_false (busymate_microphone_media_allowed (allowed, top, FALSE, TRUE, FALSE, TRUE)); /* camera */
  g_assert_false (busymate_microphone_media_allowed (allowed, top, FALSE, FALSE, TRUE, TRUE)); /* screen */
  g_assert_false (busymate_microphone_media_allowed (allowed, "https://evil.com/", TRUE, FALSE, FALSE, TRUE));
  g_assert_false (busymate_microphone_media_allowed (allowed, "http://acme.busymate.ai/", TRUE, FALSE, FALSE, TRUE));
  g_assert_false (busymate_microphone_media_allowed (allowed, NULL, TRUE, FALSE, FALSE, TRUE));
}

static void
test_scripts (void)
{
  const char * const origins[] = { "https://acme.busymate.ai", "https://b.com:8443", "https://b.com", NULL };
  g_auto(GStrv) allow = busymate_script_allow_list (origins);
  g_autofree char *isolated = busymate_script_isolated ("BusymateBridge", TRUE);
  g_autofree char *page = busymate_script_page ("BusymateMicrophone");

  g_assert_cmpuint (g_strv_length (allow), ==, 2);
  g_assert_cmpstr (allow[0], ==, "https://acme.busymate.ai/*");
  g_assert_cmpstr (allow[1], ==, "https://b.com/*");
  g_assert_nonnull (strstr (isolated, "var NAME = 'BusymateBridge', POLL = true;"));
  g_assert_nonnull (strstr (isolated, "location.origin"));
  g_assert_null (strstr (isolated, "@"));
  g_assert_nonnull (strstr (page, "var NAME = 'BusymateMicrophone';"));
  g_assert_null (strstr (page, "location.origin"));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/core/origin/normalize", test_normalize_origin);
  g_test_add_func ("/core/origin/allowed", test_allowed_origins);
  g_test_add_func ("/core/account-key", test_account_key);
  g_test_add_func ("/core/envelope/ignored", test_ignored);
  g_test_add_func ("/core/envelope/oversized", test_oversized_ignored);
  g_test_add_func ("/core/envelope/denied", test_denied_origins);
  g_test_add_func ("/core/op/hello", test_hello);
  g_test_add_func ("/core/op/unsupported", test_numeric_id_echoed);
  g_test_add_func ("/core/op/state", test_state);
  g_test_add_func ("/core/op/mint", test_mint);
  g_test_add_func ("/core/op/open", test_open);
  g_test_add_func ("/core/op/action", test_action);
  g_test_add_func ("/core/events", test_events);
  g_test_add_func ("/core/transport", test_transport);
  g_test_add_func ("/microphone/origins", test_microphone_origins);
  g_test_add_func ("/microphone/requests", test_microphone_requests);
  g_test_add_func ("/microphone/media", test_microphone_media);
  g_test_add_func ("/scripts", test_scripts);
  return g_test_run ();
}
