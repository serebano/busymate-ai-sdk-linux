/* Busymate AI SDK for Linux — optional microphone adapter 1.0.0 (wire 1) for WebKitGTK 6.0.
 *
 * Construct once per web view, before its first load, and keep it alive for
 * that web view's lifetime. The chat sends
 *   {"type":"busymate.microphone.v1.request","id":"…","source":"dictation"|"voice"}
 * only when the user taps its microphone or voice control; the adapter answers
 *   {"id":"…","granted":true|false}
 * after its permission step, and later grants WebKit's user-media request for
 * AUDIO ONLY, only while the top document is an allowed origin, only after that
 * grant. Opening the chat never asks. Camera and screen capture are denied.
 *
 * The permission step: desktop Linux has no per-app microphone permission
 * prompt that an app can ask for (see docs/microphone.md for PipeWire, Flatpak
 * and Snap). So the adapter asks the user itself, with the
 * BusymateMicrophone::request-permission signal; its default handler shows a
 * GtkAlertDialog. A grant lasts for this adapter's lifetime unless you call
 * busymate_microphone_revoke(); every activation rechecks it.
 */
#define BUSYMATE_COMPILATION
#include "busymate/busymate-microphone.h"
#include "busymate-core-private.h"

#include <gtk/gtk.h>

typedef struct {
  WebKitScriptMessageReply *reply;
  char                     *id;
} Waiter;

struct _BusymateMicrophone {
  GObject                   parent_instance;
  char                    **allowed;
  JSCContext               *js;
  WebKitWebView            *web_view;
  WebKitUserContentManager *manager;
  WebKitUserScript         *isolated_script;
  WebKitUserScript         *page_script;
  gulong                    message_id;
  gulong                    permission_id;
  gboolean                  granted;
  gboolean                  asking;
  GPtrArray                *waiting;
  GCancellable             *dialog;
};

G_DEFINE_FINAL_TYPE (BusymateMicrophone, busymate_microphone, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_GRANTED,
  N_PROPS
};

enum {
  SIGNAL_REQUEST_PERMISSION,
  SIGNAL_MEDIA_DECIDED,
  N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static void
waiter_free (Waiter *waiter)
{
  g_clear_pointer (&waiter->reply, webkit_script_message_reply_unref);
  g_free (waiter->id);
  g_free (waiter);
}

static void
reply_string (BusymateMicrophone *self, WebKitScriptMessageReply *reply, const char *text)
{
  g_autoptr(JSCValue) value = jsc_value_new_string (self->js, text != NULL ? text : "");

  webkit_script_message_reply_return_value (reply, value);
}

static void
answer_waiters (BusymateMicrophone *self, gboolean granted)
{
  g_autoptr(GPtrArray) waiting = g_steal_pointer (&self->waiting);

  self->waiting = g_ptr_array_new_with_free_func ((GDestroyNotify) waiter_free);
  for (guint i = 0; i < waiting->len; i++)
    {
      Waiter *waiter = g_ptr_array_index (waiting, i);
      g_autofree char *answer = busymate_microphone_reply (waiter->id, granted);
      reply_string (self, waiter->reply, answer);
    }
}

/**
 * busymate_microphone_complete_permission:
 * @self: a #BusymateMicrophone
 * @granted: the user's answer
 *
 * Completes a permission step you took over in
 * #BusymateMicrophone::request-permission. Every waiting request gets the same
 * answer (concurrent asks share one prompt).
 */
void
busymate_microphone_complete_permission (BusymateMicrophone *self, gboolean granted)
{
  g_return_if_fail (BUSYMATE_IS_MICROPHONE (self));
  self->asking = FALSE;
  g_clear_object (&self->dialog);
  if (self->granted != granted)
    {
      self->granted = granted;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRANTED]);
    }
  answer_waiters (self, granted);
}

static void
on_dialog_done (GObject *source, GAsyncResult *result, gpointer data)
{
  g_autoptr(BusymateMicrophone) self = data;
  g_autoptr(GError) error = NULL;
  int button = gtk_alert_dialog_choose_finish (GTK_ALERT_DIALOG (source), result, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) && self->dialog == NULL)
    return; /* uninstalled while asking */
  busymate_microphone_complete_permission (self, error == NULL && button == 1);
}

