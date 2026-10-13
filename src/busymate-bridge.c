/* Busymate AI SDK for Linux — identity bridge v2 for WebKitGTK 6.0
 * (`busymate.bridge/2`, build 2.0.0).
 *
 * FROZEN once released. A pipe, not a policy: it moves one JSON envelope between
 * the Busymate chat (served from busymate.ai) and five fixed operations of YOUR
 * app. Every message name, timeout, retry and decision lives in the chat, which
 * Busymate updates on its side, so a fix never needs a new build of your app.
 *
 * Who may ask: EXACTLY https://<assistant>.busymate.ai plus the exact https
 * origins you list. No wildcards: another tenant's chat, or any other
 * busymate.ai page, is refused with `denied` and never reaches your mint. The
 * sender is the posting frame's own origin, read in an isolated script world
 * the page cannot touch (see busymate-scripts.c).
 *
 * Use (before the first load):
 *
 *   const char *origins[] = { "https://support.acme.com", NULL };
 *   BusymateBridge *bridge = busymate_bridge_new ("acme", origins);
 *   g_signal_connect (bridge, "get-account", G_CALLBACK (current_account_or_null), app);
 *   g_signal_connect (bridge, "mint", G_CALLBACK (ask_your_backend), app);
 *   busymate_bridge_install (bridge, web_view);
 *   webkit_web_view_load_uri (web_view, chat_url);
 *   // optional, after sign-in, sign-out or an account switch:
 *   busymate_bridge_account_changed (bridge);
 *
 * Contract: CONTRACT-v2.md (https://busymate.ai/sdk/v2/2.0.0/CONTRACT.md)
 */
#define BUSYMATE_COMPILATION
#include "busymate/busymate-bridge.h"
#include "busymate-core-private.h"

#include <gtk/gtk.h>

#define MAX_QUEUED_EVENTS 16

/* ── BusymateMintRequest ────────────────────────────────────────────────── */

struct _BusymateMintRequest {
  GObject                   parent_instance;
  BusymateBridge           *bridge;
  BusymateCall             *call;
  WebKitScriptMessageReply *reply;
  guint                     ceiling_id;
  gboolean                  finished;
};

G_DEFINE_FINAL_TYPE (BusymateMintRequest, busymate_mint_request, G_TYPE_OBJECT)

/* ── BusymateBridge ─────────────────────────────────────────────────────── */

struct _BusymateBridge {
  GObject                   parent_instance;

  char                     *assistant;
  char                    **origins;
  char                    **allowed;
  char                     *app_id;
  char                     *app_version;
  char                     *app_build;

  JSCContext               *js;
  WebKitWebView            *web_view;
  WebKitUserContentManager *manager;
  WebKitUserScript         *isolated_script;
  WebKitUserScript         *page_script;
  GtkWindow                *window;
  gulong                    message_id;
  gulong                    terminated_id;
  gulong                    load_id;
  gulong                    map_id;
  gulong                    active_id;
  gboolean                  window_active;

  char                     *hello_frame;
  WebKitScriptMessageReply *poll;
  GQueue                    events;
  GPtrArray                *mints;
  gboolean                  pending_restored;
  char                     *last_uri;
  guint                     reload_id;
};

G_DEFINE_FINAL_TYPE (BusymateBridge, busymate_bridge, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_ASSISTANT,
  PROP_ORIGINS,
  PROP_ALLOWED_ORIGINS,
  PROP_APP_ID,
  PROP_APP_VERSION,
  PROP_APP_BUILD,
  N_PROPS
};

