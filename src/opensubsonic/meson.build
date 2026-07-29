opensubsonic_sources = [
    'plugin.cc',
    'settings.cc',
    'api.cc',
    'browser.cc',
    'dialogs.cc',
]

qt_network_dep = dependency('Qt6Network', required: true)

if get_option('qt5')
  qt_module = import('qt5')
else
  qt_module = import('qt6')
endif

opensubsonic_moc = qt_module.compile_moc(
    headers: ['api.h', 'browser.h', 'dialogs.h'],
    dependencies: [audacious_dep, qt_dep, qt_network_dep]
)

opensubsonic = shared_module('opensubsonic',
    opensubsonic_sources + opensubsonic_moc,
    dependencies: [audacious_dep, qt_dep, qt_network_dep],
    name_prefix: '',
    install: true,
    install_dir: general_plugin_dir
)
