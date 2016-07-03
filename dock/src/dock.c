/*
 * WayHouse - A Wayland compositor based on libweston
 *
 * Copyright © 2016-2017 Quentin "Sardem FF7" Glidic
 *
 * This file is part of WayHouse.
 *
 * WayHouse is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WayHouse is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WayHouse. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <locale.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <nkutils-colour.h>
#include <wayland-client.h>
#include <libgwater-wayland.h>
#include <wayland-cursor.h>
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include "client.h"
#include "dock-manager-unstable-v2-client-protocol.h"

/* Supported interface versions */
#define WL_COMPOSITOR_INTERFACE_VERSION 3
#define WW_DOCK_MANAGER_INTERFACE_VERSION 1
#define WL_SHM_INTERFACE_VERSION 1
#define WL_SEAT_INTERFACE_VERSION 5
#define WL_OUTPUT_INTERFACE_VERSION 2

typedef enum {
    WW_DOCK_GLOBAL_COMPOSITOR,
    WW_DOCK_GLOBAL_DOCK_MANAGER,
    WW_DOCK_GLOBAL_SHM,
    _WW_DOCK_GLOBAL_SIZE,
} WhDockGlobalName;

typedef enum {
    WH_DOCK_CHILD_STATE_START = 0,
    WH_DOCK_CHILD_STATE_LINE_START,
    WH_DOCK_CHILD_STATE_SECTION,
    WH_DOCK_CHILD_STATE_SECTION_LENGTH,
} WhDockChildState;

typedef struct {
    GSubprocess *process;
    WhDockChildState state;
    GInputStream *stdout;
    GDataInputStream *data_stdout;
    gchar buf;
    gboolean stopped;
    gboolean urgent;
} WhDockChild;

typedef struct _WhDock WhDock;
typedef struct {
    WhClient *client;
    struct wl_display *display;
    struct wl_registry *registry;
    uint32_t global_name;
    struct zww_dock_manager_v2 *dock_manager;
    WhDockChild child;
    NkColourDouble background_colour;
    NkColourDouble text_colour;
    WhDock *dock;
} WhDockContext;

typedef struct {
    PangoLayout *text;
} WhText;

struct _WhDock {
    WhDockContext *context;
    WhClientSurface *surface;
    struct zww_dock_v2 *dock;
    struct {
        gint ascent;
    } text;
    WhClientSize padding;
    WhClientSize size;
    guint trigger_handle;
    gsize pending;
    GList *pending_texts;
    GList *texts;
};

static void
_wh_dock_dock_protocol_configure(void *data, struct zww_dock_v2 *dock, int32_t min_width, int32_t min_height, int32_t max_width, int32_t max_height, enum zww_dock_manager_v2_position position)
{
    WhDock *self = data;

    switch ( position )
    {
    case ZWW_DOCK_MANAGER_V2_POSITION_TOP:
    case ZWW_DOCK_MANAGER_V2_POSITION_BOTTOM:
    break;
    case ZWW_DOCK_MANAGER_V2_POSITION_LEFT:
    case ZWW_DOCK_MANAGER_V2_POSITION_RIGHT:
    case ZWW_DOCK_MANAGER_V2_POSITION_DEFAULT:
        g_return_if_reached();
    }

    PangoFontMetrics *metrics;
    gint height, em;

    metrics = wh_client_get_font_metrics(self->context->client);
    self->text.ascent = pango_font_metrics_get_ascent(metrics) / PANGO_SCALE;
    height = ( pango_font_metrics_get_ascent(metrics) + pango_font_metrics_get_descent(metrics) ) / PANGO_SCALE;
    em = pango_font_metrics_get_approximate_char_width(metrics) / PANGO_SCALE;
    pango_font_metrics_unref(metrics);

    self->padding.width = em;
    self->padding.height = 2 * em / 3;
    self->size.width = max_width;
    self->size.height = MAX(min_height, height + self->padding.height * 2);
}

static const struct zww_dock_v2_listener _wh_dock_dock_interface = {
    .configure = _wh_dock_dock_protocol_configure,
};

static void
_wh_dock_frame_callback(void *data, struct wl_callback *callback, uint32_t time)
{
    WhDock *self = data;

    wl_callback_destroy(callback);

    if ( ( self->pending > 2 ) && self->context->child.stopped )
    {
        g_subprocess_send_signal(self->context->child.process, SIGUSR1);
        self->context->child.stopped = FALSE;
    }
    --self->pending;
}

