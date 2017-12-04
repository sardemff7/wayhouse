executable('wayhouse', files(
    'src/wayhouse.c',
    'src/types.h',
    'src/config_.h',
    'src/config.c',
    'src/commands.h',
    'src/commands.c',
    'src/seats.h',
    'src/seats.c',
    'src/outputs.h',
    'src/outputs.c',
    'src/containers.h',
    'src/containers.c',
    'src/xwayland.h',
    'src/xwayland.c',
    ),
    c_args: [
        '-DG_LOG_DOMAIN="wayhouse"'
    ],
    dependencies: [ libweston_desktop, libweston, xkbcommon, libinput, libgwater_wayland_server, wayland_server, libnkutils, gmodule, gobject, glib ],
    install: true,
)
