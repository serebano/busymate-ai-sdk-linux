/* Busymate AI SDK for Linux — GTK4 example app.
 *
 * A real consumer of the SDK library in this repository: it installs the
 * identity bridge v2 and the optional microphone adapter before loading the
 * hosted chat, restricts navigation to the configured origins, forwards the
 * SDK callbacks to a bounded events log, and offers a settings dialog for the
 * chat URL, locale, origins, guest vs identified (mint callback) mode, the
 * microphone adapter and the close callbacks.
 */
#include <busymate/busymate.h>

#include "demo-config.h"

#define MAX_EVENTS 100
#define APP_ID "ai.busymate.sdk.example"

typedef struct {
  GtkApplication      *app;
  GtkWindow           *window;
  WebKitWebView       *view;
  GtkLabel            *status;
  GtkListBox          *events;
  GtkRevealer         *events_pane;
  guint                event_count;

  DemoConfig          *config;   /* what the settings dialog edits */
  DemoConfig          *applied;  /* what the installed SDK reads */
  DemoValidated       *valid;
  BusymateBridge      *bridge;
  BusymateMicrophone  *microphone;
  SoupSession         *session;

  /* settings dialog */
  GtkWindow           *dialog;
  GtkEntry            *assistant_entry;
  GtkEntry            *chat_url_entry;
  GtkEntry            *locale_entry;
  GtkEntry            *origins_entry;
  GtkSwitch           *identified_switch;
  GtkEntry            *account_entry;
  GtkEntry            *mint_entry;
  GtkPasswordEntry    *session_entry;
  GtkSwitch           *microphone_switch;
  GtkSwitch           *action_switch;
  GtkSwitch           *close_switch;
  GtkLabel            *error_label;

  /* --smoke-test */
  gboolean             smoke;
  gboolean             smoke_loaded;
  gboolean             smoke_hello;
  int                  exit_code;
} Demo;

static Demo demo;

static void apply_config (void);

static void
log_event (const char *format, ...)
{
  va_list args;
  g_autofree char *message = NULL;
  g_autoptr(GDateTime) now = g_date_time_new_now_local ();
  g_autofree char *time = g_date_time_format (now, "%H:%M:%S");
  g_autofree char *line = NULL;
  GtkWidget *label;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);
  line = g_strdup_printf ("%s · %s", time, message);
  if (demo.smoke)
    g_print ("event: %s\n", message);
  if (demo.events == NULL)
    return;
  label = gtk_label_new (line);
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_list_box_prepend (demo.events, label);
  if (++demo.event_count > MAX_EVENTS)
    {
      GtkListBoxRow *last = gtk_list_box_get_row_at_index (demo.events, MAX_EVENTS);
      if (last != NULL)
        gtk_list_box_remove (demo.events, GTK_WIDGET (last));
      demo.event_count = MAX_EVENTS;
    }
}

static void
set_status (const char *text)
{
  gtk_label_set_text (demo.status, text);
}

/* ── SDK callbacks ─────────────────────────────────────────────────────── */

static char *
on_get_account (BusymateBridge *bridge, gpointer data)
{
  if (!demo.applied->signed_in || demo.applied->account_id == NULL || *demo.applied->account_id == '\0')
    return NULL;
  return g_strdup (demo.applied->account_id);
}

static void
on_minted (GObject *source, GAsyncResult *result, gpointer data)
{
  g_autoptr(BusymateMintRequest) request = data;
  g_autofree char *token = NULL;

  switch (demo_mint_send_finish (result, &token))
    {
    case DEMO_MINT_TOKEN:
      log_event ("Mint: fresh correlated assertion received");
      busymate_mint_request_return_token (request, token, busymate_mint_request_get_nonce (request));
      break;
    case DEMO_MINT_NOT_SIGNED_IN:
      log_event ("Mint: backend reports not signed in");
      busymate_mint_request_return_not_signed_in (request);
      break;
    case DEMO_MINT_FAILED:
      log_event ("Mint: backend request failed; guest fallback");
      busymate_mint_request_return_failed (request);
      break;
    }
}

