project('wayhouse', 'c',
    version: '0',
    meson_version: '>=0.39.1',
    license: [ 'GPL3+', 'MIT' ],
    default_options: [
        'c_std=gnu11',
        'warning_level=2',
    ],
)

wayhouse_version = meson.project_version()

git = find_program('git', required: false)
if git.found()
	git_version = run_command(git, '--git-dir', join_paths(meson.source_root(), '.git'), 'describe', '--tags', '--always', '--dirty')
	git_branch = run_command(git, '--git-dir', join_paths(meson.source_root(), '.git'), 'describe', '--tags', '--always', '--all')
	if git_version.returncode() == 0 and git_branch.returncode() == 0
		git_branch_parts = []
		foreach b : git_branch.stdout().strip().split('/')
			if b != 'heads'
				git_branch_parts += b
			endif
		endforeach
		wayhouse_version = '@0@ - @1@ (@2@)'.format(meson.project_version(), git_version.stdout().strip(), '/'.join(git_branch_parts))
	endif
endif

is_unix = host_machine.system() != 'windows'

headers = [
    'locale.h',
    'errno.h',
    'sys/mman.h',
    'stdbool.h',
    'sys/stat.h',
    'fcntl.h',
]
c_compiler = meson.get_compiler('c')
foreach h : headers
    if not c_compiler.has_header(h)
        error('Header @0@ was not found, but is required'.format(h))
    endif
endforeach

glib_min_major='2'
glib_min_minor='40'
glib_min_version='.'.join([glib_min_major, glib_min_minor])
wayland_min_version='1.12.92'
weston_supported_majors = [
    '3',
    '2',
]
last_weston_major = weston_supported_majors[weston_supported_majors.length() - 1]
weston_min_version='1.12.90'

glib = dependency('glib-2.0', version: '>= @0@'.format(glib_min_version))
gobject = dependency('gobject-2.0')
gmodule = dependency('gmodule-2.0')
gio = dependency('gio-2.0')
if is_unix
    gio_platform = dependency('gio-unix-2.0')
else
    gio_platform = dependency('gio-windows-2.0')
endif
wayland_server = dependency('wayland-server', version: '>= @0@'.format(wayland_min_version))
wayland_client = [
    dependency('wayland-client', version: '>= @0@'.format(wayland_min_version)),
    dependency('wayland-cursor'),
]
libgwater_wayland = subproject('libgwater/wayland').get_variable('libgwater_wayland')
libgwater_wayland_server = subproject('libgwater/wayland-server').get_variable('libgwater_wayland_server')
weston_major = ''
foreach try_weston_major : weston_supported_majors
    if weston_major == ''
        libweston = dependency('libweston-@0@'.format(try_weston_major), required: last_weston_major == try_weston_major)
        libweston_desktop = dependency('libweston-desktop-@0@'.format(try_weston_major), required: last_weston_major == try_weston_major)
        if libweston.found()
            weston_major = try_weston_major
        endif
    endif
endforeach
weston = dependency('weston')
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')
cairo = dependency('cairo')
pango = [
    dependency('pango'),
    dependency('pangocairo'),
]

header_conf = configuration_data()
header_conf.set_quoted('PACKAGE_NAME', meson.project_name())
header_conf.set_quoted('WAYHOUSE_VERSION', wayhouse_version)
header_conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())

header_conf.set('WAYHOUSE_EXPORT', '__attribute__((visibility("default")))')

header_conf.set('GLIB_VERSION_MIN_REQUIRED', '(G_ENCODE_VERSION(@0@,@1@))'.format(glib_min_major, glib_min_minor))
header_conf.set('G_LOG_USE_STRUCTURED', true)

header_conf.set_quoted('WAYHOUSE_LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
header_conf.set_quoted('WESTON_PLUGINS_DIR', join_paths(weston.get_pkgconfig_variable('libdir'), 'weston'))
header_conf.set_quoted('LIBWESTON_PLUGINS_DIR', join_paths(libweston.get_pkgconfig_variable('libdir'), 'libweston-@0@'.format(weston_major)))

header_conf.set('WAYHOUSE_DEBUG', get_option('enable-debug'))

config_h = configure_file(output: 'config.h', configuration: header_conf)

add_project_arguments(
    '-fvisibility=hidden',
    '-I@0@'.format(meson.build_root()),
    language: 'c'
)
flags = [
    '-Wformat=2',
    '-Wno-unused-parameter',
]
foreach f : flags
    if c_compiler.has_argument(f)
        add_project_arguments(f, language: 'c')
    endif
endforeach

nk_modules = [
    'enum=true',
    'colour=true',
    'bindings=true',
]
nk = subproject('libnkutils', default_options: nk_modules)
nk_options = nk.get_variable('nk_options')
foreach o : nk_modules + nk_options
    if not nk_modules.contains(o) or not nk_options.contains(o)
        error('You must not change libnkutils options @0@ != @1@'.format('|'.join(nk_modules), '|'.join(nk_options)))
    endif
endforeach
libnkutils = nk.get_variable('libnkutils')

wayland_scanner = find_program('wayland-scanner')
wayland_protocols = dependency('wayland-protocols')
wayland_wall = dependency('wayland-wall')
wp_protocol_dir = wayland_protocols.get_pkgconfig_variable('pkgdatadir')
ww_protocol_dir = wayland_wall.get_pkgconfig_variable('pkgdatadir')

wayland_scanner_client = generator(wayland_scanner, output: '@BASENAME@-client-protocol.h', arguments: ['client-header', '@INPUT@', '@OUTPUT@'])
wayland_scanner_server = generator(wayland_scanner, output: '@BASENAME@-server-protocol.h', arguments: ['server-header', '@INPUT@', '@OUTPUT@'])
wayland_scanner_code = generator(wayland_scanner, output: '@BASENAME@-protocol.c', arguments: ['code', '@INPUT@', '@OUTPUT@'])

subdir('compositor')
subdir('libwhclient')
subdir('dock')
