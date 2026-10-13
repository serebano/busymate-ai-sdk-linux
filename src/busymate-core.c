/* Busymate AI SDK for Linux — the WebKit-free pipe (`busymate.bridge/2`, build 2.0.0).
 *
 * FROZEN once released. This file moves one JSON envelope between the Busymate
 * chat (served from busymate.ai) and five fixed operations of YOUR app, with the
 * same bytes as the Android, iOS and JavaScript hosts. Every message name,
 * timeout, retry and decision lives in the chat. A change is a new SDK version.
 *
 * Contract: CONTRACT-v2.md (https://busymate.ai/sdk/v2/2.0.0/CONTRACT.md)
 */
#define BUSYMATE_COMPILATION
#include "busymate-core-private.h"

#include <string.h>

#define EXACT_ORIGIN "^https://[a-z0-9](?:[a-z0-9.-]{0,251}[a-z0-9])?(?::[0-9]{1,5})?$"
#define ASSISTANT_SLUG "^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"

static gboolean
matches (const char *pattern, const char *value)
{
  return value != NULL && g_regex_match_simple (pattern, value, G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT);
}

static char *
strip_suffix (char *value, const char *suffix)
{
  if (g_str_has_suffix (value, suffix))
    value[strlen (value) - strlen (suffix)] = '\0';
  return value;
}

/**
 * busymate_normalize_origin:
 * @origin: (nullable): an origin as a frame reports it, or as you configure it
 *
 * Normalizes to `https://host[:port]`: lower-case, one trailing `/` and the
 * default `:443` dropped. Anything else — `http:`, a path, a wildcard, the
 * opaque origin `null` of a sandboxed frame — is refused.
 *
 * Returns: (transfer full) (nullable): the exact origin, or %NULL
 */
char *
busymate_normalize_origin (const char *origin)
{
  g_autofree char *trimmed = NULL;
  char *value;

  if (origin == NULL)
    return NULL;
  trimmed = g_strstrip (g_strdup (origin));
  value = g_ascii_strdown (trimmed, -1);
  strip_suffix (value, "/");
  strip_suffix (value, ":443");
  if (!matches (EXACT_ORIGIN, value))
    {
      g_free (value);
      return NULL;
    }
  return value;
}

/**
 * busymate_allowed_origins:
 * @assistant: (nullable): your assistant slug
 * @origins: (nullable) (array zero-terminated=1): extra exact `https` origins
 *
 * The exact origins allowed to ask: `https://<assistant>.busymate.ai` plus your
 * valid exact origins, de-duplicated, in order. Invalid entries are ignored.
 *
 * Returns: (transfer full) (array zero-terminated=1): the allow-list
 */
char **
busymate_allowed_origins (const char *assistant, const char * const *origins)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
  g_autoptr(GPtrArray) seen = g_ptr_array_new_with_free_func (g_free);

  if (matches (ASSISTANT_SLUG, assistant))
    g_ptr_array_add (seen, g_strdup_printf ("https://%s.busymate.ai", assistant));
  for (gsize i = 0; origins != NULL && origins[i] != NULL; i++)
    {
      char *value = busymate_normalize_origin (origins[i]);
      gboolean duplicate = FALSE;

      if (value == NULL)
        continue;
      for (guint j = 0; j < seen->len; j++)
        duplicate |= g_str_equal (seen->pdata[j], value);
      if (duplicate)
        g_free (value);
      else
        g_ptr_array_add (seen, value);
    }
  for (guint j = 0; j < seen->len; j++)
    g_strv_builder_add (builder, seen->pdata[j]);
  return g_strv_builder_end (builder);
}

/**
 * busymate_origin_in_list:
 * @origin: (nullable): an exact origin
 * @allowed: (nullable) (array zero-terminated=1): an allow-list
 *
 * Returns: whether @origin is exactly one of @allowed
 */
gboolean
busymate_origin_in_list (const char *origin, const char * const *allowed)
{
  return origin != NULL && allowed != NULL && g_strv_contains (allowed, origin);
}