static gboolean
on_mint (BusymateBridge *bridge, BusymateMintRequest *request, gpointer data)
{
  g_autofree char *body = NULL;

  if (!demo.applied->signed_in || demo.valid->mint_url == NULL)
    {
      log_event ("Mint: signed out, no backend request");
      busymate_mint_request_return_not_signed_in (request);
      return TRUE;
    }
  if (busymate_mint_request_get_nonce (request) == NULL)
    {
      busymate_mint_request_return_failed (request);
      return TRUE;
    }
  body = demo_mint_body (busymate_mint_request_get_nonce (request), busymate_mint_request_get_audience (request),
                         busymate_mint_request_get_reason (request), busymate_mint_request_get_assistant (request),
                         busymate_mint_request_get_origin (request));
  log_event ("Mint: authenticated backend requested");
  demo_mint_send (demo.session, demo.valid->mint_url, body, demo.applied->backend_session_token,
                  busymate_mint_request_get_nonce (request), NULL, on_minted, g_object_ref (request));
  return TRUE;
}

static void
close_chat (void)
{
  /* Navigating away destroys the chat document and its media tracks. */
  webkit_web_view_load_html (demo.view, "<html><body style=\"font-family:sans-serif\">"
                                        "<p>Chat closed. Use Reload to reopen.</p></body></html>", "about:blank");
  set_status ("Chat closed");
}

static gboolean
on_action (BusymateBridge *bridge, const char *name, const char *params_json, gpointer data)
{
  if (g_strcmp0 (name, "ready") == 0)
    log_event ("SDK action: ready");
  if (!demo_action_handles (name))
    return FALSE;
  log_event ("SDK action: close handled by action callback");
  close_chat ();
  return TRUE;
}

static void
on_close (BusymateBridge *bridge, gpointer data)
{
  log_event ("SDK close fallback");
  close_chat ();
}

static gboolean
on_open_uri (BusymateBridge *bridge, const char *uri, const char *mode, gpointer data)
{
  log_event ("SDK open (%s): handed to the desktop", mode);
  return FALSE; /* the SDK's default GtkUriLauncher opens it */
}

static void
on_event_sent (BusymateBridge *bridge, const char *event, gpointer data)
{
  log_event ("SDK event: %s (payload omitted)", event);
}

static void
on_request_received (BusymateBridge *bridge, const char *op, gboolean allowed, gpointer data)
{
  log_event ("SDK request: %s%s", op, allowed ? "" : " (denied: origin not allowed)");
  if (allowed && g_strcmp0 (op, "hello") == 0)
    demo.smoke_hello = TRUE;
}

static void
on_web_process_terminated (BusymateBridge *bridge, WebKitWebProcessTerminationReason reason, gpointer data)
{
  log_event ("Web process terminated (%s); SDK reloads the chat",
             reason == WEBKIT_WEB_PROCESS_CRASHED ? "crashed" :
             reason == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT ? "memory limit" : "by API");
}

static void
on_media_decided (BusymateMicrophone *microphone, gboolean granted, gpointer data)
{
  log_event ("WebView media decision: %s", granted ? "grant (audio only)" : "deny");
}

static void
on_granted_changed (BusymateMicrophone *microphone, GParamSpec *pspec, gpointer data)
{
  log_event ("Microphone permission step: %s", busymate_microphone_get_granted (microphone) ? "granted" : "not granted");
}

static gboolean
on_permission_request (WebKitWebView *view, WebKitPermissionRequest *request, gpointer data)
{
  /* Runs after the adapter (connected first) for anything it does not decide. */
  if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST (request))
    {
      log_event ("WebView media decision: deny (native microphone adapter disabled)");
      webkit_permission_request_deny (request);
      return TRUE;
    }
  return FALSE;
}

/* ── web view ──────────────────────────────────────────────────────────── */

static gboolean
on_decide_policy (WebKitWebView *view, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type, gpointer data)
{
  WebKitNavigationAction *action;
  const char *uri;
  g_autofree char *origin = NULL;

  if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE)
    return FALSE;
  action = webkit_navigation_policy_decision_get_navigation_action (WEBKIT_NAVIGATION_POLICY_DECISION (decision));
  uri = webkit_uri_request_get_uri (webkit_navigation_action_get_request (action));
  if (uri != NULL && g_str_has_prefix (uri, "about:"))
    return FALSE;
  origin = demo_secure_url_origin (uri);
  if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION && demo.valid != NULL && origin != NULL &&
      g_strv_contains ((const char * const *) demo.valid->origins, origin))
    return FALSE;
  log_event (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION
             ? "New window refused: the chat opens links through the SDK"
             : "Navigation refused: outside configured HTTPS origins");
  webkit_policy_decision_ignore (decision);
  return TRUE;
}

