subdir('busymate')

core_sources = files('busymate-core.c', 'busymate-scripts.c')
sdk_sources = files('busymate-bridge.c', 'busymate-microphone.c')

# The WebKit-free pipe, linked into the library and the unit tests.
core_lib = static_library('busymate-core', core_sources,
  dependencies: [glib_dep, gio_dep, json_dep],
  c_args: ['-DBUSYMATE_COMPILATION'],
  pic: true)
core_dep = declare_dependency(link_with: core_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gio_dep, json_dep])

sdk_deps = [glib_dep, gobject_dep, gio_dep, json_dep, gtk_dep, webkit_dep]
sdk_lib = library('busymate-ai-sdk-@0@'.format(api_version), sdk_sources,
  link_whole: core_lib,
  dependencies: sdk_deps,
  c_args: ['-DBUSYMATE_COMPILATION', '-DG_LOG_DOMAIN="Busymate"'],
  version: meson.project_version(),
  install: true)

sdk_inc = include_directories('.')
busymate_dep = declare_dependency(link_with: sdk_lib,
  include_directories: sdk_inc,
  dependencies: sdk_deps)
meson.override_dependency('busymate-ai-sdk-@0@'.format(api_version), busymate_dep)

pkg.generate(sdk_lib,
  name: 'busymate-ai-sdk-@0@'.format(api_version),
  description: 'Busymate AI SDK for Linux (GTK4 + WebKitGTK 6.0): identity bridge v2 and microphone adapter',
  subdirs: 'busymate-ai-sdk-@0@'.format(api_version),
  requires: ['gtk4', 'webkitgtk-6.0'],
  requires_private: ['json-glib-1.0'])

gir_dep = dependency('gobject-introspection-1.0', required: get_option('introspection'))
if gir_dep.found()
  gnome.generate_gir(sdk_lib,
    sources: [public_headers, version_h, core_sources, sdk_sources],
    namespace: 'Busymate',
    nsversion: '1.0',
    identifier_prefix: 'Busymate',
    symbol_prefix: 'busymate',
    includes: ['GObject-2.0', 'Gtk-4.0', 'WebKit-6.0'],
    header: 'busymate/busymate.h',
    export_packages: 'busymate-ai-sdk-@0@'.format(api_version),
    extra_args: ['-DBUSYMATE_COMPILATION', '--warn-all'],
    install: true)
endif
