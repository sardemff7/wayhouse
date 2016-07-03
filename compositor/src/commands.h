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

#ifndef __WAYHOUSE_COMMANDS_H__
#define __WAYHOUSE_COMMANDS_H__

#include "types.h"

WhCommands *wh_commands_new(WhCore *core);
void wh_commands_free(WhCommands *self);

WhCommand *wh_command_parse(WhCommands *commands, gchar *string);
void wh_command_free(WhCommand *command);

void wh_command_call(WhCommand *command, WhSeat *seat);
const gchar *wh_command_get_string(WhCommand *command);

#endif /* __WAYHOUSE_COMMANDS_H__ */
