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
#include <nkutils-colour.h>
#include <wayland-client.h>
#include <libgwater-wayland.h>
#include <wayland-cursor.h>
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "client.h"

typedef struct _WhClientBufferPool WhClientBufferPool;

/* Supported interface versions */
#define WL_COMPOSITOR_INTERFACE_VERSION 3
#define WL_SHM_INTERFACE_VERSION 1
#define WL_SEAT_INTERFACE_VERSION 5
#define WL_OUTPUT_INTERFACE_VERSION 2

#define WAYLAND_OUTPUTS_MAX_NUMBER 32

typedef enum {
    WW_DOCK_GLOBAL_COMPOSITOR,
    WW_DOCK_GLOBAL_SHM,
    _WW_DOCK_GLOBAL_SIZE,
} WhGlobalName;

struct _WhClient {
    gchar *runtime_dir;
    GMainLoop *loop;
    GWaterWaylandSource *source;
    struct wl_display *display;
    struct wl_registry *registry;
    uint32_t global_names[_WW_DOCK_GLOBAL_SIZE];
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    gint64 buffer_count;
    struct {
        gchar *theme_name;
        gchar **name;
        struct wl_cursor_theme *theme;
        struct wl_cursor *cursor;
        struct wl_cursor_image *image;
        struct wl_surface *surface;
        struct wl_callback *frame_cb;
    } cursor;
    gboolean print_version;
    GHashTable *seats;
    GHashTable *outputs;
    PangoContext *pango_context;
};

typedef struct {
    WhClient *context;
    uint32_t global_name;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
} WhSeat;

typedef struct {
    WhClient *context;
    uint32_t global_name;
    struct wl_output *output;
    int32_t width;
    int32_t height;
    int32_t scale;
} WhOutput;

struct _WhClientBuffer {
    WhClientBufferPool *pool;
    struct wl_buffer *buffer;
    //guint8 *data;
    cairo_surface_t *cairo_surface;
    //gsize size;
    gboolean released;
};

struct _WhClientBufferPool {
    WhClient *client;
    WhClientSurface *surface;
    gint32 scale;
    guint8 *data;
    gsize size;
    gboolean to_free;
    WhClientBuffer *buffers;
};

typedef struct {
    PangoLayout *text;
} WhText;

struct _WhClientSurface {
    WhClient *context;
    struct wl_list link;
    struct wl_surface *surface;
    WhClientBufferPool *pool;
    WhClientSize size;
    WhOutput *outputs[WAYLAND_OUTPUTS_MAX_NUMBER];
    GList *texts;
};

static WhOutput *
_wh_get_output(WhClient *self, struct wl_output *wl_output)
{
    return g_hash_table_lookup(self->outputs, wl_output);
}

static void
_wh_buffer_cleanup(WhClientBufferPool *self)
{
    if ( ! self->to_free )
        return;

    gint64 i, count = 0;
    for ( i = 0 ; i < self->client->buffer_count ; ++i )
    {
        if ( ( self->buffers[i].released ) && ( self->buffers[i].buffer != NULL ) )
        {
            cairo_surface_destroy(self->buffers[i].cairo_surface);
            wl_buffer_destroy(self->buffers[i].buffer);
            self->buffers[i].buffer = NULL;
        }
        if ( self->buffers[i].buffer == NULL )
            ++count;
    }

    if ( count < self->client->buffer_count )
        return;

    munmap(self->data, self->size);
    g_free(self);
}

static void
_wh_buffer_release(void *data, struct wl_buffer *buffer)
{
    WhClientBufferPool *self = data;

    gint64 i;
    for ( i = 0 ; i < self->client->buffer_count ; ++i )
    {
        if ( self->buffers[i].buffer == buffer )
            self->buffers[i].released = TRUE;
    }

    _wh_buffer_cleanup(self);
}

static void
_wh_buffer_pool_free(WhClientBufferPool *self)
{
    if ( self == NULL )
        return;

    self->to_free = TRUE;
    _wh_buffer_cleanup(self);
}

static const struct wl_buffer_listener _wh_buffer_listener = {
    _wh_buffer_release
};

