project('busymate-ai-sdk-linux', 'c',
  version: '1.0.0',
  meson_version: '>= 1.0.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'werror=false'])

api_version = '1'
gnome = import('gnome')
pkg = import('pkgconfig')
cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
  '-Wno-unused-parameter',
  '-Wno-missing-field-initializers',
  '-Werror=implicit-function-declaration',
  '-Werror=incompatible-pointer-types',
  '-Werror=int-conversion',
  '-Werror=return-type',
]), language: 'c')

glib_dep = dependency('glib-2.0', version: '>= 2.76')
gobject_dep = dependency('gobject-2.0', version: '>= 2.76')
gio_dep = dependency('gio-2.0', version: '>= 2.76')
json_dep = dependency('json-glib-1.0', version: '>= 1.6')
gtk_dep = dependency('gtk4', version: '>= 4.10')
webkit_dep = dependency('webkitgtk-6.0', version: '>= 2.42')

subdir('src')
if get_option('tests')
  subdir('tests')
endif
if get_option('example')
  subdir('example-app')
endif
