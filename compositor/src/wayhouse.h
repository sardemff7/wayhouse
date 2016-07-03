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

#ifndef __WAYHOUSE_WAYHOUSE_H__
#define __WAYHOUSE_WAYHOUSE_H__

#include "types.h"

struct weston_compositor *wh_core_get_compositor(WhCore *context);
WhConfig *wh_core_get_config(WhCore *core);
WhCommands *wh_core_get_commands(WhCore *core);
WhSeats *wh_core_get_seats(WhCore *core);
WhOutputs *wh_core_get_outputs(WhCore *core);
WhWorkspaces *wh_core_get_workspaces(WhCore *core);
WhSurface *wh_core_get_focus(WhCore *core);

void wh_core_set_focus(WhCore *core, WhSurface *surface);

void wh_stop(WhCore *core, WhSeat *seat);

#endif /* __WAYHOUSE_WAYHOUSE_H__ */
