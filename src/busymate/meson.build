version_conf = configuration_data()
version_conf.set('VERSION', meson.project_version())
version_h = configure_file(input: 'busymate-version.h.in', output: 'busymate-version.h', configuration: version_conf)

public_headers = files('busymate.h', 'busymate-core.h', 'busymate-bridge.h', 'busymate-microphone.h')
install_headers(public_headers, version_h, subdir: 'busymate-ai-sdk-@0@/busymate'.format(api_version))
