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

#ifndef __WAYHOUSE_CONTAINERS_H__
#define __WAYHOUSE_CONTAINERS_H__

#include "types.h"
#include <libweston-desktop.h>

WhWorkspaces *wh_workspaces_new(WhCore *core);
void wh_workspaces_free(WhWorkspaces *workspaces);

void wh_workspaces_add_surface(WhWorkspaces *workspaces, WhSurface *surface);
void wh_workspaces_add_output(WhWorkspaces *workspaces, WhOutput *output);
void wh_workspaces_focus_container(WhWorkspaces *workspaces, WhSeat *seat, WhDirection direction);
void wh_workspaces_focus_workspace(WhWorkspaces *workspaces, WhSeat *seat, WhTarget target);
void wh_workspaces_focus_workspace_name(WhWorkspaces *workspaces, WhSeat *seat, const gchar *target);
void wh_workspaces_focus_workspace_number(WhWorkspaces *workspaces, WhSeat *seat, guint64 target);
void wh_workspaces_focus_output(WhWorkspaces *workspaces, WhSeat *seat, WhDirection direction);
void wh_workspaces_focus_output_name(WhWorkspaces *workspaces, WhSeat *seat, const gchar *target);
void wh_workspaces_move_container(WhWorkspaces *workspaces, WhSeat *seat, WhDirection direction);
void wh_workspaces_move_container_to_workspace(WhWorkspaces *workspaces, WhSeat *seat, WhTarget target);
void wh_workspaces_move_container_to_workspace_name(WhWorkspaces *workspaces, WhSeat *seat, const gchar *target);
void wh_workspaces_move_container_to_workspace_number(WhWorkspaces *workspaces, WhSeat *seat, guint64 target);
void wh_workspaces_move_workspace_to_output(WhWorkspaces *workspaces, WhSeat *seat, WhDirection direction);
void wh_workspaces_move_workspace_to_output_name(WhWorkspaces *workspaces, WhSeat *seat, const gchar *target);
void wh_workspaces_layout_switch(WhWorkspaces *workspaces, WhSeat *seat, WhContainerLayoutType type, WhOrientation orientation);

void wh_workspace_show(WhWorkspace *workspace);
void wh_workspace_hide(WhWorkspace *workspace);

extern const struct weston_desktop_api wh_workspaces_desktop_api;

struct weston_view *wh_surface_get_view(WhSurface *surface);
struct weston_surface *wh_surface_get_surface(WhSurface *surface);

void wh_surface_set_container(WhSurface *surface, WhContainer *container);
void wh_surface_set_size(WhSurface *surface, gint32 width, gint32 height);
void wh_surface_set_activated(WhSurface *surface, gboolean activated);

void wh_surface_fullscreen(WhSurface *surface, WhSurfaceStateChange change);
void wh_surface_close(WhSurface *surface);

#endif /* __WAYHOUSE_CONTAINERS_H__ */