static gboolean
smoke_finish (gpointer data)
{
  static gboolean finished;

  if (finished)
    return G_SOURCE_REMOVE;
  finished = TRUE;
  g_print ("smoke: chat %s; hosted chat hello %s\n", demo.smoke_loaded ? "loaded" : "NOT loaded",
           demo.smoke_hello ? "received" : "not observed");
  demo.exit_code = demo.smoke_loaded ? 0 : 1;
  g_application_quit (G_APPLICATION (demo.app));
  return G_SOURCE_REMOVE;
}

static void
on_load_changed (WebKitWebView *view, WebKitLoadEvent event, gpointer data)
{
  if (event != WEBKIT_LOAD_FINISHED)
    return;
  log_event ("Chat document loaded");
  if (demo.smoke && !demo.smoke_loaded && g_strcmp0 (webkit_web_view_get_uri (view), "about:blank") != 0)
    {
      demo.smoke_loaded = TRUE;
      g_timeout_add_seconds (10, smoke_finish, NULL);
    }
}

static gboolean
on_load_failed (WebKitWebView *view, WebKitLoadEvent event, const char *uri, GError *error, gpointer data)
{
  set_status ("Chat unavailable — verify network and configured URL");
  log_event ("Navigation failed");
  return FALSE;
}

/* ── apply ─────────────────────────────────────────────────────────────── */

static void
uninstall_sdk (void)
{
  if (demo.bridge != NULL)
    {
      busymate_bridge_uninstall (demo.bridge);
      g_clear_object (&demo.bridge);
    }
  if (demo.microphone != NULL)
    {
      busymate_microphone_uninstall (demo.microphone);
      g_clear_object (&demo.microphone);
    }
}

static gboolean
apply_from (DemoConfig *config, GError **error)
{
  g_autoptr(DemoValidated) valid = demo_config_validate (config, error);
  DemoCallbacks callbacks;

  if (valid == NULL)
    return FALSE;
  webkit_web_view_stop_loading (demo.view);
  uninstall_sdk ();
  demo_config_free (demo.applied);
  demo.applied = demo_config_copy (config);
  demo_validated_free (demo.valid);
  demo.valid = g_steal_pointer (&valid);

  /* The adapter decides user-media requests first; connect it before the host's fallback. */
  if (demo.applied->microphone_enabled)
    {
      demo.microphone = busymate_microphone_new (demo.view, (const char * const *) demo.valid->origins);
      g_signal_connect (demo.microphone, "media-decided", G_CALLBACK (on_media_decided), NULL);
      g_signal_connect (demo.microphone, "notify::granted", G_CALLBACK (on_granted_changed), NULL);
    }

  demo.bridge = busymate_bridge_new (demo.valid->assistant, (const char * const *) demo.valid->origins);
  g_object_set (demo.bridge, "app-id", APP_ID, "app-version", BUSYMATE_SDK_VERSION, NULL);
  g_signal_connect (demo.bridge, "get-account", G_CALLBACK (on_get_account), NULL);
  g_signal_connect (demo.bridge, "mint", G_CALLBACK (on_mint), NULL);
  g_signal_connect (demo.bridge, "open-uri", G_CALLBACK (on_open_uri), NULL);
  g_signal_connect (demo.bridge, "event-sent", G_CALLBACK (on_event_sent), NULL);
  g_signal_connect (demo.bridge, "request-received", G_CALLBACK (on_request_received), NULL);
  g_signal_connect (demo.bridge, "web-process-terminated", G_CALLBACK (on_web_process_terminated), NULL);
  callbacks = demo_callbacks_for (demo.applied);
  if (callbacks.connect_action)
    g_signal_connect (demo.bridge, "action", G_CALLBACK (on_action), NULL);
  if (callbacks.connect_close)
    g_signal_connect (demo.bridge, "close", G_CALLBACK (on_close), NULL);
  if (!busymate_bridge_install (demo.bridge, demo.view))
    log_event ("Identity bridge not installed (no valid origin)");

  log_event ("Installed identity v2 (build %s); native microphone %s", BUSYMATE_BRIDGE_BUILD,
             demo.applied->microphone_enabled ? "enabled" : "disabled");
  set_status (demo.applied->signed_in ? "Identified mode — requires your authenticated backend" : "Guest chat");
  webkit_web_view_load_uri (demo.view, demo.valid->chat_url);
  return TRUE;
}

