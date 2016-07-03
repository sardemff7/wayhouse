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

#ifndef __WAYHOUSE_SEATS_H__
#define __WAYHOUSE_SEATS_H__

#include "types.h"

WhSeats *wh_seats_new(WhCore *core);
void wh_seats_free(WhSeats *seats);

WhSeat *wh_seats_get_from_weston_seat(WhSeats *seats, struct weston_seat *seat);

void wh_seats_set_focus(WhSeats *seats, WhSurface *surface);

#endif /* __WAYHOUSE_SEATS_H__ */
