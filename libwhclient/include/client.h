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

#ifndef __WAYHOUSE_CLIENT_H__
#define __WAYHOUSE_CLIENT_H__

typedef struct {
    gint32 width;
    gint32 height;
} WhClientSize;

typedef struct _WhClient WhClient;
typedef struct _WhClientSurface WhClientSurface;
typedef struct _WhClientBuffer WhClientBuffer;

WhClient *wh_client_new(void);
void wh_client_free(WhClient *client);

void wh_client_add_option_group(WhClient *client, GOptionContext *option_context);
gint wh_client_run(WhClient *client);

struct wl_display *wh_client_get_display(WhClient *client);
PangoFontMetrics *wh_client_get_font_metrics(WhClient *client);

PangoLayout *wh_client_text_new(WhClient *self, const gchar *text, gsize length);

WhClientSurface *wh_client_surface_new(WhClient *client);
void wh_client_surface_free(WhClientSurface *surface);
struct wl_surface *wh_client_surface_get_surface(WhClientSurface *surface);

gboolean wh_client_surface_resize(WhClientSurface *surface, WhClientSize size);
WhClientBuffer *wh_client_surface_get_buffer(WhClientSurface *surface);
void wh_client_buffer_commit(WhClientBuffer *buffer, struct wl_callback **frame_callback);

cairo_surface_t *wh_client_buffer_get_surface(WhClientBuffer *buffer);

#endif /* __WAYHOUSE_CLIENT_H__ */