static void
apply_config (void)
{
  g_autoptr(GError) error = NULL;

  if (!apply_from (demo.config, &error))
    {
      set_status (error->message);
      log_event ("Configuration rejected");
    }
}

/* ── settings dialog ───────────────────────────────────────────────────── */

static void
read_dialog (DemoConfig *config)
{
  g_free (config->assistant);
  config->assistant = g_strdup (gtk_editable_get_text (GTK_EDITABLE (demo.assistant_entry)));
  g_free (config->chat_url);
  config->chat_url = g_strdup (gtk_editable_get_text (GTK_EDITABLE (demo.chat_url_entry)));
  g_free (config->locale);
  config->locale = g_strdup (gtk_editable_get_text (GTK_EDITABLE (demo.locale_entry)));
  g_free (config->origins);
  config->origins = g_strdup (gtk_editable_get_text (GTK_EDITABLE (demo.origins_entry)));
  config->signed_in = gtk_switch_get_active (demo.identified_switch);
  g_free (config->account_id);
  config->account_id = g_strdup (gtk_editable_get_text (GTK_EDITABLE (demo.account_entry)));
  g_free (config->mint_url);
  config->mint_url = g_strdup (gtk_editable_get_text (GTK_EDITABLE (demo.mint_entry)));
  g_free (config->backend_session_token);
  config->backend_session_token = g_strdup (gtk_editable_get_text (GTK_EDITABLE (demo.session_entry)));
  config->microphone_enabled = gtk_switch_get_active (demo.microphone_switch);
  config->handle_close_action = gtk_switch_get_active (demo.action_switch);
  config->close_callback_enabled = gtk_switch_get_active (demo.close_switch);
}

static void
fill_dialog (const DemoConfig *config)
{
  gtk_editable_set_text (GTK_EDITABLE (demo.assistant_entry), config->assistant);
  gtk_editable_set_text (GTK_EDITABLE (demo.chat_url_entry), config->chat_url);
  gtk_editable_set_text (GTK_EDITABLE (demo.locale_entry), config->locale);
  gtk_editable_set_text (GTK_EDITABLE (demo.origins_entry), config->origins);
  gtk_switch_set_active (demo.identified_switch, config->signed_in);
  gtk_editable_set_text (GTK_EDITABLE (demo.account_entry), config->account_id);
  gtk_editable_set_text (GTK_EDITABLE (demo.mint_entry), config->mint_url);
  gtk_editable_set_text (GTK_EDITABLE (demo.session_entry), config->backend_session_token);
  gtk_switch_set_active (demo.microphone_switch, config->microphone_enabled);
  gtk_switch_set_active (demo.action_switch, config->handle_close_action);
  gtk_switch_set_active (demo.close_switch, config->close_callback_enabled);
  gtk_label_set_text (demo.error_label, "");
}

static void
on_apply_clicked (GtkButton *button, gpointer data)
{
  g_autoptr(DemoConfig) edited = demo_config_copy (demo.config);
  g_autoptr(GError) error = NULL;

  read_dialog (edited);
  if (!apply_from (edited, &error))
    {
      gtk_label_set_text (demo.error_label, error->message);
      log_event ("Configuration rejected");
      return;
    }
  demo_config_free (demo.config);
  demo.config = g_steal_pointer (&edited);
  gtk_window_close (demo.dialog);
}

static void
on_account_changed_clicked (GtkButton *button, gpointer data)
{
  g_autoptr(DemoConfig) edited = demo_config_copy (demo.applied);
  g_autoptr(DemoValidated) valid = NULL;
  g_autoptr(GError) error = NULL;

  read_dialog (edited);
  valid = demo_config_validate (edited, &error);
  if (valid == NULL)
    {
      gtk_label_set_text (demo.error_label, error->message);
      return;
    }
  /* Only the account reader and backend session change; the installed bridge stays. */
  demo.applied->signed_in = edited->signed_in;
  g_free (demo.applied->account_id);
  demo.applied->account_id = g_strdup (edited->account_id);
  g_free (demo.applied->backend_session_token);
  demo.applied->backend_session_token = g_strdup (edited->backend_session_token);
  g_free (demo.valid->mint_url);
  demo.valid->mint_url = g_strdup (valid->mint_url);
  if (demo.bridge != NULL)
    busymate_bridge_account_changed (demo.bridge);
  log_event ("accountChanged() forwarded; no identity fabricated");
  gtk_label_set_text (demo.error_label, "Account change forwarded to the chat.");
}

