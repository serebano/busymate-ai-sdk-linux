/* Busymate AI SDK for Linux — the WebKit-free pipe, shared by the library and its unit tests.
 */
#pragma once

#define BUSYMATE_INSIDE
#include "busymate/busymate-core.h"
#undef BUSYMATE_INSIDE

#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define BUSYMATE_MAX_ENVELOPE (64 * 1024)
#define BUSYMATE_MAX_MICROPHONE 4096
#define BUSYMATE_MINT_CEILING_MS 15000

typedef struct {
  const char         *assistant;
  const char * const *origins;      /* normalized allow-list */
  const char         *app_id;
  const char         *app_version;  /* nullable */
  const char         *app_build;    /* nullable */
  const char         *webview;      /* nullable */
  const char         *transport;
} BusymateCoreConfig;

/* Returns a newly allocated account id or NULL (signed out). */
typedef char *(*BusymateAccountFunc) (gpointer user_data);

typedef enum {
  BUSYMATE_ROUTE_IGNORE,  /* not ours: no reply at all */
  BUSYMATE_ROUTE_REPLY,   /* *reply holds the complete reply envelope */
  BUSYMATE_ROUTE_MINT,    /* host must mint; *call is set */
  BUSYMATE_ROUTE_OPEN,    /* host must open call->url */
  BUSYMATE_ROUTE_ACTION,  /* host must run call->name */
} BusymateRoute;

typedef struct {
  JsonNode   *id;          /* echoed verbatim */
  char       *op;
  char       *origin;      /* normalized, allowed sender origin */
  JsonObject *request;     /* mint: the chat's p + assistant + origin */
  char       *url;         /* open */
  char       *mode;        /* open: external | auth */
  char       *name;        /* action */
  char       *params_json; /* action params, a JSON object */
} BusymateCall;

void          busymate_call_free        (BusymateCall *call);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (BusymateCall, busymate_call_free)

BusymateRoute busymate_core_handle      (const BusymateCoreConfig *config,
                                         const char               *raw,
                                         const char               *sender_origin,
                                         BusymateAccountFunc       account,
                                         gpointer                  account_data,
                                         char                    **reply,
                                         BusymateCall            **call);

char         *busymate_core_reply_ok    (JsonNode   *id,
                                         JsonObject *r);
char         *busymate_core_reply_error (JsonNode   *id,
                                         const char *error);
char         *busymate_core_event       (const char *ev,
                                         JsonObject *r);
JsonObject   *busymate_core_state       (const char *app_id,
                                         const char *account);
JsonObject   *busymate_core_hello       (const BusymateCoreConfig *config,
                                         const char               *origin);

/* Isolated-world transport wrapper: {"o":origin,"f":frame,"d":data} or {"o","f","poll":true}. */
gboolean      busymate_transport_parse  (const char *raw,
                                         char      **origin,
                                         char      **frame,
                                         char      **data,
                                         gboolean   *poll);

/* Microphone wire 1. */
gboolean      busymate_microphone_parse (const char         *raw,
                                         const char         *sender_origin,
                                         const char * const *allowed,
                                         char              **id,
                                         char              **source);
char         *busymate_microphone_reply (const char *id,
                                         gboolean    granted);
char        **busymate_microphone_allowed (const char * const *origins);
gboolean      busymate_microphone_media_allowed (const char * const *allowed,
                                                 const char         *top_uri,
                                                 gboolean            audio,
                                                 gboolean            video,
                                                 gboolean            display,
                                                 gboolean            granted);

/* Page scripts (see busymate-scripts.c). */
char         *busymate_script_isolated  (const char *name,
                                         gboolean    poll);
char         *busymate_script_page      (const char *name);
char        **busymate_script_allow_list (const char * const *origins);

G_END_DECLS