static const struct wl_callback_listener _wh_dock_frame_wl_callback_listener = {
    .done = _wh_dock_frame_callback,
};

static gboolean
_wh_dock_draw(gpointer user_data)
{
    WhDock *self = user_data;
    WhClientBuffer *buffer;

    self->trigger_handle = 0;

    buffer = wh_client_surface_get_buffer(self->surface);
    if ( buffer == NULL )
        return FALSE;
    if ( ( self->pending > 1 ) && ( ! self->context->child.stopped ) )
    {
        g_subprocess_send_signal(self->context->child.process, SIGUSR2);
        self->context->child.stopped = TRUE;
    }

    cairo_t *cr;

    cr = cairo_create(wh_client_buffer_get_surface(buffer));

    cairo_set_source_rgba(cr, self->context->background_colour.red, self->context->background_colour.green, self->context->background_colour.blue, self->context->background_colour.alpha);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);

    gint x = self->size.width - self->padding.width;
    gint y;
    gint text_width;
    gint text_height;
    gint text_baseline;
    cairo_set_source_rgba(cr, self->context->text_colour.red, self->context->text_colour.green, self->context->text_colour.blue, self->context->text_colour.alpha);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    GList *text_;
    for ( text_ = self->texts ; text_ != NULL ; text_ = g_list_next(text_) )
    {
        PangoLayout *text = text_->data;
        pango_layout_get_pixel_size(text, &text_width, &text_height);
        text_baseline = pango_layout_get_baseline(text) / PANGO_SCALE;
        x -= text_width;
        y = self->padding.height + self->text.ascent - text_baseline;
        cairo_move_to(cr, x, y);
        pango_cairo_update_layout(cr, text);
        pango_cairo_show_layout(cr, text);
        if ( g_list_next(text_) != NULL )
        {
            x -= self->padding.width;
            cairo_rectangle(cr, x, self->padding.height, 0.5, self->size.height - 2 * self->padding.height);
            cairo_fill(cr);
            x -= (self->padding.width - 1);
        }
    }
    g_list_free_full(self->texts, g_object_unref);
    self->texts = NULL;

    cairo_destroy(cr);

    struct wl_callback *frame_cb;

    ++self->pending;
    wh_client_buffer_commit(buffer, &frame_cb);
    wl_callback_add_listener(frame_cb, &_wh_dock_frame_wl_callback_listener, self);

    return FALSE;
}

static void
_wh_dock_trigger_drawing(WhDock *self)
{
    if ( self->trigger_handle > 0 )
        return;
    self->trigger_handle = g_idle_add(_wh_dock_draw, self);
}

static WhDock *
_wh_dock_create(WhDockContext *context)
{
    WhDock *self;
    self = g_new0(WhDock, 1);

    self->context = context;
    self->surface = wh_client_surface_new(self->context->client);
    if ( self->surface == NULL )
    {
        g_warning("Could not create surface");
        g_free(self);
        return NULL;
    }

    self->dock = zww_dock_manager_v2_create_dock(self->context->dock_manager, wh_client_surface_get_surface(self->surface), NULL, ZWW_DOCK_MANAGER_V2_POSITION_BOTTOM);
    if ( self->dock == NULL )
    {
        g_warning("Could not create dock");
        g_free(self);
        return NULL;
    }

    zww_dock_v2_add_listener(self->dock, &_wh_dock_dock_interface, self);
    wl_display_roundtrip(self->context->display);

    g_debug("SIZE %dx%d", self->size.width, self->size.height);

    if ( ( self->size.width < 1 ) || ( self->size.height < 1 ) || ( ! wh_client_surface_resize(self->surface, self->size) ) )
    {
        zww_dock_v2_destroy(self->dock);
        wh_client_surface_free(self->surface);
        g_free(self);
        return NULL;
    }

    return self;
}

static void
_wh_dock_free(WhDock *self)
{
    zww_dock_v2_destroy(self->dock);
    wh_client_surface_free(self->surface);
    g_free(self);
}

static void _wh_dock_spawn_status_generator(WhDockContext *self);

static void
_wh_dock_child_existed(GObject *obj, GAsyncResult *res, gpointer user_data)
{
    WhDockContext *self = user_data;
    GError *error = NULL;
    if ( g_subprocess_wait_check_finish(G_SUBPROCESS(obj), res, &error) )
        return;

    if ( self->child.data_stdout != NULL )
    {
        g_object_unref(self->child.data_stdout);
        self->child.data_stdout = NULL;
    }
    _wh_dock_spawn_status_generator(self);
}