static char *
origin_from_uri (GUri *uri)
{
  const char *host = g_uri_get_host (uri);
  int port = g_uri_get_port (uri);
  g_autofree char *lower = NULL;

  if (g_strcmp0 (g_uri_get_scheme (uri), "https") != 0 || host == NULL || *host == '\0')
    return NULL;
  lower = g_ascii_strdown (host, -1);
  if (port == -1 || port == 443)
    return g_strdup_printf ("https://%s", lower);
  return g_strdup_printf ("https://%s:%d", lower, port);
}

/**
 * busymate_microphone_origin:
 * @origin: (nullable): a configured microphone origin
 *
 * The microphone adapter's stricter origin rule (identical to the Android and
 * iOS adapters): scheme + host + optional port only — no trailing slash, path,
 * credentials, wildcard, query or fragment. Hosts lower-case; `:443` dropped.
 *
 * Returns: (transfer full) (nullable): the exact origin, or %NULL
 */
char *
busymate_microphone_origin (const char *origin)
{
  g_autoptr(GUri) uri = NULL;

  if (origin == NULL || strchr (origin, '*') != NULL)
    return NULL;
  uri = g_uri_parse (origin, G_URI_FLAGS_NONE, NULL);
  if (uri == NULL || g_uri_get_userinfo (uri) != NULL || g_uri_get_query (uri) != NULL ||
      g_uri_get_fragment (uri) != NULL || *g_uri_get_path (uri) != '\0')
    return NULL;
  return origin_from_uri (uri);
}

/**
 * busymate_origin_of_uri:
 * @uri: (nullable): a document URI
 *
 * Returns: (transfer full) (nullable): `https://host[:port]` of an `https` URI, else %NULL
 */
char *
busymate_origin_of_uri (const char *uri)
{
  g_autoptr(GUri) parsed = NULL;

  if (uri == NULL)
    return NULL;
  parsed = g_uri_parse (uri, G_URI_FLAGS_NONE, NULL);
  return parsed != NULL ? origin_from_uri (parsed) : NULL;
}

/**
 * busymate_account_key:
 * @app_id: your application id
 * @account: the raw account id
 *
 * SHA-256(`app_id:account`), first 128 bits, lower-case hex. The raw id never
 * crosses to the page.
 *
 * Returns: (transfer full): 32 hex characters
 */
char *
busymate_account_key (const char *app_id, const char *account)
{
  g_autofree char *input = g_strconcat (app_id ? app_id : "", ":", account ? account : "", NULL);
  char *hex = g_compute_checksum_for_string (G_CHECKSUM_SHA256, input, -1);

  hex[32] = '\0';
  return hex;
}

/* ── envelopes ─────────────────────────────────────────────────────────── */

static char *
to_json (JsonNode *root)
{
  g_autoptr(JsonGenerator) generator = json_generator_new ();

  json_generator_set_root (generator, root);
  return json_generator_to_data (generator, NULL);
}

static char *
object_to_json (JsonObject *object)
{
  g_autoptr(JsonNode) node = json_node_new (JSON_NODE_OBJECT);

  json_node_set_object (node, object);
  return to_json (node);
}

static char *
finish_reply (JsonObject *body, JsonNode *id)
{
  json_object_set_int_member (body, "bm", BUSYMATE_BRIDGE_WIRE);
  json_object_set_member (body, "id", json_node_copy (id));
  return object_to_json (body);
}

char *
busymate_core_reply_ok (JsonNode *id, JsonObject *r)
{
  g_autoptr(JsonObject) body = json_object_new ();

  json_object_set_boolean_member (body, "ok", TRUE);
  json_object_set_object_member (body, "r", r != NULL ? json_object_ref (r) : json_object_new ());
  return finish_reply (body, id);
}

char *
busymate_core_reply_error (JsonNode *id, const char *error)
{
  g_autoptr(JsonObject) body = json_object_new ();

  json_object_set_boolean_member (body, "ok", FALSE);
  json_object_set_string_member (body, "e", error);
  return finish_reply (body, id);
}

char *
busymate_core_event (const char *ev, JsonObject *r)
{
  g_autoptr(JsonObject) body = json_object_new ();

  json_object_set_int_member (body, "bm", BUSYMATE_BRIDGE_WIRE);
  json_object_set_string_member (body, "ev", ev);
  json_object_set_object_member (body, "r", r != NULL ? json_object_ref (r) : json_object_new ());
  return object_to_json (body);
}

