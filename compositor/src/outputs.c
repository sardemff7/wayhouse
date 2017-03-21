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
#include "outputs.h"

struct _WhOutputs {
    WhCore *core;
    struct wl_listener output_create_listener;
    struct wl_listener output_destroy_listener;
    GHashTable *outputs;
    GHashTable *outputs_by_name;
};

struct _WhOutput {
    WhOutputs *outputs;
    struct weston_output *output;
    WhWorkspace *current;
};

struct _WhContainer {
    WhWorkspaces *workspaces;
    enum { A, B} type;

    gboolean current;
    GQueue *children;
    GList *link;
    GQueue *history;
    GList *history_link;

    gboolean visible;
    WhContainer *parent;
    guint layout;
    struct weston_geometry geometry;
};

struct _WhWorkspace {
    WhContainer container;
    WhOutput *output;
    gchar *name;
    guint64 number;
};

void
wh_outputs_control(WhOutputs *self, WhSeat *seat, WhStateChange state, const gchar *name)
{
    WhOutput *output;

    output = g_hash_table_lookup(self->outputs_by_name, name);
    if ( output == NULL )
        return;

    switch ( state )
    {
    case WH_STATE_ENABLE:
        if ( ! output->output->enabled )
        {
            weston_output_enable(output->output);
            wh_workspaces_add_output(wh_core_get_workspaces(self->core), output);
        }
    break;
    case WH_STATE_DISABLE:
        if ( output->output->enabled )
        {
            weston_output_disable(output->output);
            wh_workspaces_remove_output(wh_core_get_workspaces(self->core), output);
            output->current = NULL;
        }
    break;
    case WH_STATE_TOGGLE:
        if ( output->output->enabled )
        {
            weston_output_disable(output->output);
            wh_workspaces_remove_output(wh_core_get_workspaces(self->core), output);
            output->current = NULL;
        }
        else
        {
            weston_output_enable(output->output);
            wh_workspaces_add_output(wh_core_get_workspaces(self->core), output);
        }
    break;
    }
}

gboolean
wh_output_set_current_workspace(WhOutput *self, WhWorkspace *workspace)
{
    g_debug("Output %s got workspace %s (previous %s)", self->output->name, workspace->name, self->current ? self->current->name : "none");
    if ( self->current == workspace )
        return FALSE;

    if ( self->current != NULL )
        wh_workspace_hide(self->current);
    self->current = workspace;
    wh_workspace_show(self->current);
    return TRUE;
}

WhWorkspace *
wh_output_get_current_workspace(WhOutput *self)
{
    g_debug("Output %s has current workspace workspace %s", self->output->name, self->current ? self->current->name : "none");
    return self->current;
}

static void
_wh_output_new(WhOutputs *outputs, struct weston_output *output)
{
    WhOutput *self;

    self = g_new0(WhOutput, 1);
    self->outputs = outputs;
    self->output = output;

    g_hash_table_insert(self->outputs->outputs, self->output, self);
    g_hash_table_insert(self->outputs->outputs_by_name, self->output->name, self);

    wh_workspaces_add_output(wh_core_get_workspaces(self->outputs->core), self);
}

static void
_wh_output_free(gpointer data)
{
    WhOutput *self = data;

    wh_workspaces_remove_output(wh_core_get_workspaces(self->outputs->core), self);

    g_free(self);
}

struct weston_geometry
wh_output_get_geometry(WhOutput *self)
{
    struct weston_geometry geometry = {
        .x = self->output->x,
        .y = self->output->y,
        .width = self->output->width,
        .height = self->output->height,
    };
    return geometry;
}

static void
_wh_outputs_output_created(struct wl_listener *listener, void *data)
{
    WhOutputs *self = wl_container_of(listener, self, output_create_listener);
    struct weston_output *output = data;

    _wh_output_new(self, output);
}

static void
_wh_outputs_output_destroyed(struct wl_listener *listener, void *data)
{
    /* FIXME: we need a way to check that the output is gone for good or just disabled
    WhOutputs *self = wl_container_of(listener, self, output_destroy_listener);
    struct weston_output *output = data;

    g_hash_table_remove(self->outputs_by_name, output->name);
    g_hash_table_remove(self->outputs, output);
    */
}

