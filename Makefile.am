comma := ,
empty :=
space := $(empty) $(empty)

if WAYHOUSE_USE_GIT_VERSION
# First, check if we have an exact match for a tag
WAYHOUSE_GIT_EXACT_VERSION := $(shell git describe --tags --match 'v*' --exact-match --dirty 2>/dev/null | sed -e 's:^v::')
WAYHOUSE_VERSION := $(if $(WAYHOUSE_GIT_EXACT_VERSION),$(WAYHOUSE_GIT_EXACT_VERSION),$(VERSION) - $(shell git describe --tags --always --dirty) ($(shell git describe --tags --always --all | sed -e 's:heads/::')))
else # ! WAYHOUSE_USE_GIT_VERSION
WAYHOUSE_VERSION = $(VERSION)
endif # ! WAYHOUSE_USE_GIT_VERSION

AM_DISTCHECK_CONFIGURE_FLAGS = \
	--with-westonplugindir="$$dc_install_base/$(westonplugindir)" \
	$(null)

ACLOCAL_AMFLAGS = -I m4 -I src/libnkutils -I src/libgwater ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory

bin_PROGRAMS =
noinst_LTLIBRARIES =
check_PROGRAMS =

pkginclude_HEADERS =

TESTS =

EXTRA_DIST = \
	autogen.sh \
	README.md \
	$(null)
CLEANFILES =


AM_CPPFLAGS += \
	-I $(srcdir)/src/ \
	-I $(builddir)/src/ \
	-D WAYHOUSE_VERSION='"$(WAYHOUSE_VERSION)"' \
	$(null)

AM_CFLAGS += \
	$(WESTON_CFLAGS) \
	$(null)


include compositor/compositor.mk
include libwhclient/libwhclient.mk
include dock/dock.mk


include src/libnkutils/libnkutils.mk
include src/libgwater/wayland-server.mk
include src/libgwater/wayland.mk


# Wayland protocol code generation rules
src/%-protocol.c: $(wwprotocoldir)/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(WAYLAND_SCANNER) code < $< > $@

src/%-server-protocol.h: $(wwprotocoldir)/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(WAYLAND_SCANNER) server-header < $< > $@

src/%-client-protocol.h: $(wwprotocoldir)/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(WAYLAND_SCANNER) client-header < $< > $@

src/%-protocol.c: $(wpprotocoldir)/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(WAYLAND_SCANNER) code < $< > $@

src/%-server-protocol.h: $(wpprotocoldir)/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(WAYLAND_SCANNER) server-header < $< > $@

src/%-client-protocol.h: $(wpprotocoldir)/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(WAYLAND_SCANNER) client-header < $< > $@

-include local-rules.mk