static void
set_nullable_string (JsonObject *object, const char *name, const char *value)
{
  if (value != NULL)
    json_object_set_string_member (object, name, value);
  else
    json_object_set_null_member (object, name);
}

JsonObject *
busymate_core_state (const char *app_id, const char *account)
{
  JsonObject *state = json_object_new ();

  json_object_set_boolean_member (state, "signedIn", account != NULL);
  if (account != NULL)
    {
      g_autofree char *key = busymate_account_key (app_id, account);
      json_object_set_string_member (state, "account", key);
    }
  else
    json_object_set_null_member (state, "account");
  return state;
}

JsonObject *
busymate_core_hello (const BusymateCoreConfig *config, const char *origin)
{
  static const char * const caps[] = {
    "hello", "state", "mint", "open", "action", "ev:resume", "ev:state", "ev:restored", NULL
  };
  JsonObject *hello = json_object_new ();
  JsonArray *origins = json_array_new ();
  JsonArray *capabilities = json_array_new ();
  JsonObject *app = json_object_new ();

  for (gsize i = 0; config->origins != NULL && config->origins[i] != NULL; i++)
    json_array_add_string_element (origins, config->origins[i]);
  for (gsize i = 0; caps[i] != NULL; i++)
    json_array_add_string_element (capabilities, caps[i]);
  set_nullable_string (app, "id", config->app_id);
  set_nullable_string (app, "version", config->app_version);
  set_nullable_string (app, "build", config->app_build);

  json_object_set_string_member (hello, "bridge", "busymate");
  json_object_set_int_member (hello, "v", BUSYMATE_BRIDGE_WIRE);
  json_object_set_string_member (hello, "build", BUSYMATE_BRIDGE_BUILD);
  json_object_set_string_member (hello, "platform", BUSYMATE_PLATFORM);
  json_object_set_string_member (hello, "transport", config->transport);
  json_object_set_string_member (hello, "assistant", config->assistant);
  json_object_set_string_member (hello, "origin", origin);
  json_object_set_array_member (hello, "origins", origins);
  json_object_set_array_member (hello, "caps", capabilities);
  set_nullable_string (hello, "webview", config->webview);
  json_object_set_object_member (hello, "app", app);
  return hello;
}

void
busymate_call_free (BusymateCall *call)
{
  if (call == NULL)
    return;
  g_clear_pointer (&call->id, json_node_unref);
  g_clear_pointer (&call->request, json_object_unref);
  g_free (call->op);
  g_free (call->origin);
  g_free (call->url);
  g_free (call->mode);
  g_free (call->name);
  g_free (call->params_json);
  g_free (call);
}

static JsonObject *
parse_object (const char *raw, gsize max)
{
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *root;

  if (raw == NULL || strlen (raw) > max)
    return NULL;
  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, raw, -1, NULL))
    return NULL;
  root = json_parser_get_root (parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    return NULL;
  return json_object_ref (json_node_get_object (root));
}

static gboolean
is_wire_version (JsonObject *envelope)
{
  JsonNode *bm = json_object_get_member (envelope, "bm");

  if (bm == NULL || !JSON_NODE_HOLDS_VALUE (bm))
    return FALSE;
  switch (json_node_get_value_type (bm))
    {
    case G_TYPE_INT64:
      return json_node_get_int (bm) == BUSYMATE_BRIDGE_WIRE;
    case G_TYPE_DOUBLE:
      return json_node_get_double (bm) == BUSYMATE_BRIDGE_WIRE;
    default:
      return FALSE;
    }
}

static const char *
string_member (JsonObject *object, const char *name)
{
  JsonNode *node = json_object_get_member (object, name);

  if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
    return NULL;
  return json_node_get_string (node);
}

static JsonObject *
object_member (JsonObject *object, const char *name)
{
  JsonNode *node = json_object_get_member (object, name);

  return node != NULL && JSON_NODE_HOLDS_OBJECT (node) ? json_node_get_object (node) : NULL;
}

