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

#ifndef __WAYHOUSE_OUTPUTS_H__
#define __WAYHOUSE_OUTPUTS_H__

#include "types.h"

WhOutputs *wh_outputs_new(WhCore *core);
void wh_outputs_free(WhOutputs *outputs);

WhOutput *wh_outputs_get(WhOutputs *outputs, WhOutput *output, WhDirection direction);

gboolean wh_output_set_current_workspace(WhOutput *output, WhWorkspace *workspace);
WhWorkspace *wh_output_get_current_workspace(WhOutput *output);

struct weston_geometry wh_output_get_geometry(WhOutput *self);

#endif /* __WAYHOUSE_OUTPUTS_H__ */