static void
on_revoke_clicked (GtkButton *button, gpointer data)
{
  if (demo.microphone != NULL)
    busymate_microphone_revoke (demo.microphone);
  log_event ("Microphone grant revoked; the next tap asks again");
}

static void
on_reset_clicked (GtkButton *button, gpointer data)
{
  g_autoptr(DemoConfig) defaults = demo_config_new_default ();

  fill_dialog (defaults);
}

static void
on_cancel_clicked (GtkButton *button, gpointer data)
{
  gtk_window_close (demo.dialog);
}

static GtkWidget *
add_row (GtkGrid *grid, int row, const char *label, GtkWidget *field, const char *hint)
{
  GtkWidget *title = gtk_label_new (label);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);

  gtk_label_set_xalign (GTK_LABEL (title), 1);
  gtk_widget_set_valign (title, GTK_ALIGN_START);
  gtk_widget_set_margin_top (title, 6);
  gtk_widget_set_hexpand (field, TRUE);
  gtk_box_append (GTK_BOX (box), field);
  if (hint != NULL)
    {
      GtkWidget *note = gtk_label_new (hint);
      gtk_label_set_xalign (GTK_LABEL (note), 0);
      gtk_label_set_wrap (GTK_LABEL (note), TRUE);
      gtk_widget_add_css_class (note, "dim-label");
      gtk_widget_add_css_class (note, "caption");
      gtk_box_append (GTK_BOX (box), note);
    }
  gtk_grid_attach (grid, title, 0, row, 1, 1);
  gtk_grid_attach (grid, box, 1, row, 1, 1);
  return field;
}

static GtkWidget *
switch_row (GtkGrid *grid, int row, const char *label, GtkSwitch **out, const char *hint)
{
  GtkWidget *toggle = gtk_switch_new ();
  GtkWidget *holder = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  gtk_widget_set_halign (toggle, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (holder), toggle);
  *out = GTK_SWITCH (toggle);
  return add_row (grid, row, label, holder, hint);
}

static void
on_dialog_destroy (GtkWidget *widget, gpointer data)
{
  demo.dialog = NULL;
}