static gboolean
is_https_url (const char *url)
{
  g_autoptr(GUri) uri = NULL;
  const char *host;

  if (url == NULL)
    return FALSE;
  uri = g_uri_parse (url, G_URI_FLAGS_NONE, NULL);
  if (uri == NULL || g_strcmp0 (g_uri_get_scheme (uri), "https") != 0)
    return FALSE;
  host = g_uri_get_host (uri);
  return host != NULL && *host != '\0';
}

/*
 * Handle ONE envelope from the page. `sender_origin` is the sending frame's
 * own origin, read in the isolated script world (never chosen by the page).
 */
BusymateRoute
busymate_core_handle (const BusymateCoreConfig *config,
                      const char               *raw,
                      const char               *sender_origin,
                      BusymateAccountFunc       account,
                      gpointer                  account_data,
                      char                    **reply,
                      BusymateCall            **out_call)
{
  g_autoptr(JsonObject) envelope = parse_object (raw, BUSYMATE_MAX_ENVELOPE);
  g_autoptr(BusymateCall) call = NULL;
  JsonNode *id;
  JsonObject *p;
  const char *op;

  *reply = NULL;
  *out_call = NULL;
  if (envelope == NULL || !is_wire_version (envelope))
    return BUSYMATE_ROUTE_IGNORE;
  id = json_object_get_member (envelope, "id");
  if (id == NULL || JSON_NODE_HOLDS_NULL (id))
    return BUSYMATE_ROUTE_IGNORE;

  call = g_new0 (BusymateCall, 1);
  call->id = json_node_copy (id);
  op = string_member (envelope, "op");
  call->op = g_strdup (op != NULL ? op : "");
  call->origin = busymate_normalize_origin (sender_origin);
  if (call->origin == NULL || !busymate_origin_in_list (call->origin, config->origins))
    {
      *reply = busymate_core_reply_error (call->id, "denied");
      *out_call = g_steal_pointer (&call);
      return BUSYMATE_ROUTE_REPLY;
    }
  p = object_member (envelope, "p");

  if (g_str_equal (call->op, "hello"))
    {
      g_autoptr(JsonObject) hello = busymate_core_hello (config, call->origin);
      *reply = busymate_core_reply_ok (call->id, hello);
    }
  else if (g_str_equal (call->op, "state"))
    {
      g_autofree char *current = account ? account (account_data) : NULL;
      g_autoptr(JsonObject) state = busymate_core_state (config->app_id, current);
      *reply = busymate_core_reply_ok (call->id, state);
    }
  else if (g_str_equal (call->op, "mint"))
    {
      g_autofree char *current = account ? account (account_data) : NULL;

      if (current == NULL)
        *reply = busymate_core_reply_error (call->id, "not_signed_in");
      else
        {
          GList *members = p != NULL ? json_object_get_members (p) : NULL;

          call->request = json_object_new ();
          for (GList *l = members; l != NULL; l = l->next)
            json_object_set_member (call->request, l->data, json_node_copy (json_object_get_member (p, l->data)));
          g_list_free (members);
          /* Who asked is stated by this file, never by the page. */
          json_object_set_string_member (call->request, "assistant", config->assistant);
          json_object_set_string_member (call->request, "origin", call->origin);
          *out_call = g_steal_pointer (&call);
          return BUSYMATE_ROUTE_MINT;
        }
    }
  else if (g_str_equal (call->op, "open"))
    {
      const char *url = p ? string_member (p, "url") : NULL;
      const char *mode = p ? string_member (p, "mode") : NULL;

      if (!is_https_url (url))
        *reply = busymate_core_reply_error (call->id, "bad_request");
      else
        {
          call->url = g_strdup (url);
          call->mode = g_strdup (g_strcmp0 (mode, "auth") == 0 ? "auth" : "external");
          *out_call = g_steal_pointer (&call);
          return BUSYMATE_ROUTE_OPEN;
        }
    }
  else if (g_str_equal (call->op, "action"))
    {
      const char *name = p ? string_member (p, "name") : NULL;

      if (name == NULL || *name == '\0' || g_utf8_strlen (name, -1) > 64)
        *reply = busymate_core_reply_error (call->id, "bad_request");
      else
        {
          JsonObject *params = object_member (p, "params");
          g_autoptr(JsonObject) empty = json_object_new ();

          call->name = g_strdup (name);
          call->params_json = object_to_json (params != NULL ? params : empty);
          *out_call = g_steal_pointer (&call);
          return BUSYMATE_ROUTE_ACTION;
        }
    }
  else
    *reply = busymate_core_reply_error (call->id, "unsupported");

  *out_call = g_steal_pointer (&call);
  return BUSYMATE_ROUTE_REPLY;
}

