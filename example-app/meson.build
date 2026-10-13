soup_dep = dependency('libsoup-3.0', version: '>= 3.0')

demo_lib = static_library('demo-config', 'src/demo-config.c',
  dependencies: [glib_dep, gio_dep, json_dep, soup_dep])
demo_dep = declare_dependency(link_with: demo_lib,
  include_directories: include_directories('src'),
  dependencies: [glib_dep, gio_dep, json_dep, soup_dep])

example = executable('busymate-sdk-example', 'src/main.c',
  dependencies: [busymate_dep, demo_dep],
  install: false)

if get_option('tests')
  test_demo = executable('test-demo-config', 'tests/test-demo-config.c',
    dependencies: [demo_dep])
  test('demo-config', test_demo, suite: ['unit'], protocol: 'tap', args: ['--tap'])
endif