static void
show_settings (void)
{
  GtkWidget *grid, *scroller, *buttons, *outer, *button;
  int row = 0;

  if (demo.dialog != NULL)
    {
      gtk_window_present (demo.dialog);
      return;
    }
  demo.dialog = GTK_WINDOW (gtk_window_new ());
  gtk_window_set_title (demo.dialog, "Settings");
  gtk_window_set_transient_for (demo.dialog, demo.window);
  gtk_window_set_modal (demo.dialog, TRUE);
  gtk_window_set_default_size (demo.dialog, 620, 640);
  g_signal_connect (demo.dialog, "destroy", G_CALLBACK (on_dialog_destroy), NULL);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 10);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_widget_set_margin_start (grid, 16);
  gtk_widget_set_margin_end (grid, 16);
  gtk_widget_set_margin_top (grid, 16);
  gtk_widget_set_margin_bottom (grid, 16);

  demo.assistant_entry = GTK_ENTRY (add_row (GTK_GRID (grid), row++, "Assistant slug", gtk_entry_new (),
                                             "Bridge assistant: https://<slug>.busymate.ai may ask."));
  demo.chat_url_entry = GTK_ENTRY (add_row (GTK_GRID (grid), row++, "Chat HTTPS URL", gtk_entry_new (),
                                            "Loaded in the WebKitGTK view; keep your tenant's query parameters (channel=desktop_sdk)."));
  demo.locale_entry = GTK_ENTRY (add_row (GTK_GRID (grid), row++, "Locale", gtk_entry_new (),
                                          "Sets the chat URL's locale parameter, e.g. en, de, pt-BR."));
  demo.origins_entry = GTK_ENTRY (add_row (GTK_GRID (grid), row++, "Exact HTTPS origins", gtk_entry_new (),
                                           "Comma separated. Bridge and microphone allow-list; must include the chat origin."));
  switch_row (GTK_GRID (grid), row++, "Identified mode", &demo.identified_switch,
              "Off: guest, no mint request. On: the mint callback POSTs to your backend.");
  demo.account_entry = GTK_ENTRY (add_row (GTK_GRID (grid), row++, "Account ID", gtk_entry_new (),
                                           "What the app's account reader returns. Not a login or proof."));
  demo.mint_entry = GTK_ENTRY (add_row (GTK_GRID (grid), row++, "Mint endpoint", gtk_entry_new (),
                                        "Your authenticated HTTPS endpoint returning {\"token\",\"nonce\"}. Redirects are refused."));
  demo.session_entry = GTK_PASSWORD_ENTRY (add_row (GTK_GRID (grid), row++, "Backend session token",
                                                    gtk_password_entry_new (),
                                                    "Optional Bearer credential for YOUR backend, memory only. Never a signing or admin key."));
  switch_row (GTK_GRID (grid), row++, "Native microphone", &demo.microphone_switch,
              "Installs the microphone adapter: permission is asked only after a tap in the chat.");
  switch_row (GTK_GRID (grid), row++, "Close via action", &demo.action_switch,
              "Connects the action callback; it handles close only.");
  switch_row (GTK_GRID (grid), row++, "Close fallback", &demo.close_switch,
              "Connects the close callback (fallback for an unhandled close action).");

  demo.error_label = GTK_LABEL (gtk_label_new (""));
  gtk_label_set_wrap (demo.error_label, TRUE);
  gtk_label_set_xalign (demo.error_label, 0);
  gtk_widget_add_css_class (GTK_WIDGET (demo.error_label), "error");
  gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (demo.error_label), 0, row++, 2, 1);

  scroller = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), grid);
  gtk_widget_set_vexpand (scroller, TRUE);

  buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_start (buttons, 16);
  gtk_widget_set_margin_end (buttons, 16);
  gtk_widget_set_margin_bottom (buttons, 16);
  button = gtk_button_new_with_label ("Forward accountChanged()");
  g_signal_connect (button, "clicked", G_CALLBACK (on_account_changed_clicked), NULL);
  gtk_box_append (GTK_BOX (buttons), button);
  button = gtk_button_new_with_label ("Revoke microphone");
  g_signal_connect (button, "clicked", G_CALLBACK (on_revoke_clicked), NULL);
  gtk_box_append (GTK_BOX (buttons), button);
  button = gtk_button_new_with_label ("Reset");
  g_signal_connect (button, "clicked", G_CALLBACK (on_reset_clicked), NULL);
  gtk_box_append (GTK_BOX (buttons), button);
  button = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand (button, TRUE);
  gtk_box_append (GTK_BOX (buttons), button);
  button = gtk_button_new_with_label ("Cancel");
  g_signal_connect (button, "clicked", G_CALLBACK (on_cancel_clicked), NULL);
  gtk_box_append (GTK_BOX (buttons), button);
  button = gtk_button_new_with_label ("Apply and reload");
  gtk_widget_add_css_class (button, "suggested-action");
  g_signal_connect (button, "clicked", G_CALLBACK (on_apply_clicked), NULL);
  gtk_box_append (GTK_BOX (buttons), button);

  outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append (GTK_BOX (outer), scroller);
  gtk_box_append (GTK_BOX (outer), buttons);
  gtk_window_set_child (demo.dialog, outer);
  fill_dialog (demo.config);
  gtk_window_present (demo.dialog);
}

/* ── main window ───────────────────────────────────────────────────────── */

static void
on_settings_clicked (GtkButton *button, gpointer data)
{
  show_settings ();
}

static void
on_reload_clicked (GtkButton *button, gpointer data)
{
  apply_config ();
  log_event ("Chat reloaded");
}

static void
on_events_toggled (GtkToggleButton *button, gpointer data)
{
  gtk_revealer_set_reveal_child (demo.events_pane, gtk_toggle_button_get_active (button));
}