gboolean
busymate_transport_parse (const char *raw, char **origin, char **frame, char **data, gboolean *poll)
{
  /* The wrapper adds a few bytes around the envelope; the envelope bound is enforced later. */
  g_autoptr(JsonObject) wrapper = parse_object (raw, BUSYMATE_MAX_ENVELOPE * 2 + 512);
  const char *o, *f, *d;
  JsonNode *wants_poll;

  *origin = *frame = *data = NULL;
  *poll = FALSE;
  if (wrapper == NULL)
    return FALSE;
  o = string_member (wrapper, "o");
  f = string_member (wrapper, "f");
  d = string_member (wrapper, "d");
  wants_poll = json_object_get_member (wrapper, "poll");
  if (o == NULL || f == NULL || *f == '\0' || strlen (f) > 64)
    return FALSE;
  *poll = wants_poll != NULL && JSON_NODE_HOLDS_VALUE (wants_poll) &&
          json_node_get_value_type (wants_poll) == G_TYPE_BOOLEAN && json_node_get_boolean (wants_poll);
  if (!*poll && d == NULL)
    return FALSE;
  *origin = g_strdup (o);
  *frame = g_strdup (f);
  *data = g_strdup (d);
  return TRUE;
}

/* ── microphone wire 1 ─────────────────────────────────────────────────── */

char **
busymate_microphone_allowed (const char * const *origins)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
  g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (gsize i = 0; origins != NULL && origins[i] != NULL; i++)
    {
      char *value = busymate_microphone_origin (origins[i]);

      if (value == NULL)
        continue;
      if (g_hash_table_add (seen, value))
        g_strv_builder_add (builder, value);
    }
  return g_strv_builder_end (builder);
}

gboolean
busymate_microphone_parse (const char         *raw,
                           const char         *sender_origin,
                           const char * const *allowed,
                           char              **id,
                           char              **source)
{
  g_autoptr(JsonObject) request = parse_object (raw, BUSYMATE_MAX_MICROPHONE);
  g_autofree char *origin = busymate_microphone_origin (sender_origin);
  const char *value;

  *id = NULL;
  if (source != NULL)
    *source = NULL;
  if (request == NULL || !busymate_origin_in_list (origin, allowed) ||
      g_strcmp0 (string_member (request, "type"), BUSYMATE_MICROPHONE_REQUEST_TYPE) != 0)
    return FALSE;
  value = string_member (request, "id");
  if (value == NULL || *value == '\0')
    return FALSE;
  *id = g_strdup (value);
  if (source != NULL)
    *source = g_strdup (string_member (request, "source"));
  return TRUE;
}

char *
busymate_microphone_reply (const char *id, gboolean granted)
{
  g_autoptr(JsonObject) reply = json_object_new ();

  json_object_set_string_member (reply, "id", id);
  json_object_set_boolean_member (reply, "granted", granted);
  return object_to_json (reply);
}

/*
 * WebKit's media decision: only audio (never camera or screen), only after the
 * adapter's permission step granted, only while the top-level document is an
 * allowed origin. WebKitGTK does not report the requesting frame's origin for
 * user-media requests; a cross-origin frame can only ask when the (allowed)
 * top document delegates `microphone` through its iframe Permissions Policy.
 */
gboolean
busymate_microphone_media_allowed (const char * const *allowed,
                                   const char         *top_uri,
                                   gboolean            audio,
                                   gboolean            video,
                                   gboolean            display,
                                   gboolean            granted)
{
  g_autofree char *origin = busymate_origin_of_uri (top_uri);

  return audio && !video && !display && granted && busymate_origin_in_list (origin, allowed);
}
