executable('wh-dock', files(
        'src/dock.c',
    ) + [
        wayland_scanner_client.process(join_paths(ww_protocol_dir, 'unstable', 'dock-manager', 'dock-manager-unstable-v2.xml')),
        wayland_scanner_code.process(join_paths(ww_protocol_dir, 'unstable', 'dock-manager', 'dock-manager-unstable-v2.xml')),
    ],
    c_args: [
        '-DG_LOG_DOMAIN="wh-dock"',
    ],
    dependencies: [ pango, cairo, libwhclient, libgwater_wayland, gio_platform, gio, libnkutils, glib ],
    install: true,
)
