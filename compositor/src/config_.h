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

#ifndef __WAYHOUSE_CONFIG_H__
#define __WAYHOUSE_CONFIG_H__

#include "types.h"

WhConfig *wh_config_new(WhCore *core, gboolean use_pixman);
void wh_config_free(WhConfig *config);
gboolean wh_config_load_backend(WhConfig *config);

struct xkb_rule_names *wh_config_get_xkb_names(WhConfig *config);
struct weston_backend_config *wh_config_get_drm_config(WhConfig *config);
struct weston_backend_config *wh_config_get_wayland_config(WhConfig *config);
struct weston_backend_config *wh_config_get_x11_config(WhConfig *config);
gboolean wh_config_get_xwayland(WhConfig *config);
const gchar * const *wh_config_get_common_modules(WhConfig *config);

const WhWorkspaceConfig *wh_config_get_first_workspace(void);
const WhWorkspaceConfig *wh_config_get_assign(WhConfig *config, const gchar *app_id);

#endif /* __WAYHOUSE_CONFIG_H__ */