enum {
  SIGNAL_GET_ACCOUNT,
  SIGNAL_MINT,
  SIGNAL_OPEN_URI,
  SIGNAL_ACTION,
  SIGNAL_CLOSE,
  SIGNAL_EVENT_SENT,
  SIGNAL_REQUEST_RECEIVED,
  SIGNAL_WEB_PROCESS_TERMINATED,
  N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static void
reply_string (JSCContext *js, WebKitScriptMessageReply *reply, const char *text)
{
  g_autoptr(JSCValue) value = jsc_value_new_string (js, text != NULL ? text : "");

  webkit_script_message_reply_return_value (reply, value);
}

static void
release_poll (BusymateBridge *self)
{
  if (self->poll == NULL)
    return;
  reply_string (self->js, self->poll, "");
  g_clear_pointer (&self->poll, webkit_script_message_reply_unref);
}

static void
forget_frame (BusymateBridge *self)
{
  release_poll (self);
  g_queue_clear_full (&self->events, g_free);
  g_clear_pointer (&self->hello_frame, g_free);
}

/* ── mint ──────────────────────────────────────────────────────────────── */

static void
mint_finish (BusymateMintRequest *request, char *reply_json)
{
  g_autofree char *owned = reply_json;

  if (request->finished)
    return;
  request->finished = TRUE;
  g_clear_handle_id (&request->ceiling_id, g_source_remove);
  if (request->reply != NULL && request->bridge != NULL)
    reply_string (request->bridge->js, request->reply, owned);
  g_clear_pointer (&request->reply, webkit_script_message_reply_unref);
  if (request->bridge != NULL)
    g_ptr_array_remove (request->bridge->mints, request);
}

static gboolean
mint_ceiling (gpointer data)
{
  BusymateMintRequest *request = data;

  request->ceiling_id = 0;
  /* The ceiling only reclaims the callback; the chat owns every real budget. */
  mint_finish (request, busymate_core_reply_error (request->call->id, "timeout"));
  return G_SOURCE_REMOVE;
}

static void
busymate_mint_request_finalize (GObject *object)
{
  BusymateMintRequest *self = BUSYMATE_MINT_REQUEST (object);

  g_clear_handle_id (&self->ceiling_id, g_source_remove);
  g_clear_pointer (&self->reply, webkit_script_message_reply_unref);
  g_clear_pointer (&self->call, busymate_call_free);
  g_clear_weak_pointer (&self->bridge);
  G_OBJECT_CLASS (busymate_mint_request_parent_class)->finalize (object);
}

static void
busymate_mint_request_class_init (BusymateMintRequestClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = busymate_mint_request_finalize;
}

static void
busymate_mint_request_init (BusymateMintRequest *self)
{
}

static const char *
request_string (BusymateMintRequest *self, const char *name)
{
  JsonNode *node;

  g_return_val_if_fail (BUSYMATE_IS_MINT_REQUEST (self), NULL);
  node = json_object_get_member (self->call->request, name);
  if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
    return NULL;
  return json_node_get_string (node);
}

/**
 * busymate_mint_request_get_nonce:
 * @self: a #BusymateMintRequest
 *
 * Returns: (nullable): the nonce your backend must sign into a fresh token
 */
const char *
busymate_mint_request_get_nonce (BusymateMintRequest *self)
{
  return request_string (self, "nonce");
}

/**
 * busymate_mint_request_get_audience:
 * @self: a #BusymateMintRequest
 *
 * Returns: (nullable): the chat's `aud`
 */
const char *
busymate_mint_request_get_audience (BusymateMintRequest *self)
{
  return request_string (self, "aud");
}

/**
 * busymate_mint_request_get_reason:
 * @self: a #BusymateMintRequest
 *
 * Returns: (nullable): the chat's `reason`
 */
const char *
busymate_mint_request_get_reason (BusymateMintRequest *self)
{
  return request_string (self, "reason");
}

/**
 * busymate_mint_request_get_assistant:
 * @self: a #BusymateMintRequest
 *
 * Returns: the configured assistant slug (set by the bridge, never by the page)
 */
const char *
busymate_mint_request_get_assistant (BusymateMintRequest *self)
{
  return request_string (self, "assistant");
}

/**
 * busymate_mint_request_get_origin:
 * @self: a #BusymateMintRequest
 *
 * Returns: the asking frame's exact origin (set by the bridge, never by the page)
 */
const char *
busymate_mint_request_get_origin (BusymateMintRequest *self)
{
  return request_string (self, "origin");
}

/**
 * busymate_mint_request_to_json:
 * @self: a #BusymateMintRequest
 *
 * The whole request as JSON: the chat's fields plus `assistant` and `origin`.
 *
 * Returns: (transfer full): a JSON object
 */
char *
busymate_mint_request_to_json (BusymateMintRequest *self)
{
  g_autoptr(JsonNode) node = json_node_new (JSON_NODE_OBJECT);
  g_autoptr(JsonGenerator) generator = json_generator_new ();

  g_return_val_if_fail (BUSYMATE_IS_MINT_REQUEST (self), NULL);
  json_node_set_object (node, self->call->request);
  json_generator_set_root (generator, node);
  return json_generator_to_data (generator, NULL);
}

/**
 * busymate_mint_request_return_token:
 * @self: a #BusymateMintRequest
 * @token: a fresh single-use launch token from your backend's signer
 * @nonce: the nonce it signed
 *
 * Answers the chat. Call once, on the main thread; later answers are ignored.
 */
void
busymate_mint_request_return_token (BusymateMintRequest *self, const char *token, const char *nonce)
{
  g_autoptr(JsonObject) r = NULL;

  g_return_if_fail (BUSYMATE_IS_MINT_REQUEST (self));
  if (token == NULL || nonce == NULL)
    {
      busymate_mint_request_return_failed (self);
      return;
    }
  r = json_object_new ();
  json_object_set_string_member (r, "token", token);
  json_object_set_string_member (r, "nonce", nonce);
  mint_finish (self, busymate_core_reply_ok (self->call->id, r));
}

/**
 * busymate_mint_request_return_not_signed_in:
 * @self: a #BusymateMintRequest
 *
 * Answers `not_signed_in`: the chat stays a guest.
 */
void
busymate_mint_request_return_not_signed_in (BusymateMintRequest *self)
{
  g_return_if_fail (BUSYMATE_IS_MINT_REQUEST (self));
  mint_finish (self, busymate_core_reply_error (self->call->id, "not_signed_in"));
}

/**
 * busymate_mint_request_return_failed:
 * @self: a #BusymateMintRequest
 *
 * Answers `mint_failed`: the chat stays a guest.
 */
void
busymate_mint_request_return_failed (BusymateMintRequest *self)
{
  g_return_if_fail (BUSYMATE_IS_MINT_REQUEST (self));
  mint_finish (self, busymate_core_reply_error (self->call->id, "mint_failed"));
}

/**
 * busymate_mint_request_is_finished:
 * @self: a #BusymateMintRequest
 *
 * Returns: whether the chat already got its answer (or the 15 s ceiling passed)
 */
gboolean
busymate_mint_request_is_finished (BusymateMintRequest *self)
{
  g_return_val_if_fail (BUSYMATE_IS_MINT_REQUEST (self), TRUE);
  return self->finished;
}

/* ── the pipe ──────────────────────────────────────────────────────────── */

static char *
current_account (gpointer data)
{
  BusymateBridge *self = data;
  char *account = NULL;

  g_signal_emit (self, signals[SIGNAL_GET_ACCOUNT], 0, &account);
  return account;
}

static void
config_of (BusymateBridge *self, BusymateCoreConfig *config, char **webview)
{
  *webview = g_strdup_printf ("WebKitGTK/%u.%u.%u", webkit_get_major_version (),
                              webkit_get_minor_version (), webkit_get_micro_version ());
  config->assistant = self->assistant;
  config->origins = (const char * const *) self->allowed;
  config->app_id = self->app_id;
  config->app_version = self->app_version;
  config->app_build = self->app_build;
  config->webview = *webview;
  config->transport = "message";
}

static void
deliver_event (BusymateBridge *self, char *envelope)
{
  if (self->poll != NULL)
    {
      reply_string (self->js, self->poll, envelope);
      g_clear_pointer (&self->poll, webkit_script_message_reply_unref);
      g_free (envelope);
      return;
    }
  g_queue_push_tail (&self->events, envelope);
  while (g_queue_get_length (&self->events) > MAX_QUEUED_EVENTS)
    g_free (g_queue_pop_head (&self->events));
}

static void
emit_event (BusymateBridge *self, const char *ev)
{
  g_autoptr(JsonObject) r = NULL;

  /* Events go to the frame that said hello; before any hello the chat re-reads on its own. */
  if (self->hello_frame == NULL)
    return;
  if (g_str_equal (ev, "state"))
    {
      g_autofree char *account = current_account (self);
      r = busymate_core_state (self->app_id, account);
    }
  deliver_event (self, busymate_core_event (ev, r));
  g_signal_emit (self, signals[SIGNAL_EVENT_SENT], 0, ev);
}

static void
on_poll (BusymateBridge *self, const char *frame, WebKitScriptMessageReply *reply)
{
  if (self->hello_frame == NULL || !g_str_equal (frame, self->hello_frame))
    {
      reply_string (self->js, reply, "");
      return;
    }
  if (!g_queue_is_empty (&self->events))
    {
      g_autofree char *envelope = g_queue_pop_head (&self->events);
      reply_string (self->js, reply, envelope);
      return;
    }
  release_poll (self);
  self->poll = webkit_script_message_reply_ref (reply);
}

static GtkWindow *
window_of (BusymateBridge *self)
{
  GtkRoot *root = self->web_view ? gtk_widget_get_root (GTK_WIDGET (self->web_view)) : NULL;

  return GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL;
}

typedef struct {
  BusymateBridge           *bridge;
  WebKitScriptMessageReply *reply;
  JsonNode                 *id;
} OpenTask;

static void
open_task_free (OpenTask *task)
{
  g_clear_object (&task->bridge);
  g_clear_pointer (&task->reply, webkit_script_message_reply_unref);
  g_clear_pointer (&task->id, json_node_unref);
  g_free (task);
}

static void
on_launched (GObject *source, GAsyncResult *result, gpointer data)
{
  OpenTask *task = data;
  g_autoptr(GError) error = NULL;
  g_autofree char *reply = NULL;

  if (gtk_uri_launcher_launch_finish (GTK_URI_LAUNCHER (source), result, &error))
    {
      g_autoptr(JsonObject) r = json_object_new ();
      json_object_set_boolean_member (r, "opened", TRUE);
      reply = busymate_core_reply_ok (task->id, r);
    }
  else
    reply = busymate_core_reply_error (task->id, "unsupported");
  reply_string (task->bridge->js, task->reply, reply);
  open_task_free (task);
}

static char *
handle_open (BusymateBridge *self, BusymateCall *call, WebKitScriptMessageReply *reply)
{
  gboolean handled = FALSE;
  g_autoptr(GtkUriLauncher) launcher = NULL;
  OpenTask *task;

  g_signal_emit (self, signals[SIGNAL_OPEN_URI], 0, call->url, call->mode, &handled);
  if (handled)
    {
      g_autoptr(JsonObject) r = json_object_new ();
      json_object_set_boolean_member (r, "opened", TRUE);
      return busymate_core_reply_ok (call->id, r);
    }
  task = g_new0 (OpenTask, 1);
  task->bridge = g_object_ref (self);
  task->reply = webkit_script_message_reply_ref (reply);
  task->id = json_node_copy (call->id);
  launcher = gtk_uri_launcher_new (call->url);
  gtk_uri_launcher_launch (launcher, window_of (self), NULL, on_launched, task);
  return NULL;
}

static char *
handle_action (BusymateBridge *self, BusymateCall *call)
{
  gboolean handled = FALSE;
  g_autoptr(JsonObject) r = json_object_new ();

  g_signal_emit (self, signals[SIGNAL_ACTION], g_quark_from_string (call->name), call->name, call->params_json, &handled);
  if (!handled && g_str_equal (call->name, "close") &&
      g_signal_has_handler_pending (self, signals[SIGNAL_CLOSE], 0, FALSE))
    {
      g_signal_emit (self, signals[SIGNAL_CLOSE], 0);
      handled = TRUE;
    }
  json_object_set_boolean_member (r, "handled", handled);
  return busymate_core_reply_ok (call->id, r);
}

static void
handle_mint (BusymateBridge *self, BusymateCall *call, WebKitScriptMessageReply *reply)
{
  g_autoptr(BusymateMintRequest) request = g_object_new (BUSYMATE_TYPE_MINT_REQUEST, NULL);
  gboolean handled = FALSE;

  g_set_weak_pointer (&request->bridge, self);
  request->call = call;
  request->reply = webkit_script_message_reply_ref (reply);
  g_ptr_array_add (self->mints, g_object_ref (request));
  request->ceiling_id = g_timeout_add (BUSYMATE_MINT_CEILING_MS, mint_ceiling, request);
  g_signal_emit (self, signals[SIGNAL_MINT], 0, request, &handled);
  if (!handled)
    busymate_mint_request_return_failed (request);
}

static gboolean
on_message (WebKitUserContentManager *manager,
            JSCValue                 *value,
            WebKitScriptMessageReply *reply,
            BusymateBridge           *self)
{
  g_autofree char *raw = jsc_value_is_string (value) ? jsc_value_to_string (value) : NULL;
  g_autofree char *origin = NULL, *frame = NULL, *data = NULL, *webview = NULL, *answer = NULL;
  g_autoptr(BusymateCall) call = NULL;
  BusymateCoreConfig config;
  gboolean poll;
  BusymateRoute route;

  if (!busymate_transport_parse (raw, &origin, &frame, &data, &poll))
    {
      reply_string (self->js, reply, "");
      return TRUE;
    }
  if (poll)
    {
      on_poll (self, frame, reply);
      return TRUE;
    }
  config_of (self, &config, &webview);
  route = busymate_core_handle (&config, data, origin, current_account, self, &answer, &call);
  if (call != NULL)
    g_signal_emit (self, signals[SIGNAL_REQUEST_RECEIVED], 0, call->op,
                   busymate_origin_in_list (call->origin, (const char * const *) self->allowed));
  switch (route)
    {
    case BUSYMATE_ROUTE_IGNORE:
      break;
    case BUSYMATE_ROUTE_REPLY:
      if (g_str_equal (call->op, "hello") && busymate_origin_in_list (call->origin, (const char * const *) self->allowed))
        {
          if (g_strcmp0 (self->hello_frame, frame) != 0)
            {
              forget_frame (self);
              self->hello_frame = g_strdup (frame);
            }
          reply_string (self->js, reply, answer);
          if (self->pending_restored)
            {
              self->pending_restored = FALSE;
              emit_event (self, "restored");
            }
          return TRUE;
        }
      break;
    case BUSYMATE_ROUTE_MINT:
      handle_mint (self, g_steal_pointer (&call), reply);
      return TRUE;
    case BUSYMATE_ROUTE_OPEN:
      answer = handle_open (self, call, reply);
      if (answer == NULL)
        return TRUE;
      break;
    case BUSYMATE_ROUTE_ACTION:
      answer = handle_action (self, call);
      break;
    }
  reply_string (self->js, reply, answer);
  return TRUE;
}

/* ── lifecycle ─────────────────────────────────────────────────────────── */

static void
on_load_changed (WebKitWebView *web_view, WebKitLoadEvent event, BusymateBridge *self)
{
  if (event != WEBKIT_LOAD_COMMITTED)
    return;
  /* A new top document: the old hello frame and its held reply are gone. */
  forget_frame (self);
  if (webkit_web_view_get_uri (web_view) != NULL)
    {
      g_free (self->last_uri);
      self->last_uri = g_strdup (webkit_web_view_get_uri (web_view));
    }
}

static gboolean
reload_after_crash (gpointer data)
{
  BusymateBridge *self = data;

  self->reload_id = 0;
  if (self->web_view != NULL && self->last_uri != NULL)
    webkit_web_view_load_uri (self->web_view, self->last_uri);
  return G_SOURCE_REMOVE;
}

static void
on_web_process_terminated (WebKitWebView                     *web_view,
                           WebKitWebProcessTerminationReason  reason,
                           BusymateBridge                    *self)
{
  g_signal_emit (self, signals[SIGNAL_WEB_PROCESS_TERMINATED], 0, reason);
  if (reason == WEBKIT_WEB_PROCESS_TERMINATED_BY_API)
    return;
  forget_frame (self);
  while (self->mints->len > 0)
    busymate_mint_request_return_failed (g_ptr_array_index (self->mints, 0));
  /* WebKitGTK starts a fresh web process for the same view: reload, then tell the chat. */
  self->pending_restored = TRUE;
  if (self->last_uri == NULL && webkit_web_view_get_uri (web_view) != NULL)
    self->last_uri = g_strdup (webkit_web_view_get_uri (web_view));
  if (self->reload_id == 0)
    self->reload_id = g_idle_add (reload_after_crash, self);
}

static void
on_active_changed (GtkWindow *window, GParamSpec *pspec, BusymateBridge *self)
{
  gboolean active = gtk_window_is_active (window);

  if (active && !self->window_active)
    emit_event (self, "resume");
  self->window_active = active;
}

static void
watch_window (BusymateBridge *self)
{
  GtkWindow *window = window_of (self);

  if (window == self->window)
    return;
  if (self->window != NULL)
    g_clear_signal_handler (&self->active_id, self->window);
  g_clear_weak_pointer (&self->window);
  if (window == NULL)
    return;
  g_set_weak_pointer (&self->window, window);
  self->window_active = gtk_window_is_active (window);
  self->active_id = g_signal_connect (window, "notify::is-active", G_CALLBACK (on_active_changed), self);
}

static void
on_map (GtkWidget *widget, BusymateBridge *self)
{
  watch_window (self);
}

/**
 * busymate_bridge_new:
 * @assistant: your assistant slug; `https://<assistant>.busymate.ai` may ask
 * @origins: (nullable) (array zero-terminated=1): extra EXACT `https` origins
 *   that load the chat (your mapped support host). Wildcards are ignored.
 *
 * Returns: (transfer full): a new bridge; install it before the first load
 */
BusymateBridge *
busymate_bridge_new (const char *assistant, const char * const *origins)
{
  return g_object_new (BUSYMATE_TYPE_BRIDGE, "assistant", assistant, "origins", origins, NULL);
}

/**
 * busymate_bridge_install:
 * @self: a #BusymateBridge
 * @web_view: the chat's web view, before its first load
 *
 * Registers the `BusymateBridge` handler in the isolated `busymate` script
 * world, adds the document-start scripts for the allowed hosts and forwards
 * web-process termination and window activation. Install once per web view.
 * With no valid allowed origin the bridge stays inert and the chat runs as a
 * guest.
 *
 * Returns: %TRUE when installed
 */
gboolean
busymate_bridge_install (BusymateBridge *self, WebKitWebView *web_view)
{
  g_autofree char *isolated = NULL, *page = NULL;
  g_auto(GStrv) allow = NULL;
  g_autofree char *detailed = NULL;

  g_return_val_if_fail (BUSYMATE_IS_BRIDGE (self), FALSE);
  g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), FALSE);
  g_return_val_if_fail (self->web_view == NULL, FALSE);

  if (self->allowed == NULL || self->allowed[0] == NULL)
    return FALSE;
  self->manager = g_object_ref (webkit_web_view_get_user_content_manager (web_view));
  if (!webkit_user_content_manager_register_script_message_handler_with_reply (self->manager, BUSYMATE_BRIDGE_NAME,
                                                                               BUSYMATE_SCRIPT_WORLD))
    {
      g_warning ("BusymateBridge: a handler named %s is already registered", BUSYMATE_BRIDGE_NAME);
      g_clear_object (&self->manager);
      return FALSE;
    }
  webkit_settings_set_enable_javascript (webkit_web_view_get_settings (web_view), TRUE);
  g_set_weak_pointer (&self->web_view, web_view);
  if (self->app_id == NULL)
    {
      GApplication *app = g_application_get_default ();
      const char *id = app ? g_application_get_application_id (app) : NULL;
      self->app_id = g_strdup (id ? id : g_get_prgname () ? g_get_prgname () : "unknown");
    }

  detailed = g_strconcat ("script-message-with-reply-received::", BUSYMATE_BRIDGE_NAME, NULL);
  self->message_id = g_signal_connect (self->manager, detailed, G_CALLBACK (on_message), self);

  allow = busymate_script_allow_list ((const char * const *) self->allowed);
  isolated = busymate_script_isolated (BUSYMATE_BRIDGE_NAME, TRUE);
  page = busymate_script_page (BUSYMATE_BRIDGE_NAME);
  self->isolated_script = webkit_user_script_new_for_world (isolated, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                                            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                                                            BUSYMATE_SCRIPT_WORLD, (const char * const *) allow, NULL);
  self->page_script = webkit_user_script_new (page, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                              WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                                              (const char * const *) allow, NULL);
  webkit_user_content_manager_add_script (self->manager, self->isolated_script);
  webkit_user_content_manager_add_script (self->manager, self->page_script);

  self->terminated_id = g_signal_connect (web_view, "web-process-terminated", G_CALLBACK (on_web_process_terminated), self);
  self->load_id = g_signal_connect (web_view, "load-changed", G_CALLBACK (on_load_changed), self);
  self->map_id = g_signal_connect (web_view, "map", G_CALLBACK (on_map), self);
  if (gtk_widget_get_mapped (GTK_WIDGET (web_view)))
    watch_window (self);
  return TRUE;
}

