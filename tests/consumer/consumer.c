/* Builds against the INSTALLED SDK (pkg-config busymate-ai-sdk-1), as a consumer would. */
#include <busymate/busymate.h>

int
main (int argc, char **argv)
{
  const char *origins[] = { "https://support.acme.com", NULL };
  g_autoptr(BusymateBridge) bridge = busymate_bridge_new ("acme", origins);
  const char * const *allowed = busymate_bridge_get_allowed_origins (bridge);

  g_print ("Busymate SDK %s, bridge build %s, microphone adapter %s\n",
           BUSYMATE_SDK_VERSION, BUSYMATE_BRIDGE_BUILD, BUSYMATE_MICROPHONE_ADAPTER);
  g_assert_cmpstr (allowed[0], ==, "https://acme.busymate.ai");
  g_assert_cmpstr (allowed[1], ==, "https://support.acme.com");
  g_assert_null (allowed[2]);
  return 0;
}
