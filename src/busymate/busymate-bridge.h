/* Busymate AI SDK for Linux — identity bridge v2 (`busymate.bridge/2`, build 2.0.0).
 */
#pragma once

#if !defined(BUSYMATE_INSIDE) && !defined(BUSYMATE_COMPILATION)
#error "Only <busymate/busymate.h> can be included directly."
#endif

#include <webkit/webkit.h>

G_BEGIN_DECLS

#define BUSYMATE_TYPE_MINT_REQUEST (busymate_mint_request_get_type ())
G_DECLARE_FINAL_TYPE (BusymateMintRequest, busymate_mint_request, BUSYMATE, MINT_REQUEST, GObject)

const char *busymate_mint_request_get_nonce        (BusymateMintRequest *self);
const char *busymate_mint_request_get_audience     (BusymateMintRequest *self);
const char *busymate_mint_request_get_reason       (BusymateMintRequest *self);
const char *busymate_mint_request_get_assistant    (BusymateMintRequest *self);
const char *busymate_mint_request_get_origin       (BusymateMintRequest *self);
char       *busymate_mint_request_to_json          (BusymateMintRequest *self);
void        busymate_mint_request_return_token     (BusymateMintRequest *self,
                                                    const char          *token,
                                                    const char          *nonce);
void        busymate_mint_request_return_not_signed_in (BusymateMintRequest *self);
void        busymate_mint_request_return_failed    (BusymateMintRequest *self);
gboolean    busymate_mint_request_is_finished      (BusymateMintRequest *self);

#define BUSYMATE_TYPE_BRIDGE (busymate_bridge_get_type ())
G_DECLARE_FINAL_TYPE (BusymateBridge, busymate_bridge, BUSYMATE, BRIDGE, GObject)

BusymateBridge     *busymate_bridge_new              (const char         *assistant,
                                                      const char * const *origins);
gboolean            busymate_bridge_install          (BusymateBridge     *self,
                                                      WebKitWebView      *web_view);
void                busymate_bridge_uninstall        (BusymateBridge     *self);
const char * const *busymate_bridge_get_allowed_origins (BusymateBridge  *self);
const char         *busymate_bridge_get_assistant    (BusymateBridge     *self);
void                busymate_bridge_account_changed  (BusymateBridge     *self);
void                busymate_bridge_resume           (BusymateBridge     *self);

G_END_DECLS