/**
 * busymate_bridge_uninstall:
 * @self: a #BusymateBridge
 *
 * Removes the handler, scripts and signal connections from the web view it was
 * installed on. Pending mints answer `mint_failed`. Safe to call twice.
 */
void
busymate_bridge_uninstall (BusymateBridge *self)
{
  g_return_if_fail (BUSYMATE_IS_BRIDGE (self));

  forget_frame (self);
  while (self->mints->len > 0)
    busymate_mint_request_return_failed (g_ptr_array_index (self->mints, 0));
  g_clear_handle_id (&self->reload_id, g_source_remove);
  if (self->window != NULL)
    g_clear_signal_handler (&self->active_id, self->window);
  g_clear_weak_pointer (&self->window);
  if (self->web_view != NULL)
    {
      g_clear_signal_handler (&self->terminated_id, self->web_view);
      g_clear_signal_handler (&self->load_id, self->web_view);
      g_clear_signal_handler (&self->map_id, self->web_view);
    }
  g_clear_weak_pointer (&self->web_view);
  if (self->manager != NULL)
    {
      g_clear_signal_handler (&self->message_id, self->manager);
      if (self->isolated_script)
        webkit_user_content_manager_remove_script (self->manager, self->isolated_script);
      if (self->page_script)
        webkit_user_content_manager_remove_script (self->manager, self->page_script);
      webkit_user_content_manager_unregister_script_message_handler (self->manager, BUSYMATE_BRIDGE_NAME,
                                                                     BUSYMATE_SCRIPT_WORLD);
    }
  g_clear_pointer (&self->isolated_script, webkit_user_script_unref);
  g_clear_pointer (&self->page_script, webkit_user_script_unref);
  g_clear_object (&self->manager);
  self->pending_restored = FALSE;
}

