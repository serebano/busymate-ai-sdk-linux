/* Busymate AI SDK for Linux — the two document-start scripts behind each pipe.
 *
 * WebKitGTK does not tell the UI process which frame posted a script message.
 * So the native handler lives in an ISOLATED script world ("busymate"), where
 * the page's own scripts cannot reach it, and a tiny script in that world reads
 * the sending frame's real `location.origin` and forwards the envelope together
 * with it. The page sees only an Electron/Android-shaped object
 * (`postMessage` + `addEventListener("message")`); envelopes cross between the
 * two worlds as plain strings in DOM CustomEvents of the same document.
 * Events the app sends by itself (resume/state/restored) reach the frame that
 * last said `hello` through a held reply ("poll"), so no frame is guessed.
 */
#define BUSYMATE_COMPILATION
#include "busymate-core-private.h"

static const char isolated_template[] =
  "(function () {\n"
  "  var NAME = '@NAME@', POLL = @POLL@;\n"
  "  var handler = window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers[NAME];\n"
  "  if (!handler || typeof handler.postMessage !== 'function') return;\n"
  "  var bytes = new Uint8Array(16);\n"
  "  crypto.getRandomValues(bytes);\n"
  "  var frame = Array.prototype.map.call(bytes, function (b) { return (b + 256).toString(16).slice(1); }).join('');\n"
  "  var polling = false;\n"
  "  function deliver(value) {\n"
  "    if (typeof value !== 'string' || !value) return false;\n"
  "    document.dispatchEvent(new CustomEvent('busymate:' + NAME + ':in', { detail: value }));\n"
  "    return true;\n"
  "  }\n"
  "  function ask(body) {\n"
  "    body.o = String(location.origin);\n"
  "    body.f = frame;\n"
  "    return Promise.resolve(handler.postMessage(JSON.stringify(body)));\n"
  "  }\n"
  "  function poll() {\n"
  "    polling = true;\n"
  "    ask({ poll: true }).then(function (value) { if (deliver(value)) poll(); else polling = false; },\n"
  "                             function () { polling = false; });\n"
  "  }\n"
  "  document.addEventListener('busymate:' + NAME + ':out', function (event) {\n"
  "    var data = event.detail;\n"
  "    if (typeof data !== 'string') return;\n"
  "    ask({ d: data }).then(deliver, function () {});\n"
  "    if (POLL && !polling) poll();\n"
  "  }, true);\n"
  "})();\n";

static const char page_template[] =
  "(function () {\n"
  "  var NAME = '@NAME@';\n"
  "  if (Object.prototype.hasOwnProperty.call(window, NAME)) return;\n"
  "  var listeners = [];\n"
  "  document.addEventListener('busymate:' + NAME + ':in', function (event) {\n"
  "    var data = event.detail;\n"
  "    if (typeof data !== 'string') return;\n"
  "    listeners.slice().forEach(function (listener) { try { listener({ data: data }); } catch (_) {} });\n"
  "  }, true);\n"
  "  var port = Object.freeze({\n"
  "    postMessage: function (value) {\n"
  "      document.dispatchEvent(new CustomEvent('busymate:' + NAME + ':out', { detail: String(value) }));\n"
  "    },\n"
  "    addEventListener: function (type, listener) {\n"
  "      if (type === 'message' && typeof listener === 'function' && listeners.indexOf(listener) < 0) listeners.push(listener);\n"
  "    },\n"
  "    removeEventListener: function (type, listener) {\n"
  "      var index = listeners.indexOf(listener);\n"
  "      if (index >= 0) listeners.splice(index, 1);\n"
  "    }\n"
  "  });\n"
  "  try { Object.defineProperty(window, NAME, { value: port, configurable: false, enumerable: false, writable: false }); } catch (_) {}\n"
  "})();\n";

static char *
fill (const char *template, const char *name, gboolean poll)
{
  GString *script = g_string_new (template);

  g_string_replace (script, "@NAME@", name, 0);
  g_string_replace (script, "@POLL@", poll ? "true" : "false", 0);
  return g_string_free (script, FALSE);
}

char *
busymate_script_isolated (const char *name, gboolean poll)
{
  return fill (isolated_template, name, poll);
}

char *
busymate_script_page (const char *name)
{
  return fill (page_template, name, FALSE);
}

/* WebKit user-script URL patterns do not carry ports: match the host, check the exact origin natively. */
char **
busymate_script_allow_list (const char * const *origins)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
  g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (gsize i = 0; origins != NULL && origins[i] != NULL; i++)
    {
      g_autoptr(GUri) uri = g_uri_parse (origins[i], G_URI_FLAGS_NONE, NULL);
      char *pattern;

      if (uri == NULL || g_uri_get_host (uri) == NULL)
        continue;
      pattern = g_strdup_printf ("https://%s/*", g_uri_get_host (uri));
      if (g_hash_table_add (seen, pattern))
        g_strv_builder_add (builder, pattern);
    }
  return g_strv_builder_end (builder);
}