static WhClientBufferPool *
_wh_create_buffer_pool(WhClientSurface *surface, gint32 width, gint32 height, gint32 scale)
{
    WhClient *client = surface->context;
    struct wl_shm_pool *pool;
    int fd;
    uint8_t *data;
    int32_t stride;
    size_t size;
    size_t pool_size;

    width *= scale;
    height *= scale;

    stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    size = stride * height;
    pool_size = size * client->buffer_count;

    gchar *filename;
    filename = g_build_filename(client->runtime_dir, "wayland-surface", NULL);
    fd = g_open(filename, O_CREAT | O_RDWR | O_CLOEXEC, 0);
    g_unlink(filename);
    g_free(filename);
    if ( fd < 0 )
    {
        g_warning("creating a file failed: %s\n", g_strerror(errno));
        return NULL;
    }
    if ( ftruncate(fd, pool_size) < 0 )
    {
        g_warning("truncating file to %zu B failed: %s\n", pool_size, g_strerror(errno));
        close(fd);
        return NULL;
    }

    data = mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ( data == MAP_FAILED )
    {
        g_warning("mmap failed: %s\n", g_strerror(errno));
        close(fd);
        return NULL;
    }

    WhClientBufferPool *self;
    self = g_new0(WhClientBufferPool, 1);
    self->client = client;
    self->surface = surface;
    self->buffers = g_new0(WhClientBuffer, client->buffer_count);
    self->scale = scale;

    pool = wl_shm_create_pool(client->shm, fd, pool_size);
    gint64 i;
    for ( i = 0 ; i < client->buffer_count ; ++i )
    {
        self->buffers[i].pool = self;
        self->buffers[i].buffer = wl_shm_pool_create_buffer(pool, size * i, width, height, stride, WL_SHM_FORMAT_ARGB8888);
        //self->buffers[i].data = data + size * i;
        self->buffers[i].cairo_surface = cairo_image_surface_create_for_data(data + size * i, CAIRO_FORMAT_ARGB32, width, height, stride);
        cairo_surface_set_device_scale(self->buffers[i].cairo_surface, scale, scale);
        self->buffers[i].released = TRUE;
        wl_buffer_add_listener(self->buffers[i].buffer, &_wh_buffer_listener, self);
    }
    wl_shm_pool_destroy(pool);
    close(fd);

    return self;
}

static void
_wh_surface_protocol_enter(void *data, struct wl_surface *wl_surface, struct wl_output *wl_output)
{
    WhClientSurface *self = data;
    WhOutput *output;

    output = g_hash_table_lookup(self->context->outputs, wl_output);
    if ( output == NULL )
        return;

    gsize i;
    for ( i = 0 ; i < WAYLAND_OUTPUTS_MAX_NUMBER ; ++i )
    {
        if ( self->outputs[i] == NULL )
            break;
    }
    if ( i == WAYLAND_OUTPUTS_MAX_NUMBER )
        return;
    self->outputs[i] = output;
}

static void
_wh_surface_protocol_leave(void *data, struct wl_surface *wl_surface, struct wl_output *wl_output)
{
    WhClientSurface *self = data;
    WhOutput *output;

    output = _wh_get_output(self->context, wl_output);
    if ( output == NULL )
        return;

    gsize i;
    for ( i = 0 ; i < WAYLAND_OUTPUTS_MAX_NUMBER ; ++i )
    {
        if ( self->outputs[i] == output )
            break;
    }
    if ( i == WAYLAND_OUTPUTS_MAX_NUMBER )
        return;

    for ( ; i < (WAYLAND_OUTPUTS_MAX_NUMBER - 1) ; ++i )
    {
        self->outputs[i] = self->outputs[i+1];
    }
    self->outputs[WAYLAND_OUTPUTS_MAX_NUMBER - 1] = NULL;
}

static const struct wl_surface_listener _wh_surface_interface = {
    .enter = _wh_surface_protocol_enter,
    .leave = _wh_surface_protocol_leave,
};

WhClientSurface *
wh_client_surface_new(WhClient *client)
{
    WhClientSurface *self;
    self = g_new0(WhClientSurface, 1);

    self->context = client;
    self->surface = wl_compositor_create_surface(self->context->compositor);
    if ( self->surface == NULL )
    {
        g_free(self);
        return NULL;
    }

    wl_surface_add_listener(self->surface, &_wh_surface_interface, self);

    return self;
}