/**
 * busymate_bridge_get_allowed_origins:
 * @self: a #BusymateBridge
 *
 * Returns: (transfer none) (array zero-terminated=1): the exact allow-list
 */
const char * const *
busymate_bridge_get_allowed_origins (BusymateBridge *self)
{
  g_return_val_if_fail (BUSYMATE_IS_BRIDGE (self), NULL);
  return (const char * const *) self->allowed;
}

/**
 * busymate_bridge_get_assistant:
 * @self: a #BusymateBridge
 *
 * Returns: (nullable): the configured assistant slug
 */
const char *
busymate_bridge_get_assistant (BusymateBridge *self)
{
  g_return_val_if_fail (BUSYMATE_IS_BRIDGE (self), NULL);
  return self->assistant;
}

/**
 * busymate_bridge_account_changed:
 * @self: a #BusymateBridge
 *
 * Optional: tell the chat the signed-in account changed (sign-in, sign-out or a
 * switch). Sends the `state` event; the chat also re-reads state on its own.
 */
void
busymate_bridge_account_changed (BusymateBridge *self)
{
  g_return_if_fail (BUSYMATE_IS_BRIDGE (self));
  emit_event (self, "state");
}

/**
 * busymate_bridge_resume:
 * @self: a #BusymateBridge
 *
 * Sends the `resume` event. The bridge already does this when the web view's
 * window becomes active again; call it for other host-specific resumes.
 */