WhOutputs *
wh_outputs_new(WhCore *core)
{
    WhOutputs *self;

    self = g_new0(WhOutputs, 1);
    self->core = core;

    self->outputs = g_hash_table_new_full(NULL, NULL, NULL, _wh_output_free);
    self->outputs_by_name = g_hash_table_new(g_str_hash, g_str_equal);

    struct weston_compositor *compositor = wh_core_get_compositor(self->core);
    struct weston_output *output;

    wl_list_for_each(output, &compositor->output_list, link)
        _wh_output_new(self, output);

    self->output_create_listener.notify = _wh_outputs_output_created;
    wl_signal_add(&compositor->output_created_signal, &self->output_create_listener);

    self->output_destroy_listener.notify = _wh_outputs_output_destroyed;
    wl_signal_add(&compositor->output_destroyed_signal, &self->output_destroy_listener);

    return self;
}

void
wh_outputs_free(WhOutputs *self)
{
    if ( self == NULL )
        return;

    g_hash_table_unref(self->outputs_by_name);
    g_hash_table_unref(self->outputs);

    g_free(self);
}

WhOutput *
wh_outputs_get(WhOutputs *self, WhOutput *current, WhDirection direction)
{
    gint32 cx, cy, dx, dy, tx, ty, x, y, min, max, val;
    gboolean vertical;

    dx = current->output->width / 2;
    dy = current->output->height / 2;
    cx = current->output->x + dx;
    cy = current->output->y + dy;
    tx = G_MAXINT32;
    ty = G_MAXINT32;

    const gchar * const directions[] = {
        [WH_DIRECTION_TOP] = "top",
        [WH_DIRECTION_BOTTOM] = "bottom",
        [WH_DIRECTION_LEFT] = "left",
        [WH_DIRECTION_RIGHT] = "right",
    };

    g_debug("Get output on %s of %s (%dx%d+%dx%d): %dx%d (%dx%d)", directions[direction], current->output->name, current->output->width, current->output->height, current->output->x, current->output->y, cx, cy, dx, dy);
    GHashTableIter iter;
    g_hash_table_iter_init(&iter, self->outputs);
    switch ( direction )
    {
    case WH_DIRECTION_TOP:
        min = G_MININT32;
        max = current->output->y;
        vertical = TRUE;
    break;
    case WH_DIRECTION_BOTTOM:
        min = current->output->y + current->output->height;
        max = G_MAXINT32;
        vertical = TRUE;
    break;
    case WH_DIRECTION_LEFT:
        min = G_MININT32;
        max = current->output->x;
        vertical = FALSE;
    break;
    case WH_DIRECTION_RIGHT:
        min = current->output->x + current->output->width;
        max = G_MAXINT32;
        vertical = FALSE;
    break;
    case WH_DIRECTION_PARENT:
    case WH_DIRECTION_CHILD:
    default:
        g_return_val_if_reached(NULL);
    }

    WhOutput *output, *target = NULL;
    while ( g_hash_table_iter_next(&iter, NULL, (gpointer *) &output) )
    {
        if ( current == output )
            continue;

        x = cx - ( output->output->x + output->output->width / 2 );
        x = ABS(x);
        y = cy - ( output->output->y + output->output->height / 2 );
        y = ABS(y);
        g_debug("        Try %s (%dx%d+%dx%d): %dx%d", output->output->name, output->output->width, output->output->height, output->output->x, output->output->y, x, y);
        if ( vertical )
        {
            g_debug("            Vertical move, deviation: %d / %d|%d", x, dx, tx);
            if ( ( x > dx ) || ( x > tx ) )
                continue;
            val = output->output->y;
        }
        else
        {
            g_debug("            Horizontal move, deviation: %d / %d|%d", y, dy, ty);
            if ( ( y > dy ) || ( y > ty ) )
                continue;
            val = output->output->x;
        }
        g_debug("Check min < val < max: %d < %d < %d", min, val, max);
        if ( ( val < min ) || ( val > max ) )
            continue;

        tx = x;
        ty = y;
        target = output;
    }

    if ( target != NULL )
        g_debug("    Found %s", target->output->name);
    return target;
}
