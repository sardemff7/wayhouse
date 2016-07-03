bin_PROGRAMS += \
	wh-dock \
	$(null)
CLEANFILES += \
	$(nodist_wh_dock_SOURCES) \
	$(null)

wh_dock_SOURCES = \
	%D%/src/dock.c \
	$(null)

nodist_wh_dock_SOURCES = \
	src/unstable/dock-manager/dock-manager-unstable-v2-protocol.c \
	src/unstable/dock-manager/dock-manager-unstable-v2-client-protocol.h \
	$(null)

wh_dock_CFLAGS = \
	$(AM_CFLAGS) \
	-I $(srcdir)/src/unstable/dock-manager/ \
	-D G_LOG_DOMAIN=\"wh-dock\" \
	$(PANGO_CFLAGS) \
	$(CAIRO_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GW_WAYLAND_CFLAGS) \
	$(NKUTILS_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

wh_dock_LDADD = \
	libwhclient.la \
	$(PANGO_LIBS) \
	$(CAIRO_LIBS) \
	$(GOBJECT_LIBS) \
	$(GIO_LIBS) \
	$(GW_WAYLAND_LIBS) \
	$(NKUTILS_LIBS) \
	$(GLIB_LIBS) \
	$(null)

wh-dock src/wh_dock-dock.o: src/unstable/dock-manager/dock-manager-unstable-v2-client-protocol.h
