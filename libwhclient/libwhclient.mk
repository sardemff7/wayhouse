AM_CFLAGS += \
	-I $(srcdir)/%D%/include \
	$(null)

noinst_LTLIBRARIES += \
	libwhclient.la \
	$(null)


libwhclient_la_SOURCES = \
	%D%/src/client.c \
	$(null)

libwhclient_la_CFLAGS = \
	$(AM_CFLAGS) \
	-D G_LOG_DOMAIN=\"libwhclient\" \
	$(PANGO_CFLAGS) \
	$(CAIRO_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GW_WAYLAND_CFLAGS) \
	$(NKUTILS_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

libwhclient_la_LIBADD = \
	$(PANGO_LIBS) \
	$(CAIRO_LIBS) \
	$(GOBJECT_LIBS) \
	$(GW_WAYLAND_LIBS) \
	$(NKUTILS_LIBS) \
	$(GLIB_LIBS) \
	$(null)
