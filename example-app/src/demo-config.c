/* Example app settings, validation and the authenticated-backend mint client.
 */
#include "demo-config.h"

#include <json-glib/json-glib.h>
#include <string.h>

G_DEFINE_QUARK (demo-config-error-quark, demo_config_error)

DemoConfig *
demo_config_new_default (void)
{
  DemoConfig *config = g_new0 (DemoConfig, 1);

  config->assistant = g_strdup (DEMO_DEFAULT_ASSISTANT);
  config->chat_url = g_strdup (DEMO_DEFAULT_CHAT_URL);
  config->locale = g_strdup (DEMO_DEFAULT_LOCALE);
  config->origins = g_strdup (DEMO_DEFAULT_ORIGINS);
  config->account_id = g_strdup ("");
  config->mint_url = g_strdup ("");
  config->backend_session_token = g_strdup ("");
  config->microphone_enabled = TRUE;
  config->close_callback_enabled = TRUE;
  return config;
}

DemoConfig *
demo_config_copy (const DemoConfig *config)
{
  DemoConfig *copy = g_new0 (DemoConfig, 1);

  *copy = *config;
  copy->assistant = g_strdup (config->assistant);
  copy->chat_url = g_strdup (config->chat_url);
  copy->locale = g_strdup (config->locale);
  copy->origins = g_strdup (config->origins);
  copy->account_id = g_strdup (config->account_id);
  copy->mint_url = g_strdup (config->mint_url);
  copy->backend_session_token = g_strdup (config->backend_session_token);
  return copy;
}

void
demo_config_free (DemoConfig *config)
{
  if (config == NULL)
    return;
  g_free (config->assistant);
  g_free (config->chat_url);
  g_free (config->locale);
  g_free (config->origins);
  g_free (config->account_id);
  g_free (config->mint_url);
  if (config->backend_session_token != NULL)
    memset (config->backend_session_token, 0, strlen (config->backend_session_token));
  g_free (config->backend_session_token);
  g_free (config);
}

void
demo_validated_free (DemoValidated *validated)
{
  if (validated == NULL)
    return;
  g_free (validated->assistant);
  g_free (validated->chat_url);
  g_strfreev (validated->origins);
  g_free (validated->mint_url);
  g_free (validated);
}

static GUri *
secure_uri (const char *raw)
{
  g_autofree char *trimmed = NULL;
  GUri *uri;

  if (raw == NULL)
    return NULL;
  trimmed = g_strstrip (g_strdup (raw));
  uri = g_uri_parse (trimmed, G_URI_FLAGS_NONE, NULL);
  if (uri == NULL)
    return NULL;
  if (g_strcmp0 (g_uri_get_scheme (uri), "https") != 0 || g_uri_get_host (uri) == NULL ||
      *g_uri_get_host (uri) == '\0' || g_uri_get_userinfo (uri) != NULL)
    {
      g_uri_unref (uri);
      return NULL;
    }
  return uri;
}

static char *
origin_of (GUri *uri)
{
  g_autofree char *host = g_ascii_strdown (g_uri_get_host (uri), -1);
  int port = g_uri_get_port (uri);

  return port == -1 || port == 443 ? g_strdup_printf ("https://%s", host)
                                   : g_strdup_printf ("https://%s:%d", host, port);
}

char *
demo_secure_url_origin (const char *url)
{
  g_autoptr(GUri) uri = secure_uri (url);

  return uri != NULL ? origin_of (uri) : NULL;
}