void
busymate_bridge_resume (BusymateBridge *self)
{
  g_return_if_fail (BUSYMATE_IS_BRIDGE (self));
  emit_event (self, "resume");
}

static void
rebuild_allowed (BusymateBridge *self)
{
  g_strfreev (self->allowed);
  self->allowed = busymate_allowed_origins (self->assistant, (const char * const *) self->origins);
}

static void
busymate_bridge_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  BusymateBridge *self = BUSYMATE_BRIDGE (object);

  switch (prop_id)
    {
    case PROP_ASSISTANT:
      g_free (self->assistant);
      self->assistant = g_value_dup_string (value);
      rebuild_allowed (self);
      break;
    case PROP_ORIGINS:
      g_strfreev (self->origins);
      self->origins = g_value_dup_boxed (value);
      rebuild_allowed (self);
      break;
    case PROP_APP_ID:
      g_free (self->app_id);
      self->app_id = g_value_dup_string (value);
      break;
    case PROP_APP_VERSION:
      g_free (self->app_version);
      self->app_version = g_value_dup_string (value);
      break;
    case PROP_APP_BUILD:
      g_free (self->app_build);
      self->app_build = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
busymate_bridge_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  BusymateBridge *self = BUSYMATE_BRIDGE (object);

  switch (prop_id)
    {
    case PROP_ASSISTANT:
      g_value_set_string (value, self->assistant);
      break;
    case PROP_ORIGINS:
      g_value_set_boxed (value, self->origins);
      break;
    case PROP_ALLOWED_ORIGINS:
      g_value_set_boxed (value, self->allowed);
      break;
    case PROP_APP_ID:
      g_value_set_string (value, self->app_id);
      break;
    case PROP_APP_VERSION:
      g_value_set_string (value, self->app_version);
      break;
    case PROP_APP_BUILD:
      g_value_set_string (value, self->app_build);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
busymate_bridge_dispose (GObject *object)
{
  busymate_bridge_uninstall (BUSYMATE_BRIDGE (object));
  G_OBJECT_CLASS (busymate_bridge_parent_class)->dispose (object);
}

static void
busymate_bridge_finalize (GObject *object)
{
  BusymateBridge *self = BUSYMATE_BRIDGE (object);

  g_free (self->assistant);
  g_strfreev (self->origins);
  g_strfreev (self->allowed);
  g_free (self->app_id);
  g_free (self->app_version);
  g_free (self->app_build);
  g_free (self->last_uri);
  g_ptr_array_unref (self->mints);
  g_clear_object (&self->js);
  G_OBJECT_CLASS (busymate_bridge_parent_class)->finalize (object);
}

static void
busymate_bridge_class_init (BusymateBridgeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = busymate_bridge_set_property;
  object_class->get_property = busymate_bridge_get_property;
  object_class->dispose = busymate_bridge_dispose;
  object_class->finalize = busymate_bridge_finalize;

  /**
   * BusymateBridge:assistant:
   *
   * Your assistant's slug on busymate.ai; `https://<assistant>.busymate.ai` may ask.
   */
  properties[PROP_ASSISTANT] = g_param_spec_string ("assistant", NULL, NULL, NULL,
                                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * BusymateBridge:origins:
   *
   * Extra EXACT `https` origins that load the chat. Wildcards are ignored.
   */
  properties[PROP_ORIGINS] = g_param_spec_boxed ("origins", NULL, NULL, G_TYPE_STRV,
                                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * BusymateBridge:allowed-origins:
   *
   * The normalized exact allow-list.
   */
  properties[PROP_ALLOWED_ORIGINS] = g_param_spec_boxed ("allowed-origins", NULL, NULL, G_TYPE_STRV,
                                                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  /**
   * BusymateBridge:app-id:
   *
   * Your application id; salts the account key and is reported in `hello`.
   * Defaults to the default #GApplication's id, then the program name.
   */
  properties[PROP_APP_ID] = g_param_spec_string ("app-id", NULL, NULL, NULL,
                                                 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * BusymateBridge:app-version:
   *
   * Your application version, reported in `hello`.
   */
  properties[PROP_APP_VERSION] = g_param_spec_string ("app-version", NULL, NULL, NULL,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * BusymateBridge:app-build:
   *
   * Your application build, reported in `hello`.
   */
  properties[PROP_APP_BUILD] = g_param_spec_string ("app-build", NULL, NULL, NULL,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * BusymateBridge::get-account:
   * @bridge: the bridge
   *
   * Read when `state` or `mint` is asked. Never leaves the device raw.
   *
   * Returns: (transfer full) (nullable): the signed-in account id, or %NULL when signed out
   */
  signals[SIGNAL_GET_ACCOUNT] = g_signal_new ("get-account", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                              g_signal_accumulator_first_wins, NULL, NULL, G_TYPE_STRING, 0);
  /**
   * BusymateBridge::mint:
   * @bridge: the bridge
   * @request: the chat's request plus `assistant` and `origin` set by the bridge
   *
   * Ask YOUR authenticated backend for a fresh Busymate launch token and answer
   * with busymate_mint_request_return_token() (or not_signed_in / failed),
   * now or later. Never cache or reuse a token.
   *
   * Returns: %TRUE when you will answer the request
   */
  signals[SIGNAL_MINT] = g_signal_new ("mint", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                       g_signal_accumulator_true_handled, NULL, NULL,
                                       G_TYPE_BOOLEAN, 1, BUSYMATE_TYPE_MINT_REQUEST);
  /**
   * BusymateBridge::open-uri:
   * @bridge: the bridge
   * @uri: an `https` URI
   * @mode: `external` or `auth`
   *
   * Optional. Unhandled, the URI opens with #GtkUriLauncher.
   *
   * Returns: %TRUE when you opened it
   */
  signals[SIGNAL_OPEN_URI] = g_signal_new ("open-uri", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                           g_signal_accumulator_true_handled, NULL, NULL,
                                           G_TYPE_BOOLEAN, 2, G_TYPE_STRING, G_TYPE_STRING);
  /**
   * BusymateBridge::action:
   * @bridge: the bridge
   * @name: the action name (`close`, `ready`, …); also the signal detail
   * @params_json: its params as a JSON object
   *
   * Optional fixed app actions. Never expose general native execution.
   *
   * Returns: %TRUE only when handled
   */
  signals[SIGNAL_ACTION] = g_signal_new ("action", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0,
                                         g_signal_accumulator_true_handled, NULL, NULL,
                                         G_TYPE_BOOLEAN, 2, G_TYPE_STRING, G_TYPE_STRING);
  /**
   * BusymateBridge::close:
   * @bridge: the bridge
   *
   * Optional: the chat asked to be closed (fallback for an unhandled `close` action).
   */
  signals[SIGNAL_CLOSE] = g_signal_new ("close", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                        NULL, NULL, NULL, G_TYPE_NONE, 0);
  /**
   * BusymateBridge::event-sent:
   * @bridge: the bridge
   * @event: `resume`, `state` or `restored`
   *
   * Informational: the bridge sent an event to the chat (payloads are not exposed).
   */
  signals[SIGNAL_EVENT_SENT] = g_signal_new ("event-sent", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                             NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  /**
   * BusymateBridge::request-received:
   * @bridge: the bridge
   * @op: the operation name the chat asked for
   * @allowed: whether the sending frame's origin is allowed (else answered `denied`)
   *
   * Informational, for diagnostics: payloads, tokens and account ids are not exposed.
   */
  signals[SIGNAL_REQUEST_RECEIVED] = g_signal_new ("request-received", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                                   NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);
  /**
   * BusymateBridge::web-process-terminated:
   * @bridge: the bridge
   * @reason: why WebKit's web process ended
   *
   * Informational. Unless terminated by API, the bridge reloads the last chat
   * URL and sends `restored` after the chat says hello again.
   */
  signals[SIGNAL_WEB_PROCESS_TERMINATED] = g_signal_new ("web-process-terminated", G_TYPE_FROM_CLASS (klass),
                                                         G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
                                                         WEBKIT_TYPE_WEB_PROCESS_TERMINATION_REASON);
}

static void
busymate_bridge_init (BusymateBridge *self)
{
  self->js = jsc_context_new ();
  self->mints = g_ptr_array_new_with_free_func (g_object_unref);
  g_queue_init (&self->events);
  self->allowed = g_new0 (char *, 1);
}