void
wh_client_surface_free(WhClientSurface *self)
{
    wl_surface_destroy(self->surface);
    _wh_buffer_pool_free(self->pool);
    g_free(self);
}

gboolean
wh_client_surface_resize(WhClientSurface *self, WhClientSize size)
{
    WhClientBufferPool *pool;
    gint32 scale = 1;

    gsize i;
    for ( i = 0 ; ( i < WAYLAND_OUTPUTS_MAX_NUMBER ) && ( self->outputs[i] != NULL ) ; ++i )
    {
        if ( scale < self->outputs[i]->scale )
            scale = self->outputs[i]->scale;
    }
    g_debug("Resize %dx%d@%d", size.width, size.height, scale);
    pool = _wh_create_buffer_pool(self, size.width, size.height, scale);
    if ( ( pool == NULL ) && ( self->pool == NULL ) )
        return FALSE;
    _wh_buffer_pool_free(self->pool);
    self->pool = pool;
    self->size = size;
    return TRUE;
}

struct wl_surface *
wh_client_surface_get_surface(WhClientSurface *self)
{
    return self->surface;
}

WhClientBuffer *
wh_client_surface_get_buffer(WhClientSurface *surface)
{
    WhClientBuffer *buffer = NULL;

    gint64 i;
    for ( i = 0 ; ( buffer == NULL ) && ( i < surface->context->buffer_count ) ; ++i )
    {
        buffer = surface->pool->buffers + i;
        if ( ! buffer->released )
            buffer = NULL;
    }
    if ( buffer == NULL )
        return NULL;
    buffer->released = FALSE;

    return buffer;
}

void
wh_client_buffer_commit(WhClientBuffer *self, struct wl_callback **frame_cb)
{
    WhClientSurface *surface = self->pool->surface;

    cairo_surface_flush(self->cairo_surface);

    wl_surface_damage(surface->surface, 0, 0, surface->size.width, surface->size.height);
    wl_surface_attach(surface->surface, self->buffer, 0, 0);
    if ( wl_surface_get_version(surface->surface) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION )
        wl_surface_set_buffer_scale(surface->surface, self->pool->scale);
    if ( frame_cb != NULL )
        *frame_cb = wl_surface_frame(surface->surface);
    wl_surface_commit(surface->surface);
}

cairo_surface_t *
wh_client_buffer_get_surface(WhClientBuffer *self)
{
    return self->cairo_surface;
}

PangoLayout *
wh_client_text_new(WhClient *self, const gchar *text, gsize length)
{
    PangoLayout *layout;

    layout = pango_layout_new(self->pango_context);
    pango_layout_set_markup(layout, text, length);

    return layout;
}

static void
_wh_cursor_set_image(WhClient *self, int i)
{
    struct wl_buffer *buffer;
    struct wl_cursor_image *image;
    image = self->cursor.cursor->images[i];

    self->cursor.image = image;
    buffer = wl_cursor_image_get_buffer(self->cursor.image);
    wl_surface_attach(self->cursor.surface, buffer, 0, 0);
    wl_surface_damage(self->cursor.surface, 0, 0, self->cursor.image->width, self->cursor.image->height);
    wl_surface_commit(self->cursor.surface);
}

static void _wh_cursor_frame_callback(void *data, struct wl_callback *callback, uint32_t time);

static const struct wl_callback_listener _wh_cursor_frame_wl_callback_listener = {
    .done = _wh_cursor_frame_callback,
};

static void
_wh_cursor_frame_callback(void *data, struct wl_callback *callback, uint32_t time)
{
    WhClient *self = data;
    int i;

    if ( self->cursor.frame_cb != NULL )
        wl_callback_destroy(self->cursor.frame_cb);
    self->cursor.frame_cb = wl_surface_frame(self->cursor.surface);
    wl_callback_add_listener(self->cursor.frame_cb, &_wh_cursor_frame_wl_callback_listener, self);

    i = wl_cursor_frame(self->cursor.cursor, time);
    _wh_cursor_set_image(self, i);
}

static void
_wh_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    WhSeat *self = data;
    WhClient *context = self->context;

    if ( context->cursor.surface == NULL )
        return;

    if ( context->cursor.cursor->image_count < 2 )
        _wh_cursor_set_image(context, 0);
    else
        _wh_cursor_frame_callback(context, context->cursor.frame_cb, 0);

    wl_pointer_set_cursor(self->pointer, serial, context->cursor.surface, context->cursor.image->hotspot_x, context->cursor.image->hotspot_y);
}