static gboolean
default_request_permission (BusymateMicrophone *self, const char *origin, const char *source)
{
  g_autoptr(GtkAlertDialog) dialog = NULL;
  const char *buttons[] = { "Don’t Allow", "Allow", NULL };
  GtkRoot *root = self->web_view ? gtk_widget_get_root (GTK_WIDGET (self->web_view)) : NULL;

  dialog = gtk_alert_dialog_new ("Allow the microphone?");
  gtk_alert_dialog_set_detail (dialog, g_strcmp0 (source, "voice") == 0
                               ? "The support assistant wants to use your microphone for a voice conversation."
                               : "The support assistant wants to use your microphone for dictation.");
  gtk_alert_dialog_set_buttons (dialog, buttons);
  gtk_alert_dialog_set_cancel_button (dialog, 0);
  gtk_alert_dialog_set_default_button (dialog, 1);
  gtk_alert_dialog_set_modal (dialog, TRUE);
  self->dialog = g_cancellable_new ();
  gtk_alert_dialog_choose (dialog, GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL, self->dialog,
                           on_dialog_done, g_object_ref (self));
  return TRUE;
}

static void
ask (BusymateMicrophone *self, WebKitScriptMessageReply *reply, const char *id, const char *origin, const char *source)
{
  Waiter *waiter;
  gboolean handled = FALSE;

  /* Every activation rechecks; only concurrent asks share one prompt. */
  if (self->granted)
    {
      g_autofree char *answer = busymate_microphone_reply (id, TRUE);
      reply_string (self, reply, answer);
      return;
    }
  waiter = g_new0 (Waiter, 1);
  waiter->reply = webkit_script_message_reply_ref (reply);
  waiter->id = g_strdup (id);
  g_ptr_array_add (self->waiting, waiter);
  if (self->asking)
    return;
  self->asking = TRUE;
  g_signal_emit (self, signals[SIGNAL_REQUEST_PERMISSION], 0, origin,
                 g_strcmp0 (source, "voice") == 0 ? "voice" : "dictation", &handled);
  if (!handled)
    busymate_microphone_complete_permission (self, FALSE);
}

static gboolean
on_message (WebKitUserContentManager *manager,
            JSCValue                 *value,
            WebKitScriptMessageReply *reply,
            BusymateMicrophone       *self)
{
  g_autofree char *raw = jsc_value_is_string (value) ? jsc_value_to_string (value) : NULL;
  g_autofree char *origin = NULL, *frame = NULL, *data = NULL, *id = NULL, *source = NULL, *exact = NULL;
  gboolean poll;

  if (!busymate_transport_parse (raw, &origin, &frame, &data, &poll) || poll ||
      !busymate_microphone_parse (data, origin, (const char * const *) self->allowed, &id, &source))
    {
      /* Not ours, or not allowed: no answer for the page (the chat times out to its blocked notice). */
      reply_string (self, reply, "");
      return TRUE;
    }
  exact = busymate_microphone_origin (origin);
  ask (self, reply, id, exact, source);
  return TRUE;
}

static gboolean
on_permission_request (WebKitWebView *web_view, WebKitPermissionRequest *request, BusymateMicrophone *self)
{
  WebKitUserMediaPermissionRequest *media;
  gboolean allowed;

  if (!WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST (request))
    return FALSE; /* not a capture request: leave it to the host */
  media = WEBKIT_USER_MEDIA_PERMISSION_REQUEST (request);
  allowed = busymate_microphone_media_allowed ((const char * const *) self->allowed,
                                               webkit_web_view_get_uri (web_view),
                                               webkit_user_media_permission_is_for_audio_device (media),
                                               webkit_user_media_permission_is_for_video_device (media),
                                               webkit_user_media_permission_is_for_display_device (media),
                                               self->granted);
  if (allowed)
    webkit_permission_request_allow (request);
  else
    webkit_permission_request_deny (request);
  g_signal_emit (self, signals[SIGNAL_MEDIA_DECIDED], 0, allowed);
  return TRUE;
}