/* Sets (or replaces) the `locale` query parameter, keeping every other parameter in order. */
char *
demo_url_with_locale (const char *url, const char *locale)
{
  g_autoptr(GUri) uri = g_uri_parse (url, G_URI_FLAGS_ENCODED, NULL);
  g_autoptr(GString) query = g_string_new (NULL);
  g_auto(GStrv) parts = NULL;
  gboolean replaced = FALSE;
  g_autoptr(GUri) result = NULL;

  if (uri == NULL)
    return NULL;
  if (locale == NULL || *locale == '\0')
    return g_strdup (url);
  parts = g_uri_get_query (uri) ? g_strsplit (g_uri_get_query (uri), "&", -1) : g_new0 (char *, 1);
  for (gsize i = 0; parts[i] != NULL; i++)
    {
      if (*parts[i] == '\0')
        continue;
      if (query->len > 0)
        g_string_append_c (query, '&');
      if (g_str_equal (parts[i], "locale") || g_str_has_prefix (parts[i], "locale="))
        {
          if (replaced)
            {
              g_string_truncate (query, query->len > 0 ? query->len - 1 : 0);
              continue;
            }
          g_string_append_printf (query, "locale=%s", locale);
          replaced = TRUE;
        }
      else
        g_string_append (query, parts[i]);
    }
  if (!replaced)
    g_string_append_printf (query, "%slocale=%s", query->len > 0 ? "&" : "", locale);
  result = g_uri_build (G_URI_FLAGS_ENCODED, g_uri_get_scheme (uri), g_uri_get_userinfo (uri), g_uri_get_host (uri),
                        g_uri_get_port (uri), g_uri_get_path (uri), query->str, g_uri_get_fragment (uri));
  return g_uri_to_string (result);
}

static gboolean
is_locale (const char *locale)
{
  return locale == NULL || *locale == '\0' ||
         g_regex_match_simple ("^[a-z]{2,3}(-[A-Za-z0-9]{2,8})*$", locale, G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT);
}

static gboolean
blank (const char *value)
{
  g_autofree char *trimmed = value ? g_strstrip (g_strdup (value)) : NULL;

  return trimmed == NULL || *trimmed == '\0';
}

DemoValidated *
demo_config_validate (const DemoConfig *config, GError **error)
{
  g_autoptr(GUri) chat = NULL;
  g_autoptr(GPtrArray) origins = g_ptr_array_new_with_free_func (g_free);
  g_auto(GStrv) entries = NULL;
  g_autofree char *chat_origin = NULL;
  g_autoptr(GUri) backend = NULL;
  DemoValidated *validated;

  if (config->assistant == NULL ||
      !g_regex_match_simple ("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$", config->assistant, G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT))
    {
      g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_ASSISTANT, "Use a valid assistant slug.");
      return NULL;
    }
  chat = secure_uri (config->chat_url);
  if (chat == NULL || g_uri_get_fragment (chat) != NULL)
    {
      g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_CHAT_URL,
                           "Chat URL must use HTTPS with no embedded credentials or fragment.");
      return NULL;
    }
  if (!is_locale (config->locale))
    {
      g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_LOCALE, "Use a locale such as en, de or pt-BR.");
      return NULL;
    }
  entries = g_regex_split_simple ("[,\\n]", config->origins ? config->origins : "", G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT);
  for (gsize i = 0; entries[i] != NULL; i++)
    {
      g_autoptr(GUri) candidate = NULL;
      char *value;
      gboolean seen = FALSE;

      if (blank (entries[i]))
        continue;
      candidate = strchr (entries[i], '*') ? NULL : secure_uri (entries[i]);
      if (candidate == NULL || *g_uri_get_path (candidate) != '\0' || g_uri_get_query (candidate) != NULL ||
          g_uri_get_fragment (candidate) != NULL)
        {
          g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_ORIGIN,
                               "Origins must be exact HTTPS origins, without paths or wildcards.");
          return NULL;
        }
      value = origin_of (candidate);
      for (guint j = 0; j < origins->len; j++)
        seen |= g_str_equal (origins->pdata[j], value);
      if (seen)
        g_free (value);
      else
        g_ptr_array_add (origins, value);
    }
  chat_origin = origin_of (chat);
  {
    gboolean present = FALSE;
    for (guint j = 0; j < origins->len; j++)
      present |= g_str_equal (origins->pdata[j], chat_origin);
    if (!present)
      {
        g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_MISSING_ORIGIN,
                             "Include the actual chat origin in the allowed origins.");
        return NULL;
      }
  }
  backend = blank (config->mint_url) ? NULL : secure_uri (config->mint_url);
  if (config->signed_in)
    {
      if (blank (config->account_id))
        {
          g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_ACCOUNT,
                               "Identified mode requires your actual signed-in account ID. This is not a login.");
          return NULL;
        }
      if (backend == NULL)
        {
          g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_BACKEND,
                               "Identified mode requires your authenticated HTTPS mint endpoint.");
          return NULL;
        }
    }
  else if (!blank (config->mint_url) && backend == NULL)
    {
      g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_BACKEND,
                           "The mint endpoint must be an HTTPS URL without credentials.");
      return NULL;
    }
  if (config->backend_session_token != NULL &&
      (strchr (config->backend_session_token, '\n') != NULL || strchr (config->backend_session_token, '\r') != NULL))
    {
      g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_BACKEND,
                           "The backend session token cannot contain line breaks.");
      return NULL;
    }

  validated = g_new0 (DemoValidated, 1);
  validated->assistant = g_strdup (config->assistant);
  {
    g_autofree char *url = g_strstrip (g_strdup (config->chat_url));
    validated->chat_url = demo_url_with_locale (url, config->locale);
  }
  g_ptr_array_add (origins, NULL);
  validated->origins = (char **) g_ptr_array_steal (origins, NULL);
  validated->mint_url = backend != NULL ? g_uri_to_string (backend) : NULL;
  return validated;
}

