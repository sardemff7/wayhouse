libwhclient_inc = include_directories('include')
libwhclient_deps = [ pango, cairo, libgwater_wayland, wayland_client, libnkutils, glib ]
libwhclient_lib = static_library('libwhclient', files(
        'src/client.c'
    ),
    c_args: [
        '-DG_LOG_DOMAIN="libwhclient"'
    ],
    include_directories: libwhclient_inc,
    dependencies: libwhclient_deps,
)

libwhclient = declare_dependency(link_with: libwhclient_lib, include_directories: libwhclient_inc, dependencies: libwhclient_deps)
