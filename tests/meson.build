test_core = executable('test-core', 'test-core.c',
  dependencies: [core_dep])
test('core', test_core, suite: ['unit'], protocol: 'tap', args: ['--tap'])

test_webkit = executable('test-webkit', 'test-webkit.c',
  dependencies: [busymate_dep, json_dep, soup_dep])
test('webkit', test_webkit, suite: ['webkit'], protocol: 'tap', args: ['--tap'], timeout: 240)
