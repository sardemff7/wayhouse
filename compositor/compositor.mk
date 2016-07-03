bin_PROGRAMS += \
	wayhouse \
	$(null)


wayhouse_SOURCES = \
	%D%/src/wayhouse.c \
	%D%/src/types.h \
	%D%/src/config_.h \
	%D%/src/config.c \
	%D%/src/commands.h \
	%D%/src/commands.c \
	%D%/src/seats.h \
	%D%/src/seats.c \
	%D%/src/outputs.h \
	%D%/src/outputs.c \
	%D%/src/containers.h \
	%D%/src/containers.c \
	%D%/src/xwayland.h \
	%D%/src/xwayland.c \
	$(null)

wayhouse_CFLAGS = \
	$(AM_CFLAGS) \
	-D G_LOG_DOMAIN=\"wayhouse\" \
	-D LIBWESTON_MODULE_DIR=\"$(libwestonmoduledir)\" \
	-D WESTON_MODULE_DIR=\"$(westonmoduledir)\" \
	$(WESTON_DESKTOP_CFLAGS) \
	$(XKBCOMMON_CFLAGS) \
	$(LIBINPUT_CFLAGS) \
	$(WESTON_CFLAGS) \
	$(GMODULE_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GW_WAYLAND_SERVER_CFLAGS) \
	$(NKUTILS_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

wayhouse_LDADD = \
	$(WESTON_DESKTOP_LIBS) \
	$(XKBCOMMON_LIBS) \
	$(LIBINPUT_LIBS) \
	$(WESTON_LIBS) \
	$(GMODULE_LIBS) \
	$(GOBJECT_LIBS) \
	$(GW_WAYLAND_SERVER_LIBS) \
	$(NKUTILS_LIBS) \
	$(GLIB_LIBS) \
	$(null)