DemoCallbacks
demo_callbacks_for (const DemoConfig *config)
{
  DemoCallbacks callbacks = {
    .connect_action = config->handle_close_action,
    .connect_close = config->close_callback_enabled,
  };
  return callbacks;
}

gboolean
demo_action_handles (const char *name)
{
  /* Only `close` is handled; `ready` is informational and unknown actions stay unhandled. */
  return g_strcmp0 (name, "close") == 0;
}

/* ── mint ──────────────────────────────────────────────────────────────── */

char *
demo_mint_body (const char *nonce, const char *aud, const char *reason, const char *assistant, const char *origin)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();
  g_autoptr(JsonNode) root = NULL;

  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "nonce");
  json_builder_add_string_value (builder, nonce);
  if (aud != NULL)
    {
      json_builder_set_member_name (builder, "aud");
      json_builder_add_string_value (builder, aud);
    }
  if (reason != NULL)
    {
      json_builder_set_member_name (builder, "reason");
      json_builder_add_string_value (builder, reason);
    }
  json_builder_set_member_name (builder, "assistant");
  json_builder_add_string_value (builder, assistant);
  json_builder_set_member_name (builder, "origin");
  json_builder_add_string_value (builder, origin);
  json_builder_end_object (builder);
  root = json_builder_get_root (builder);
  return json_to_string (root, FALSE);
}

DemoMintOutcome
demo_mint_decode (guint status, const char *body, gsize length, const char *nonce, char **token)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  JsonNode *root;
  JsonObject *object;
  JsonNode *token_node, *nonce_node;

  *token = NULL;
  if (status == 401 || status == 403)
    return DEMO_MINT_NOT_SIGNED_IN;
  if (status < 200 || status >= 300 || body == NULL || nonce == NULL ||
      !json_parser_load_from_data (parser, body, length, NULL))
    return DEMO_MINT_FAILED;
  root = json_parser_get_root (parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    return DEMO_MINT_FAILED;
  object = json_node_get_object (root);
  token_node = json_object_get_member (object, "token");
  nonce_node = json_object_get_member (object, "nonce");
  if (token_node == NULL || nonce_node == NULL ||
      !JSON_NODE_HOLDS_VALUE (token_node) || json_node_get_value_type (token_node) != G_TYPE_STRING ||
      !JSON_NODE_HOLDS_VALUE (nonce_node) || json_node_get_value_type (nonce_node) != G_TYPE_STRING ||
      *json_node_get_string (token_node) == '\0' || g_strcmp0 (json_node_get_string (nonce_node), nonce) != 0)
    return DEMO_MINT_FAILED;
  *token = g_strdup (json_node_get_string (token_node));
  return DEMO_MINT_TOKEN;
}