static void
_wh_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{
    WhSeat *self = data;
    WhClient *context = self->context;

    if ( context->cursor.frame_cb != NULL )
        wl_callback_destroy(context->cursor.frame_cb);
}

static void
_wh_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
}

static void
_wh_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, enum wl_pointer_button_state state)
{
}

static void
_wh_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, enum wl_pointer_axis axis, wl_fixed_t value)
{
}

static void
_wh_pointer_frame(void *data, struct wl_pointer *pointer)
{
}

static void
_wh_pointer_axis_source(void *data, struct wl_pointer *pointer, enum wl_pointer_axis_source axis_source)
{
}

static void
_wh_pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, enum wl_pointer_axis axis)
{
}

static void
_wh_pointer_axis_discrete(void *data, struct wl_pointer *pointer, enum wl_pointer_axis axis, int32_t discrete)
{
}

static const struct wl_pointer_listener _wh_pointer_listener = {
    .enter = _wh_pointer_enter,
    .leave = _wh_pointer_leave,
    .motion = _wh_pointer_motion,
    .button = _wh_pointer_button,
    .axis = _wh_pointer_axis,
    .frame = _wh_pointer_frame,
    .axis_source = _wh_pointer_axis_source,
    .axis_stop = _wh_pointer_axis_stop,
    .axis_discrete = _wh_pointer_axis_discrete,
};

static void
_wh_pointer_release(WhSeat *self)
{
    if ( self->pointer == NULL )
        return;

    if ( wl_pointer_get_version(self->pointer) >= WL_POINTER_RELEASE_SINCE_VERSION )
        wl_pointer_release(self->pointer);
    else
        wl_pointer_destroy(self->pointer);

    self->pointer = NULL;
}

static void
_wh_seat_release(gpointer user_data)
{
    WhSeat *self = user_data;

    _wh_pointer_release(self);

    if ( wl_seat_get_version(self->seat) >= WL_SEAT_RELEASE_SINCE_VERSION )
        wl_seat_release(self->seat);
    else
        wl_seat_destroy(self->seat);

    free(self);
}

static void
_wh_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    WhSeat *self = data;
    if ( ( capabilities & WL_SEAT_CAPABILITY_POINTER ) && ( self->pointer == NULL ) )
    {
        self->pointer = wl_seat_get_pointer(self->seat);
        wl_pointer_add_listener(self->pointer, &_wh_pointer_listener, self);
    }
    else if ( ( ! ( capabilities & WL_SEAT_CAPABILITY_POINTER ) ) && ( self->pointer != NULL ) )
        _wh_pointer_release(self);
}

static void
_wh_seat_name(void *data, struct wl_seat *seat, const char *name)
{
}

static const struct wl_seat_listener _wh_seat_listener = {
    .capabilities = _wh_seat_capabilities,
    .name = _wh_seat_name,
};

static void
_wh_output_release(gpointer user_data)
{
    WhOutput *self = user_data;

    if ( wl_output_get_version(self->output) >= WL_OUTPUT_RELEASE_SINCE_VERSION )
        wl_output_release(self->output);
    else
        wl_output_destroy(self->output);

    free(self);
}

static void
_wh_output_done(void *data, struct wl_output *output)
{
}

static void
_wh_output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t width, int32_t height, int32_t subpixel, const char *make, const char *model, int32_t transform)
{
}

static void
_wh_output_mode(void *data, struct wl_output *output, enum wl_output_mode flags, int32_t width, int32_t height, int32_t refresh)
{
}

static void
_wh_output_scale(void *data, struct wl_output *output, int32_t scale)
{
    WhOutput *self = data;

    self->scale = scale;
}

static const struct wl_output_listener _wh_output_listener = {
    .geometry = _wh_output_geometry,
    .mode = _wh_output_mode,
    .scale = _wh_output_scale,
    .done = _wh_output_done,
};

static const char * const _wh_cursor_names[] = {
    "left_ptr",
    "default",
    "top_left_arrow",
    "left-arrow",
    NULL
};