static void
build_window (GtkApplication *app)
{
  GtkWidget *header, *button, *toggle, *box, *body, *scroller, *events_box, *title;

  demo.window = GTK_WINDOW (gtk_application_window_new (app));
  gtk_window_set_title (demo.window, "Busymate SDK Example");
  gtk_window_set_default_size (demo.window, 1100, 760);

  header = gtk_header_bar_new ();
  button = gtk_button_new_with_label ("Settings");
  g_signal_connect (button, "clicked", G_CALLBACK (on_settings_clicked), NULL);
  gtk_header_bar_pack_start (GTK_HEADER_BAR (header), button);
  button = gtk_button_new_with_label ("Reload");
  g_signal_connect (button, "clicked", G_CALLBACK (on_reload_clicked), NULL);
  gtk_header_bar_pack_start (GTK_HEADER_BAR (header), button);
  toggle = gtk_toggle_button_new_with_label ("Events");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), TRUE);
  g_signal_connect (toggle, "toggled", G_CALLBACK (on_events_toggled), NULL);
  gtk_header_bar_pack_end (GTK_HEADER_BAR (header), toggle);
  gtk_window_set_titlebar (demo.window, header);

  demo.view = WEBKIT_WEB_VIEW (webkit_web_view_new ());
  gtk_widget_set_hexpand (GTK_WIDGET (demo.view), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (demo.view), TRUE);
  webkit_settings_set_enable_webrtc (webkit_web_view_get_settings (demo.view), TRUE);
  g_signal_connect (demo.view, "decide-policy", G_CALLBACK (on_decide_policy), NULL);
  g_signal_connect (demo.view, "load-changed", G_CALLBACK (on_load_changed), NULL);
  g_signal_connect (demo.view, "load-failed", G_CALLBACK (on_load_failed), NULL);

  demo.events = GTK_LIST_BOX (gtk_list_box_new ());
  gtk_list_box_set_selection_mode (demo.events, GTK_SELECTION_NONE);
  scroller = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), GTK_WIDGET (demo.events));
  gtk_widget_set_vexpand (scroller, TRUE);
  gtk_widget_set_size_request (scroller, 300, -1);
  events_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  title = gtk_label_new ("Callbacks and events (no payloads, tokens or ids)");
  gtk_label_set_wrap (GTK_LABEL (title), TRUE);
  gtk_widget_add_css_class (title, "heading");
  gtk_box_append (GTK_BOX (events_box), title);
  gtk_box_append (GTK_BOX (events_box), scroller);
  demo.events_pane = GTK_REVEALER (gtk_revealer_new ());
  gtk_revealer_set_transition_type (demo.events_pane, GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
  gtk_revealer_set_child (demo.events_pane, events_box);
  gtk_revealer_set_reveal_child (demo.events_pane, TRUE);

  body = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_append (GTK_BOX (body), GTK_WIDGET (demo.view));
  gtk_box_append (GTK_BOX (body), GTK_WIDGET (demo.events_pane));

  demo.status = GTK_LABEL (gtk_label_new ("Ready — guest chat"));
  gtk_label_set_xalign (demo.status, 0);
  gtk_widget_set_margin_start (GTK_WIDGET (demo.status), 8);
  gtk_widget_set_margin_top (GTK_WIDGET (demo.status), 4);
  gtk_widget_set_margin_bottom (GTK_WIDGET (demo.status), 4);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (box), body);
  gtk_box_append (GTK_BOX (box), GTK_WIDGET (demo.status));
  gtk_window_set_child (demo.window, box);
}

static void
on_activate (GtkApplication *app, gpointer data)
{
  if (demo.window != NULL)
    {
      gtk_window_present (demo.window);
      return;
    }
  build_window (app);
  /* The adapter connects permission-request first when enabled; this host fallback runs after it. */
  apply_config ();
  g_signal_connect_after (demo.view, "permission-request", G_CALLBACK (on_permission_request), NULL);
  gtk_window_present (demo.window);
  if (demo.smoke)
    g_timeout_add_seconds (90, smoke_finish, NULL);
}

static int
on_local_options (GApplication *app, GVariantDict *options, gpointer data)
{
  if (g_variant_dict_contains (options, "smoke-test"))
    demo.smoke = TRUE;
  return -1;
}

int
main (int argc, char **argv)
{
  int status;

  demo.config = demo_config_new_default ();
  demo.session = soup_session_new_with_options ("timeout", 12, NULL);
  demo.app = gtk_application_new (APP_ID, G_APPLICATION_NON_UNIQUE);
  g_application_add_main_option (G_APPLICATION (demo.app), "smoke-test", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
                                 "Open the window, load the configured chat, print the outcome and exit", NULL);
  g_signal_connect (demo.app, "handle-local-options", G_CALLBACK (on_local_options), NULL);
  g_signal_connect (demo.app, "activate", G_CALLBACK (on_activate), NULL);
  status = g_application_run (G_APPLICATION (demo.app), argc, argv);
  uninstall_sdk ();
  g_object_unref (demo.app);
  g_object_unref (demo.session);
  demo_config_free (demo.config);
  demo_config_free (demo.applied);
  demo_validated_free (demo.valid);
  return status != 0 ? status : demo.exit_code;
}
