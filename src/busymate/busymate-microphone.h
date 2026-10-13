/* Busymate AI SDK for Linux — microphone adapter 1.0.0 (microphone wire 1).
 */
#pragma once

#if !defined(BUSYMATE_INSIDE) && !defined(BUSYMATE_COMPILATION)
#error "Only <busymate/busymate.h> can be included directly."
#endif

#include <webkit/webkit.h>

G_BEGIN_DECLS

#define BUSYMATE_TYPE_MICROPHONE (busymate_microphone_get_type ())
G_DECLARE_FINAL_TYPE (BusymateMicrophone, busymate_microphone, BUSYMATE, MICROPHONE, GObject)

BusymateMicrophone *busymate_microphone_new                 (WebKitWebView      *web_view,
                                                             const char * const *origins);
gboolean            busymate_microphone_is_installed        (BusymateMicrophone *self);
const char * const *busymate_microphone_get_allowed_origins (BusymateMicrophone *self);
gboolean            busymate_microphone_get_granted         (BusymateMicrophone *self);
void                busymate_microphone_complete_permission (BusymateMicrophone *self,
                                                             gboolean            granted);
void                busymate_microphone_revoke              (BusymateMicrophone *self);
void                busymate_microphone_uninstall           (BusymateMicrophone *self);

G_END_DECLS