static void
_wh_dock_child_read_line_callback(GObject *obj, GAsyncResult *res, gpointer user_data)
{
    WhDockContext *self = user_data;
    GError *error = NULL;
    gchar *line;

    line = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(obj), res, NULL, &error);
    if ( line == NULL )
    {
        if ( error == NULL)
            return;
        g_subprocess_force_exit(self->child.process);
    }

    if ( self->child.buf != '\0' )
    {
        gchar *tmp, buf[] = { self->child.buf, '\0' };
        tmp = g_strconcat(buf, line, NULL);
        g_free(line);
        line = tmp;
        self->child.buf = '\0';
    }
    g_debug("Read line: %s", line);
    if ( self->dock->texts == NULL )
        self->dock->texts = g_list_prepend(self->dock->texts, wh_client_text_new(self->client, line, -1));
    else
    {
        g_object_unref(self->dock->texts->data);
        self->dock->texts->data = wh_client_text_new(self->client, line, -1);
    }
    _wh_dock_trigger_drawing(self->dock);
    g_data_input_stream_read_line_async(self->child.data_stdout, G_PRIORITY_DEFAULT, NULL, _wh_dock_child_read_line_callback, self);
}

static void
_wh_dock_child_read_callback(GObject *obj, GAsyncResult *res, gpointer user_data)
{
    WhDockContext *self = user_data;
    GError *error = NULL;
    GBytes *bytes;

    bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(obj), res, &error);
    if ( bytes == NULL )
    {
        g_warning("Couldn’t read child output: %s", error->message);
        g_clear_error(&error);
        goto error;
    }

    gsize to_read = 0;
    switch ( self->child.state )
    {
    case WH_DOCK_CHILD_STATE_START:
    {
        const guchar *type;

        type = g_bytes_get_data(bytes, NULL);
        switch ( *type )
        {
        case '\0':
            self->child.state = WH_DOCK_CHILD_STATE_LINE_START;
            to_read = 1;
        break;
        default:
            self->child.buf = *type;
            g_bytes_unref(bytes);
            self->child.data_stdout = g_data_input_stream_new(self->child.stdout);
            g_data_input_stream_read_line_async(self->child.data_stdout, G_PRIORITY_DEFAULT, NULL, _wh_dock_child_read_line_callback, self);
            return;
        }
    }
    break;
    case WH_DOCK_CHILD_STATE_LINE_START:
    {
        const guchar *type;

        type = g_bytes_get_data(bytes, NULL);
        switch ( *type )
        {
        case 's':
            self->child.state = WH_DOCK_CHILD_STATE_SECTION;
            to_read = sizeof(guint64);
        break;
        case 'u':
            self->child.urgent = TRUE;
            to_read = 1;
        break;
        case '\0':
            g_list_free_full(self->dock->texts, g_object_unref);
            self->dock->texts = self->dock->pending_texts;
            self->dock->pending_texts = NULL;
            if ( self->child.urgent && self->child.stopped )
                zww_dock_v2_show(self->dock->dock);
            self->child.urgent = FALSE;
            _wh_dock_trigger_drawing(self->dock);
            to_read = 1;
        break;
        }
    }
    break;
    case WH_DOCK_CHILD_STATE_SECTION:
    {
        const guint64 *length;
        length = g_bytes_get_data(bytes, NULL);
        self->child.state = WH_DOCK_CHILD_STATE_SECTION_LENGTH;
        to_read = GUINT64_FROM_BE(*length);
    }
    break;
    case WH_DOCK_CHILD_STATE_SECTION_LENGTH:
    {
        PangoLayout *text;
        const gchar *data;
        gsize length;
        data = g_bytes_get_data(bytes, &length);
        text = wh_client_text_new(self->client, data, length);
        self->dock->pending_texts = g_list_prepend(self->dock->pending_texts, text);
        self->child.state = WH_DOCK_CHILD_STATE_LINE_START;
        to_read = 1;
    }
    break;
    }

    g_bytes_unref(bytes);

    if ( to_read > 0 )
        g_input_stream_read_bytes_async(self->child.stdout, to_read, G_PRIORITY_DEFAULT, NULL, _wh_dock_child_read_callback, self);
    else
error:
        g_subprocess_force_exit(self->child.process);
}

