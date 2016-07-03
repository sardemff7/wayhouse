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

#include <string.h>

#include <glib.h>

#include <compositor.h>

#include "types.h"
#include "wayhouse.h"
#include "containers.h"
#include "seats.h"

struct _WhSeats {
    WhCore *core;
    struct wl_listener seat_create_listener;
    GHashTable *seats;
};

struct _WhSeat {
    WhSeats *seats;
    struct weston_seat *seat;
    struct wl_listener destroy_listener;
};

static void
_wh_seat_destroyed(struct wl_listener *listener, void *data)
{
    WhSeat *self = wl_container_of(listener, self, destroy_listener);

    g_hash_table_remove(self->seats->seats, self->seat);
}

static void
_wh_seat_new(WhSeats *seats, struct weston_seat *seat)
{
    WhSeat *self;

    self = g_new0(WhSeat, 1);
    self->seats = seats;
    self->seat = seat;

    g_hash_table_insert(self->seats->seats, seat, self);
    self->destroy_listener.notify = _wh_seat_destroyed;
    wl_signal_add(&self->seat->destroy_signal, &self->destroy_listener);
}

static void
_wh_seat_free(gpointer data)
{
    WhSeat *self = data;

    wl_list_remove(&self->destroy_listener.link);

    g_free(self);
}

void
wh_seats_set_focus(WhSeats *self, WhSurface *surface)
{
    GHashTableIter iter;
    WhSeat *seat;
    struct weston_surface *focus = ( surface == NULL ) ? NULL : wh_surface_get_surface(surface);

    g_hash_table_iter_init(&iter, self->seats);
    while ( g_hash_table_iter_next(&iter, NULL, (gpointer *) &seat) )
        weston_seat_set_keyboard_focus(seat->seat, focus);
}

static void
_wh_seats_seat_created(struct wl_listener *listener, void *data)
{
    WhSeats *self = wl_container_of(listener, self, seat_create_listener);
    struct weston_seat *seat = data;

    _wh_seat_new(self, seat);
}

WhSeats *
wh_seats_new(WhCore *core)
{
    WhSeats *self;

    self = g_new0(WhSeats, 1);
    self->core = core;

    self->seats = g_hash_table_new_full(NULL, NULL, NULL, _wh_seat_free);

    struct weston_compositor *compositor = wh_core_get_compositor(self->core);
    struct weston_seat *seat;

    wl_list_for_each(seat, &compositor->seat_list, link)
        _wh_seat_new(self, seat);

    self->seat_create_listener.notify = _wh_seats_seat_created;
    wl_signal_add(&compositor->seat_created_signal, &self->seat_create_listener);

    return self;
}

void
wh_seats_free(WhSeats *self)
{
    if ( self == NULL )
        return;

    g_hash_table_unref(self->seats);

    g_free(self);
}

WhSeat *
wh_seats_get_from_weston_seat(WhSeats *seats, struct weston_seat *seat)
{
    return NULL;
}
