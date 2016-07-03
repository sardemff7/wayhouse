tAC_PREREQ([2.65])
AC_INIT([wayhouse],[0])

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_SRCDIR([compositor/src/wayhouse.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([.aux])

AM_INIT_AUTOMAKE([1.14 foreign subdir-objects parallel-tests dist-xz no-dist-gzip tar-ustar])
AM_SILENT_RULES([yes])

AM_CONDITIONAL([WAYHOUSE_USE_GIT_VERSION], [test -d "${srcdir}"/.git])

AC_ARG_VAR([WAYLAND_SCANNER], [Wayland XML scanner executable])

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_MKDIR_P
LT_INIT([disable-static pic-only])
PKG_PROG_PKG_CONFIG
AC_PATH_PROG([WAYLAND_SCANNER], [wayland-scanner])
if test -z "${WAYLAND_SCANNER}"; then
    AC_MSG_ERROR([wayland-scanner not found])
fi
AC_PROG_INSTALL

GETTEXT_PACKAGE=${PACKAGE_NAME}
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["${GETTEXT_PACKAGE}"], [Gettext Package])
AC_SUBST([GETTEXT_PACKAGE])
dnl IT_PROG_INTLTOOL([0.40.0])
dnl AM_GLIB_GNU_GETTEXT

AC_DEFINE([WH_EXPORT], [__attribute__((visibility("default")))], [Export symbol])

AC_CANONICAL_HOST
wh_os_unix=no
wh_os_win=no
case "${host_os}" in
    mingw*)
        wh_os_win=yes
        pkg_config_extra_args="--dont-define-prefix"
    ;;
    *)
        wh_os_unix=yes
        pkg_config_extra_args=
    ;;
esac
AM_CONDITIONAL([WH_OS_UNIX], [test x${wh_os_unix} = xyes])
AM_CONDITIONAL([WH_OS_WIN], [test x${wh_os_win} = xyes])

glib_min_major="2"
glib_min_minor="40"
glib_min_version="${glib_min_major}.${glib_min_minor}"
gio_min_version="${glib_min_version}"
wayland_min_version=1.12.92
m4_define([weston_supported_majors], [3, 2])
weston_major=3
weston_min_version=1.12.90

AM_CFLAGS="-fvisibility=hidden -Wall -Wextra -Wformat=2 -Wno-unused-parameter -Wno-missing-field-initializers -Wstrict-prototypes -Wmissing-prototypes -Wsign-compare"
AM_CPPFLAGS=""
AM_LDFLAGS=""
AM_DOCBOOK_CONDITIONS=""


AC_DEFUN([WH_CHECK_WESTON], [
    m4_ifnblank([$1], [
        m4_ifblank([$2], [
            PKG_CHECK_MODULES([WESTON], [libweston-$1 >= ${weston_min_version}], [weston_major=$1])
        ], [
            PKG_CHECK_MODULES([WESTON], [libweston-$1 >= ${weston_min_version}], [weston_major=$1], [WH_CHECK_WESTON(m4_shift($@))])
        ])
    ])
])

#
# Dependencies
#
AC_FUNC_MMAP
AC_CHECK_HEADERS([locale.h errno.h sys/mman.h stdbool.h sys/stat.h fcntl.h])
if test x${ac_cv_func_mmap_fixed_mapped} = xno; then
    AC_MSG_ERROR([*** mmap() function is required])
fi

PKG_CHECK_MODULES([GLIB], [glib-2.0 >= ${glib_min_version}])
AC_DEFINE_UNQUOTED([GLIB_VERSION_MIN_REQUIRED], [(G_ENCODE_VERSION(${glib_min_major},${glib_min_minor}))], [The lower GLib version supported])
gio_unix=
if test x${wh_os_unix} = xyes; then
    gio_unix="gio-unix-2.0"
fi
PKG_CHECK_MODULES([GIO], [gio-2.0 >= ${gio_min_version} ${gio_unix}])
AC_SUBST([gio_unix])
PKG_CHECK_MODULES([GOBJECT], [gobject-2.0])
PKG_CHECK_MODULES([GMODULE], [gmodule-2.0])

NK_INIT([enum colour/double colour/alpha])

GW_CHECK_WAYLAND_SERVER([wayland-server >= ${wayland_min_version}])
PKG_CHECK_MODULES([WAYLAND_SERVER], [wayland-server >= ${wayland_min_version}])
WH_CHECK_WESTON(weston_supported_majors)
PKG_CHECK_MODULES([XKBCOMMON], [xkbcommon])
PKG_CHECK_MODULES([LIBINPUT], [libinput])
PKG_CHECK_MODULES([WESTON_DESKTOP], [libweston-desktop-${weston_major} >= ${weston_min_version}])

GW_CHECK_WAYLAND([wayland-client >= ${wayland_min_version} wayland-cursor])
PKG_CHECK_MODULES([CAIRO], [cairo])
PKG_CHECK_MODULES([PANGO], [pango pangocairo])

save_CFLAGS=${CFLAGS}
save_LIBS=${LIBS}
CFLAGS=${WESTON_CFLAGS}
LIBS=${WESTON_LIBS}
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <compositor.h>]], [
    weston_compositor_load_module(NULL, "test.so");
])], [AC_DEFINE([LIBWESTON_HAS_COMMON_MODULES], [1], [Common modules support])])
CFLAGS=${save_CFLAGS}
LIBS=${save_LIBS}

libwestonmoduledir=`${PKG_CONFIG} --variable=libdir libweston-${weston_major}`/libweston-${weston_major}
westonmoduledir=`${PKG_CONFIG} --variable=libdir weston`/weston
AC_SUBST([libwestonmoduledir])
AC_SUBST([westonmoduledir])

wpprotocoldir=`${PKG_CONFIG} --variable=pkgdatadir wayland-protocols`
wwprotocoldir=`${PKG_CONFIG} --variable=pkgdatadir wayland-wall`
AC_SUBST([wpprotocoldir])
AC_SUBST([wwprotocoldir])


# Debug
AC_ARG_ENABLE([debug], AS_HELP_STRING([--disable-debug], [Disable debug output]), [], [enable_debug=yes])
if test x${enable_debug} = xyes; then
    AC_DEFINE([WAYHOUSE_DEBUG], [1], [Activate debug mode])
fi

AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_LDFLAGS])
AC_SUBST([AM_DOCBOOK_CONDITIONS])

AM_SUBST_NOTMAKE([LIBS])
AC_SUBST([shrext_cmds])

AC_CONFIG_FILES([
    Makefile
])
AC_OUTPUT