static void
_wh_dock_spawn_status_generator(WhDockContext *self)
{
    GError *error = NULL;
    gchar *child_argv[] = {
        "j4status",
        "-o", "pango",
        NULL
    };

    self->child.process = g_subprocess_newv((const gchar * const *) child_argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error);
    if ( self->child.process == NULL )
    {
        g_warning("Couldn’t launch child: %s", error->message);
        g_clear_error(&error);
        return;
    }

    self->child.stdout = g_subprocess_get_stdout_pipe(self->child.process);
    g_input_stream_read_bytes_async(self->child.stdout, 1, G_PRIORITY_DEFAULT, NULL, _wh_dock_child_read_callback, self);
    g_subprocess_wait_check_async(self->child.process, NULL, _wh_dock_child_existed, self);
}

static void
_wh_dock_registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    WhDockContext *self = data;

    if ( g_strcmp0(interface, "zww_dock_manager_v2") == 0 )
    {
        self->global_name = name;
        self->dock_manager = wl_registry_bind(registry, name, &zww_dock_manager_v2_interface, WW_DOCK_MANAGER_INTERFACE_VERSION);
    }
}

static void
_wh_dock_registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    WhDockContext *self = data;

    if ( self->global_name == name )
    {
        self->global_name = 0;
        zww_dock_manager_v2_destroy(self->dock_manager);
        self->dock_manager = NULL;
    }
}

static const struct wl_registry_listener _wh_dock_registry_listener = {
    .global = _wh_dock_registry_handle_global,
    .global_remove = _wh_dock_registry_handle_global_remove,
};

static gboolean
_wh_dock_parse_colour(const gchar *option_name, const gchar *value, gpointer data, GError **error)
{
    WhDockContext *self = data;
    NkColourDouble *colour;
    if ( option_name[1] == '-' )
        option_name += 2;
    else
        option_name += 1;
    switch ( option_name[0] )
    {
    case 'b':
        colour = &self->background_colour;
    break;
    case 't':
        colour = &self->text_colour;
    break;
    default:
        g_return_val_if_reached(FALSE);
    }
    return nk_colour_double_parse(value, colour);
}

int
main(int argc, char *argv[])
{
    static WhDockContext self_;
    WhDockContext *self = &self_;

    setlocale(LC_ALL, "");

    int retval = 0;
    GError *error = NULL;

    self->client = wh_client_new();

    self->background_colour.alpha = 1.0;
    self->text_colour.red = 1.0;
    self->text_colour.green = 1.0;
    self->text_colour.blue = 1.0;
    self->text_colour.alpha = 1.0;

    GOptionContext *option_context = NULL;
    GOptionGroup *option_group = NULL;
    GOptionEntry entries[] =
    {
        { "background-colour", 'b', 0,                     G_OPTION_ARG_CALLBACK, _wh_dock_parse_colour,    "Colour to use as background, defaults to #000000", "<colour>" },
        { "text-colour",       't', 0,                     G_OPTION_ARG_CALLBACK, _wh_dock_parse_colour,    "Colour to use for the text, defaults to #FFFFFF",  "<colour>" },
        { .long_name = NULL }
    };

    option_context = g_option_context_new("- ");
    option_group = g_option_group_new("", "", "", self, NULL);
    g_option_group_add_entries(option_group, entries);
    g_option_group_set_translation_domain(option_group, GETTEXT_PACKAGE);
    g_option_context_set_main_group(option_context, option_group);
    wh_client_add_option_group(self->client, option_context);
    if ( ! g_option_context_parse(option_context, &argc, &argv, &error) )
    {
        g_warning("Option parsing failed: %s\n", error->message);
        retval = 2;
        goto end;
    }
    g_option_context_free(option_context);

    self->display = wh_client_get_display(self->client);
    self->registry = wl_display_get_registry(self->display);
    wl_registry_add_listener(self->registry, &_wh_dock_registry_listener, self);
    wl_display_roundtrip(self->display);

    if ( self->dock_manager == NULL )
    {
        wh_client_free(self->client);
        g_warning("No wh_dock_manager interface provided by the compositor");
        return 4;
    }

    self->dock = _wh_dock_create(self);
    if ( self->dock == NULL )
        return 5;

    _wh_dock_spawn_status_generator(self);

    retval = wh_client_run(self->client);

    g_subprocess_send_signal(self->child.process, SIGTERM);
    g_subprocess_wait(self->child.process, NULL, NULL);

end:
    return retval;
}