/**
 * busymate_microphone_new:
 * @web_view: the chat's web view, before its first load
 * @origins: (array zero-terminated=1): the EXACT `https` origins serving your
 *   chat (scheme + host + optional port; no path, wildcard, query or fragment)
 *
 * Installs the `BusymateMicrophone` handler in the isolated `busymate` world,
 * its document-start scripts for the allowed hosts, enables WebKit media
 * streams and decides WebKit's user-media permission requests on @web_view.
 * With no valid origin the adapter stays inert.
 *
 * Returns: (transfer full): the adapter; keep it for the web view's lifetime
 */
BusymateMicrophone *
busymate_microphone_new (WebKitWebView *web_view, const char * const *origins)
{
  BusymateMicrophone *self;
  g_autofree char *isolated = NULL, *page = NULL, *detailed = NULL;
  g_auto(GStrv) allow = NULL;

  g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);
  self = g_object_new (BUSYMATE_TYPE_MICROPHONE, NULL);
  g_strfreev (self->allowed);
  self->allowed = busymate_microphone_allowed (origins);
  if (self->allowed[0] == NULL)
    return self;
  self->manager = g_object_ref (webkit_web_view_get_user_content_manager (web_view));
  if (!webkit_user_content_manager_register_script_message_handler_with_reply (self->manager, BUSYMATE_MICROPHONE_NAME,
                                                                               BUSYMATE_SCRIPT_WORLD))
    {
      g_warning ("BusymateMicrophone: a handler named %s is already registered", BUSYMATE_MICROPHONE_NAME);
      g_clear_object (&self->manager);
      return self;
    }
  g_set_weak_pointer (&self->web_view, web_view);
  webkit_settings_set_enable_media_stream (webkit_web_view_get_settings (web_view), TRUE);

  detailed = g_strconcat ("script-message-with-reply-received::", BUSYMATE_MICROPHONE_NAME, NULL);
  self->message_id = g_signal_connect (self->manager, detailed, G_CALLBACK (on_message), self);
  allow = busymate_script_allow_list ((const char * const *) self->allowed);
  isolated = busymate_script_isolated (BUSYMATE_MICROPHONE_NAME, FALSE);
  page = busymate_script_page (BUSYMATE_MICROPHONE_NAME);
  self->isolated_script = webkit_user_script_new_for_world (isolated, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                                            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                                                            BUSYMATE_SCRIPT_WORLD, (const char * const *) allow, NULL);
  self->page_script = webkit_user_script_new (page, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                              WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                                              (const char * const *) allow, NULL);
  webkit_user_content_manager_add_script (self->manager, self->isolated_script);
  webkit_user_content_manager_add_script (self->manager, self->page_script);
  self->permission_id = g_signal_connect (web_view, "permission-request", G_CALLBACK (on_permission_request), self);
  return self;
}

/**
 * busymate_microphone_is_installed:
 * @self: a #BusymateMicrophone
 *
 * Returns: whether the adapter is active on a web view
 */
gboolean
busymate_microphone_is_installed (BusymateMicrophone *self)
{
  g_return_val_if_fail (BUSYMATE_IS_MICROPHONE (self), FALSE);
  return self->web_view != NULL && self->manager != NULL;
}

/**
 * busymate_microphone_get_allowed_origins:
 * @self: a #BusymateMicrophone
 *
 * Returns: (transfer none) (array zero-terminated=1): the exact microphone origins
 */
const char * const *
busymate_microphone_get_allowed_origins (BusymateMicrophone *self)
{
  g_return_val_if_fail (BUSYMATE_IS_MICROPHONE (self), NULL);
  return (const char * const *) self->allowed;
}

/**
 * busymate_microphone_get_granted:
 * @self: a #BusymateMicrophone
 *
 * Returns: whether the user granted the microphone in this adapter's lifetime
 */
gboolean
busymate_microphone_get_granted (BusymateMicrophone *self)
{
  g_return_val_if_fail (BUSYMATE_IS_MICROPHONE (self), FALSE);
  return self->granted;
}

/**
 * busymate_microphone_revoke:
 * @self: a #BusymateMicrophone
 *
 * Forgets the grant: the next activation asks again and WebKit capture is
 * denied until then. Use it for an app setting that turns the microphone off.
 */
void
busymate_microphone_revoke (BusymateMicrophone *self)
{
  g_return_if_fail (BUSYMATE_IS_MICROPHONE (self));
  if (!self->granted)
    return;
  self->granted = FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRANTED]);
}

