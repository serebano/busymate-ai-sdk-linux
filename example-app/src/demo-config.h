/* Example app settings, validation and the authenticated-backend mint client.
 * These are the same functions the app runs; tests/test-demo-config.c tests them.
 */
#pragma once

#include <libsoup/soup.h>

G_BEGIN_DECLS

#define DEMO_DEFAULT_ASSISTANT "busyproxy"
#define DEMO_DEFAULT_CHAT_URL "https://busymate.ai/support/busyproxy?channel=desktop_sdk&locale=en"
#define DEMO_DEFAULT_LOCALE "en"
#define DEMO_DEFAULT_ORIGINS "https://busymate.ai"

typedef struct {
  char     *assistant;
  char     *chat_url;
  char     *locale;
  char     *origins;              /* comma or newline separated exact https origins */
  gboolean  signed_in;            /* identified mode: use the mint callback */
  char     *account_id;           /* NOT a login: the value the app's account reader returns */
  char     *mint_url;             /* your authenticated HTTPS mint endpoint */
  char     *backend_session_token;/* your existing backend session credential, memory only */
  gboolean  microphone_enabled;
  gboolean  handle_close_action;
  gboolean  close_callback_enabled;
} DemoConfig;

typedef struct {
  char  *assistant;
  char  *chat_url;                /* with the locale applied */
  char **origins;                 /* normalized exact origins */
  char  *mint_url;                /* nullable */
} DemoValidated;

typedef enum {
  DEMO_CONFIG_ERROR_ASSISTANT,
  DEMO_CONFIG_ERROR_CHAT_URL,
  DEMO_CONFIG_ERROR_LOCALE,
  DEMO_CONFIG_ERROR_ORIGIN,
  DEMO_CONFIG_ERROR_MISSING_ORIGIN,
  DEMO_CONFIG_ERROR_ACCOUNT,
  DEMO_CONFIG_ERROR_BACKEND,
} DemoConfigError;

#define DEMO_CONFIG_ERROR (demo_config_error_quark ())
GQuark         demo_config_error_quark (void);

DemoConfig    *demo_config_new_default (void);
DemoConfig    *demo_config_copy        (const DemoConfig *config);
void           demo_config_free        (DemoConfig *config);
DemoValidated *demo_config_validate    (const DemoConfig *config, GError **error);
void           demo_validated_free     (DemoValidated *validated);
char          *demo_secure_url_origin  (const char *url);
char          *demo_url_with_locale    (const char *url, const char *locale);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DemoConfig, demo_config_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (DemoValidated, demo_validated_free)

/* The actual callback factory used by the demo: which SDK callbacks to connect. */
typedef struct {
  gboolean connect_action;  /* handles `close` only; `ready` is logged, others unhandled */
  gboolean connect_close;
} DemoCallbacks;
DemoCallbacks  demo_callbacks_for      (const DemoConfig *config);
gboolean       demo_action_handles     (const char *name);

/* Mint: request body, backend response mapping and the HTTP call. */
typedef enum {
  DEMO_MINT_TOKEN,
  DEMO_MINT_NOT_SIGNED_IN,
  DEMO_MINT_FAILED,
} DemoMintOutcome;

char           *demo_mint_body         (const char *nonce, const char *aud, const char *reason,
                                        const char *assistant, const char *origin);
DemoMintOutcome demo_mint_decode       (guint status, const char *body, gsize length,
                                        const char *nonce, char **token);
SoupMessage    *demo_mint_message      (const char *url, const char *body, const char *session_token,
                                        GError **error);
void            demo_mint_send         (SoupSession *session, const char *url, const char *body,
                                        const char *session_token, const char *nonce,
                                        GCancellable *cancellable, GAsyncReadyCallback callback,
                                        gpointer user_data);
DemoMintOutcome demo_mint_send_finish  (GAsyncResult *result, char **token);

G_END_DECLS