static void
_wh_registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    WhClient *self = data;

    if ( g_strcmp0(interface, "wl_compositor") == 0 )
    {
        self->global_names[WW_DOCK_GLOBAL_COMPOSITOR] = name;
        self->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, MIN(version, WL_COMPOSITOR_INTERFACE_VERSION));
    }
    else if ( g_strcmp0(interface, "wl_shm") == 0 )
    {
        self->global_names[WW_DOCK_GLOBAL_SHM] = name;
        self->shm = wl_registry_bind(registry, name, &wl_shm_interface, MIN(version, WL_SHM_INTERFACE_VERSION));
    }
    else if ( g_strcmp0(interface, "wl_seat") == 0 )
    {
        WhSeat *seat = g_new0(WhSeat, 1);
        seat->context = self;
        seat->global_name = name;
        seat->seat = wl_registry_bind(registry, name, &wl_seat_interface, MIN(version, WL_SEAT_INTERFACE_VERSION));

        g_hash_table_insert(self->seats, seat->seat, seat);

        wl_seat_add_listener(seat->seat, &_wh_seat_listener, seat);
    }
    else if ( g_strcmp0(interface, "wl_output") == 0 )
    {
        WhOutput *output = g_new0(WhOutput, 1);
        output->context = self;
        output->global_name = name;
        output->output = wl_registry_bind(registry, name, &wl_output_interface, MIN(version, WL_OUTPUT_INTERFACE_VERSION));
        output->scale = 1;

        g_hash_table_insert(self->outputs, output->output, output);

        wl_output_add_listener(output->output, &_wh_output_listener, output);
    }

    if ( ( self->cursor.theme == NULL ) && ( self->compositor != NULL ) && ( self->shm != NULL ) )
    {
        self->cursor.theme = wl_cursor_theme_load(self->cursor.theme_name, 32, self->shm);
        if ( self->cursor.theme != NULL )
        {
            const char * const *cname = (const char * const *) self->cursor.name;
            for ( cname = ( cname != NULL ) ? cname : _wh_cursor_names ; ( self->cursor.cursor == NULL ) && ( *cname != NULL ) ; ++cname )
                self->cursor.cursor = wl_cursor_theme_get_cursor(self->cursor.theme, *cname);
            if ( self->cursor.cursor == NULL )
            {
                wl_cursor_theme_destroy(self->cursor.theme);
                self->cursor.theme = NULL;
            }
            else
                self->cursor.surface = wl_compositor_create_surface(self->compositor);
        }
    }
}

static gboolean
_wh_client_find_global(gpointer key, gpointer value, gpointer user_data)
{
    struct {
        WhClient *context;
        uint32_t global_name;
    } *global = value;
    uint32_t *name = user_data;
    return ( global->global_name == *name );
}

static void
_wh_registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    WhClient *self = data;

    WhGlobalName i;
    for ( i = 0 ; i < _WW_DOCK_GLOBAL_SIZE ; ++i )
    {
        if ( self->global_names[i] != name )
            continue;
        self->global_names[i] = 0;

        switch ( i )
        {
        case WW_DOCK_GLOBAL_COMPOSITOR:
            wl_compositor_destroy(self->compositor);
            self->compositor = NULL;
        break;
        case WW_DOCK_GLOBAL_SHM:
            wl_shm_destroy(self->shm);
            self->shm = NULL;
        break;
        case _WW_DOCK_GLOBAL_SIZE:
            g_return_if_reached();
        }
        return;
    }
    if ( ( self->cursor.theme != NULL ) && ( ( self->compositor == NULL ) || ( self->shm == NULL ) ) )
    {
        if ( self->cursor.frame_cb != NULL )
            wl_callback_destroy(self->cursor.frame_cb);
        self->cursor.frame_cb = NULL;

        wl_surface_destroy(self->cursor.surface);
        wl_cursor_theme_destroy(self->cursor.theme);
        self->cursor.surface = NULL;
        self->cursor.image = NULL;
        self->cursor.cursor = NULL;
        self->cursor.theme = NULL;
    }

    WhSeat *seat;
    seat = g_hash_table_find(self->seats, _wh_client_find_global, &name);
    if ( seat != NULL )
    {
        g_hash_table_remove(self->seats, seat->seat);
        return;
    }

    WhOutput *output;
    output = g_hash_table_find(self->outputs, _wh_client_find_global, &name);
    {
        g_hash_table_remove(self->outputs, output->output);
        _wh_output_release(output);
        return;
    }
}