SoupMessage *
demo_mint_message (const char *url, const char *body, const char *session_token, GError **error)
{
  g_autoptr(GBytes) bytes = g_bytes_new (body, strlen (body));
  SoupMessage *message = soup_message_new ("POST", url);
  SoupMessageHeaders *headers;

  if (message == NULL)
    {
      g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_BACKEND, "Invalid mint endpoint.");
      return NULL;
    }
  if (session_token != NULL && (strchr (session_token, '\n') || strchr (session_token, '\r')))
    {
      g_object_unref (message);
      g_set_error_literal (error, DEMO_CONFIG_ERROR, DEMO_CONFIG_ERROR_BACKEND, "Invalid backend session token.");
      return NULL;
    }
  /* Authentication is never forwarded to another endpoint or scheme. */
  soup_message_add_flags (message, SOUP_MESSAGE_NO_REDIRECT);
  headers = soup_message_get_request_headers (message);
  soup_message_headers_replace (headers, "Cache-Control", "no-store");
  soup_message_headers_replace (headers, "Accept", "application/json");
  if (session_token != NULL && *session_token != '\0')
    {
      g_autofree char *bearer = g_strconcat ("Bearer ", session_token, NULL);
      soup_message_headers_replace (headers, "Authorization", bearer);
    }
  soup_message_set_request_body_from_bytes (message, "application/json", bytes);
  return message;
}

typedef struct {
  SoupMessage *message;
  char        *nonce;
} MintTask;

static void
mint_task_free (MintTask *data)
{
  g_clear_object (&data->message);
  g_free (data->nonce);
  g_free (data);
}

static void
on_mint_read (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(GTask) task = user_data;
  MintTask *data = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) bytes = soup_session_send_and_read_finish (SOUP_SESSION (source), result, &error);
  g_autofree char *token = NULL;
  gsize length = 0;
  const char *body;
  DemoMintOutcome outcome;

  if (bytes == NULL)
    {
      g_task_return_int (task, DEMO_MINT_FAILED);
      return;
    }
  body = g_bytes_get_data (bytes, &length);
  outcome = demo_mint_decode (soup_message_get_status (data->message), body, length, data->nonce, &token);
  g_object_set_data_full (G_OBJECT (task), "token", g_steal_pointer (&token), g_free);
  g_task_return_int (task, outcome);
}

void
demo_mint_send (SoupSession *session, const char *url, const char *body, const char *session_token,
                const char *nonce, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  GTask *task = g_task_new (NULL, cancellable, callback, user_data);
  SoupMessage *message = demo_mint_message (url, body, session_token, &error);
  MintTask *data;

  if (message == NULL)
    {
      g_task_return_int (task, DEMO_MINT_FAILED);
      g_object_unref (task);
      return;
    }
  data = g_new0 (MintTask, 1);
  data->message = message;
  data->nonce = g_strdup (nonce);
  g_task_set_task_data (task, data, (GDestroyNotify) mint_task_free);
  soup_session_send_and_read_async (session, message, G_PRIORITY_DEFAULT, cancellable, on_mint_read, task);
}

DemoMintOutcome
demo_mint_send_finish (GAsyncResult *result, char **token)
{
  DemoMintOutcome outcome = g_task_propagate_int (G_TASK (result), NULL);

  *token = outcome == DEMO_MINT_TOKEN ? g_strdup (g_object_get_data (G_OBJECT (result), "token")) : NULL;
  return outcome;
}