/**
 * busymate_microphone_uninstall:
 * @self: a #BusymateMicrophone
 *
 * Removes the handler, scripts and permission-request handler. Waiting
 * requests are answered `granted: false`. Safe to call twice.
 */
void
busymate_microphone_uninstall (BusymateMicrophone *self)
{
  g_return_if_fail (BUSYMATE_IS_MICROPHONE (self));
  if (self->dialog != NULL)
    {
      GCancellable *dialog = g_steal_pointer (&self->dialog);
      g_cancellable_cancel (dialog);
      g_object_unref (dialog);
    }
  answer_waiters (self, FALSE);
  self->asking = FALSE;
  if (self->web_view != NULL)
    g_clear_signal_handler (&self->permission_id, self->web_view);
  g_clear_weak_pointer (&self->web_view);
  if (self->manager != NULL)
    {
      g_clear_signal_handler (&self->message_id, self->manager);
      webkit_user_content_manager_remove_script (self->manager, self->isolated_script);
      webkit_user_content_manager_remove_script (self->manager, self->page_script);
      webkit_user_content_manager_unregister_script_message_handler (self->manager, BUSYMATE_MICROPHONE_NAME,
                                                                     BUSYMATE_SCRIPT_WORLD);
    }
  g_clear_pointer (&self->isolated_script, webkit_user_script_unref);
  g_clear_pointer (&self->page_script, webkit_user_script_unref);
  g_clear_object (&self->manager);
}

static void
busymate_microphone_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  BusymateMicrophone *self = BUSYMATE_MICROPHONE (object);

  switch (prop_id)
    {
    case PROP_GRANTED:
      g_value_set_boolean (value, self->granted);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
busymate_microphone_dispose (GObject *object)
{
  busymate_microphone_uninstall (BUSYMATE_MICROPHONE (object));
  G_OBJECT_CLASS (busymate_microphone_parent_class)->dispose (object);
}

static void
busymate_microphone_finalize (GObject *object)
{
  BusymateMicrophone *self = BUSYMATE_MICROPHONE (object);

  g_strfreev (self->allowed);
  g_ptr_array_unref (self->waiting);
  g_clear_object (&self->js);
  G_OBJECT_CLASS (busymate_microphone_parent_class)->finalize (object);
}

static void
busymate_microphone_class_init (BusymateMicrophoneClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = busymate_microphone_get_property;
  object_class->dispose = busymate_microphone_dispose;
  object_class->finalize = busymate_microphone_finalize;

  /**
   * BusymateMicrophone:granted:
   *
   * Whether the user granted the microphone in this adapter's lifetime.
   */
  properties[PROP_GRANTED] = g_param_spec_boolean ("granted", NULL, NULL, FALSE,
                                                   G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * BusymateMicrophone::request-permission:
   * @microphone: the adapter
   * @origin: the exact origin of the asking chat frame
   * @source: `dictation` or `voice`
   *
   * Emitted only after a user tap in the chat, when no grant is held. Take it
   * over by returning %TRUE and later calling
   * busymate_microphone_complete_permission(). Unhandled, the default handler
   * asks with a modal #GtkAlertDialog on the web view's window.
   *
   * Returns: %TRUE when the permission step will be completed
   */
  signals[SIGNAL_REQUEST_PERMISSION] =
    g_signal_new_class_handler ("request-permission", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                G_CALLBACK (default_request_permission), g_signal_accumulator_true_handled, NULL, NULL,
                                G_TYPE_BOOLEAN, 2, G_TYPE_STRING, G_TYPE_STRING);
  /**
   * BusymateMicrophone::media-decided:
   * @microphone: the adapter
   * @granted: whether WebKit's user-media request was allowed
   *
   * Informational: the adapter decided a WebKit user-media permission request.
   */
  signals[SIGNAL_MEDIA_DECIDED] = g_signal_new ("media-decided", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                                NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
busymate_microphone_init (BusymateMicrophone *self)
{
  self->js = jsc_context_new ();
  self->waiting = g_ptr_array_new_with_free_func ((GDestroyNotify) waiter_free);
  self->allowed = g_new0 (char *, 1);
}