static const struct wl_registry_listener _wh_registry_listener = {
    .global = _wh_registry_handle_global,
    .global_remove = _wh_registry_handle_global_remove,
};


WhClient *
wh_client_new(void)
{
    gchar *runtime_dir;
    runtime_dir = g_build_filename(g_get_user_runtime_dir(), PACKAGE_NAME, NULL);

    if ( g_mkdir_with_parents(runtime_dir, 0755) < 0 )
    {
        g_free(runtime_dir);
        return NULL;
    }

    WhClient *self;
    self = g_new0(WhClient, 1);

    self->buffer_count = 3;

    self->runtime_dir = runtime_dir;

    self->source = g_water_wayland_source_new(NULL, NULL);
    if ( self->source == NULL )
    {
        g_free(self->runtime_dir);
        g_free(self);
        return NULL;
    }

    self->display = g_water_wayland_source_get_display(self->source);
    self->registry = wl_display_get_registry(self->display);
    wl_registry_add_listener(self->registry, &_wh_registry_listener, self);

    self->seats = g_hash_table_new_full(NULL, NULL, NULL, _wh_seat_release);
    self->outputs = g_hash_table_new_full(NULL, NULL, NULL, _wh_output_release);

    self->pango_context = pango_context_new();
    pango_context_set_font_map(self->pango_context, pango_cairo_font_map_get_default());

    return self;
}

void
wh_client_free(WhClient *self)
{
    g_free(self->runtime_dir);

    g_hash_table_unref(self->outputs);
    g_hash_table_unref(self->seats);

    WhGlobalName i;
    for ( i = 0 ; i < _WW_DOCK_GLOBAL_SIZE ; ++i )
    {
        if ( self->global_names[i] != 0 )
            _wh_registry_handle_global_remove(self, self->registry, self->global_names[i]);
    }

    wl_registry_destroy(self->registry);
    self->registry = NULL;

    if ( self->source != NULL )
        g_water_wayland_source_free(self->source);
    self->display = NULL;
    self->source = NULL;

    g_free(self);
}

static gboolean
_wh_client_option_parse_font(const gchar *option_name, const gchar *value, gpointer user_data, GError **error)
{
    WhClient *self = user_data;
    PangoFontDescription *font;

    font = pango_font_description_from_string(value);
    pango_context_set_font_description(self->pango_context, font);

    return TRUE;
}

void
wh_client_add_option_group(WhClient *self, GOptionContext *option_context)
{
    GOptionGroup *option_group;
    GOptionEntry entries[] =
    {
        { "font",              'f', 0,                     G_OPTION_ARG_CALLBACK, _wh_client_option_parse_font, "The font to use",         "<font description>" },
        { "cursor-theme",      'C', 0,                     G_OPTION_ARG_STRING,   &self->cursor.theme_name,     "The cursor theme to use", "<theme-name>" },
        { "version",           'V', 0,                     G_OPTION_ARG_NONE,     &self->print_version,         "Print version",           NULL },
        { .long_name = NULL }
    };

    option_group = g_option_group_new("client", "WayHouse clients common options", "", self, NULL);
    g_option_group_add_entries(option_group, entries);
    g_option_group_set_translation_domain(option_group, GETTEXT_PACKAGE);
    g_option_context_add_group(option_context, option_group);
}

gint
wh_client_run(WhClient *self)
{
    if ( self->print_version )
    {
        g_printf(PACKAGE_NAME " " WAYHOUSE_VERSION "\n");
        return 0;
    }

    if ( self->shm == NULL )
    {
        g_warning("No wl_shm interface provided by the compositor");
        return 4;
    }

    self->loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(self->loop);
    g_main_loop_unref(self->loop);

    return 0;
}

struct wl_display *
wh_client_get_display(WhClient *self)
{
    return self->display;
}

PangoFontMetrics *
wh_client_get_font_metrics(WhClient *self)
{
    PangoFont *font;
    PangoFontMetrics *metrics;

    font = pango_font_map_load_font(pango_cairo_font_map_get_default(), self->pango_context, pango_context_get_font_description(self->pango_context));
    metrics = pango_font_get_metrics(font, pango_language_get_default());

    g_object_unref(font);

    return metrics;
}
