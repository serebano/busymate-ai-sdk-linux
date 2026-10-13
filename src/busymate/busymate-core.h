/* Busymate AI SDK for Linux — shared constants and origin rules.
 */
#pragma once

#if !defined(BUSYMATE_INSIDE) && !defined(BUSYMATE_COMPILATION)
#error "Only <busymate/busymate.h> can be included directly."
#endif

#include <glib.h>

G_BEGIN_DECLS

/** BUSYMATE_BRIDGE_NAME: handler and page object name of the identity bridge. */
#define BUSYMATE_BRIDGE_NAME "BusymateBridge"
/** BUSYMATE_BRIDGE_WIRE: identity wire version (`busymate.bridge/2`). */
#define BUSYMATE_BRIDGE_WIRE 2
/** BUSYMATE_BRIDGE_BUILD: frozen identity bridge build. */
#define BUSYMATE_BRIDGE_BUILD "2.0.0"
/** BUSYMATE_MICROPHONE_NAME: handler and page object name of the microphone adapter. */
#define BUSYMATE_MICROPHONE_NAME "BusymateMicrophone"
/** BUSYMATE_MICROPHONE_WIRE: microphone wire version. */
#define BUSYMATE_MICROPHONE_WIRE 1
/** BUSYMATE_MICROPHONE_ADAPTER: microphone adapter version. */
#define BUSYMATE_MICROPHONE_ADAPTER "1.0.0"
/** BUSYMATE_MICROPHONE_REQUEST_TYPE: the only accepted microphone request type. */
#define BUSYMATE_MICROPHONE_REQUEST_TYPE "busymate.microphone.v1.request"
/** BUSYMATE_SCRIPT_WORLD: isolated WebKit script world that reads each frame's real origin. */
#define BUSYMATE_SCRIPT_WORLD "busymate"
/** BUSYMATE_PLATFORM: `platform` reported in `hello`. */
#define BUSYMATE_PLATFORM "linux"
/** BUSYMATE_CHANNEL: hosted chat `channel` query value for desktop hosts. */
#define BUSYMATE_CHANNEL "desktop"
/** BUSYMATE_DEFAULT_CHAT_URL: the public guest support chat used by the example. */
#define BUSYMATE_DEFAULT_CHAT_URL "https://busymate.ai/support/busyproxy?channel=desktop&locale=en"

char  *busymate_normalize_origin      (const char         *origin);
char **busymate_allowed_origins       (const char         *assistant,
                                       const char * const *origins);
char  *busymate_microphone_origin     (const char         *origin);
char  *busymate_origin_of_uri         (const char         *uri);
char  *busymate_account_key           (const char         *app_id,
                                       const char         *account);
gboolean busymate_origin_in_list      (const char         *origin,
                                       const char * const *allowed);

G_END_DECLS
