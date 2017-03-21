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
#include <errno.h>

#include <glib.h>

#include <compositor.h>

#include "types.h"
#include "wayhouse.h"
#include "config_.h"
#include "seats.h"
#include "outputs.h"
#include "containers.h"

struct _WhWorkspaces {
    WhCore *core;
    GHashTable *workspaces;
    GHashTable *workspaces_by_number;
    GList *workspaces_sorted;
    guint64 workspace_biggest;
    struct weston_layer layer;
    struct weston_layer fullscreen_layer;
    GQueue *history;
};

typedef enum {
    WH_CONTAINER_TYPE_CONTAINER = 0,
    WH_CONTAINER_TYPE_WORKSPACE,
    WH_CONTAINER_TYPE_SURFACE,
} WhContainerType;

#define WH_CONTAINER_IS_SURFACE(c) ((c)->type == WH_CONTAINER_TYPE_SURFACE)
#define WH_CONTAINER_SURFACE(c) ((WhSurface *) (c))
#define WH_CONTAINER_IS_WORKSPACE(c) ((c)->type == WH_CONTAINER_TYPE_WORKSPACE)
#define WH_CONTAINER_WORKSPACE(c) ((WhWorkspace *) (c))

typedef enum {
    WH_CONTAINER_LAYOUT_TABBED_HORIZONTAL = ( WH_CONTAINER_LAYOUT_TABBED | ( WH_ORIENTATION_HORIZONTAL << 1 ) ),
    WH_CONTAINER_LAYOUT_TABBED_VERTICAL   = ( WH_CONTAINER_LAYOUT_TABBED | ( WH_ORIENTATION_VERTICAL   << 1 ) ),
    WH_CONTAINER_LAYOUT_SPLIT_HORIZONTAL  = ( WH_CONTAINER_LAYOUT_SPLIT  | ( WH_ORIENTATION_HORIZONTAL << 1 ) ),
    WH_CONTAINER_LAYOUT_SPLIT_VERTICAL    = ( WH_CONTAINER_LAYOUT_SPLIT  | ( WH_ORIENTATION_VERTICAL   << 1 ) ),
} WhContainerLayout;

#define  WH_CONTAINER_LAYOUT_GET_ORIENTATION(l) ((l >> 1) & 1)
#define  WH_CONTAINER_LAYOUT_IS_HORIZONTAL(l) (WH_CONTAINER_LAYOUT_GET_ORIENTATION(l) == WH_ORIENTATION_HORIZONTAL)
#define  WH_CONTAINER_LAYOUT_IS_VERTICAL(l) (WH_CONTAINER_LAYOUT_GET_ORIENTATION(l) == WH_ORIENTATION_VERTICAL)

#define WH_DIRECTION_GET_TARGET(d) ((d >> 1) & 1)
#define WH_DIRECTION_GET_ORIENTATION(d) ((d) & 1)

struct _WhContainer {
    WhWorkspaces *workspaces;
    WhContainerType type;

    gboolean current;
    GQueue *children;
    GList *link;
    GQueue *history;
    GList *history_link;

    gboolean visible;
    WhContainer *parent;
    WhContainerLayout layout;
    struct weston_geometry geometry;
};

struct _WhWorkspace {
    WhContainer container;
    WhOutput *output;
    gchar *name;
    guint64 number;
};


struct _WhSurface {
    WhContainer container;
    struct weston_desktop_surface *desktop_surface;
    struct weston_surface *surface;
    struct weston_view *view;
};

static void
_wh_container_resize(WhContainer *self)
{
    gint32 x, y;
    gint32 width, height;

    x = self->geometry.x;
    y = self->geometry.y;
    width = self->geometry.width;
    height = self->geometry.height;

    if ( WH_CONTAINER_IS_SURFACE(self) )
    {
        WhSurface *surface = WH_CONTAINER_SURFACE(self);
        struct weston_geometry geometry = weston_desktop_surface_get_geometry(surface->desktop_surface);
        if ( ( geometry.width != width ) || ( geometry.height != height ) )
            wh_surface_set_size(surface, width, height);
        return;
    }

    if ( g_queue_is_empty(self->children) )
        return;

    guint length;
    length = g_queue_get_length(self->children);

    switch ( self->layout )
    {
    case WH_CONTAINER_LAYOUT_TABBED_HORIZONTAL:
    case WH_CONTAINER_LAYOUT_TABBED_VERTICAL:
    break;
    case WH_CONTAINER_LAYOUT_SPLIT_HORIZONTAL:
        width /= length;
    break;
    case WH_CONTAINER_LAYOUT_SPLIT_VERTICAL:
        height /= length;
    break;
    }


    GList *child_;
    for ( child_ = g_queue_peek_head_link(self->children) ; child_ != NULL ; child_ = g_list_next(child_) )
    {
        WhContainer *child = child_->data;

        child->geometry.x = x;
        child->geometry.y = y;
        child->geometry.width = width;
        child->geometry.height = height;
        _wh_container_resize(child);

        switch ( self->layout )
        {
        case WH_CONTAINER_LAYOUT_TABBED_HORIZONTAL:
        case WH_CONTAINER_LAYOUT_TABBED_VERTICAL:
        break;
        case WH_CONTAINER_LAYOUT_SPLIT_HORIZONTAL:
            x += width;
        break;
        case WH_CONTAINER_LAYOUT_SPLIT_VERTICAL:
            y += height;
        break;
        }
    }
}

static void _wh_container_free(WhContainer *self);
static void _wh_container_show(WhContainer *self);
static void _wh_container_hide(WhContainer *self);
static void
_wh_container_reparent(WhContainer *self, WhContainer *parent)
{
    WhContainer *old_parent = self->parent;
    guint length;

    if ( old_parent == NULL )
        goto set;

    g_queue_unlink(old_parent->history, self->history_link);
    g_queue_unlink(old_parent->children, self->link);
    length = g_queue_get_length(old_parent->children);
    if ( length > 0 )
        _wh_container_resize(old_parent);
    else if ( ! WH_CONTAINER_IS_WORKSPACE(old_parent) )
        _wh_container_free(old_parent);
    else if ( ! old_parent->visible )
    {
        WhWorkspace *workspace = WH_CONTAINER_WORKSPACE(old_parent);
        g_hash_table_remove(self->workspaces->workspaces, workspace->name);
    }

    _wh_container_hide(self);

set:
    self->parent = parent;
    if ( self->parent == NULL )
        return;

    length = g_queue_get_length(parent->children);

    g_queue_push_tail_link(parent->children, self->link);
    g_queue_push_tail_link(parent->history, self->history_link);

    _wh_container_resize(parent);
    if ( parent->visible )
        _wh_container_show(parent);
}

static void
_wh_container_init(WhContainer *self, WhWorkspaces *workspaces, WhContainerType type)
{
    self->workspaces = workspaces;
    self->type = type;

    self->children = g_queue_new();
    self->link = g_list_alloc();
    self->link->data = self;
    self->history = g_queue_new();
    self->history_link = g_list_alloc();
    self->history_link->data = self;
}

static void
_wh_container_uninit(WhContainer *self)
{
    if ( ! WH_CONTAINER_IS_WORKSPACE(self) )
        _wh_container_reparent(self, NULL);
    else
    {
        g_queue_unlink(self->workspaces->history, self->history_link);
        self->workspaces->workspaces_sorted = g_list_remove_link(self->workspaces->workspaces_sorted, self->link);
    }

    g_queue_free(self->history);
    g_queue_free(self->children);

    g_list_free_1(self->history_link);
    g_list_free_1(self->link);
}

static WhContainer *
_wh_container_new(WhWorkspaces *workspaces)
{
    WhContainer *self;

    self = g_new0(WhContainer, 1);
    _wh_container_init(self, workspaces, WH_CONTAINER_TYPE_CONTAINER);

    return self;
}

static void
_wh_container_free(WhContainer *self)
{
    g_return_if_fail(! WH_CONTAINER_IS_WORKSPACE(self));
    g_return_if_fail(! WH_CONTAINER_IS_SURFACE(self));

    _wh_container_uninit(self);
    g_free(self);
}

static void
_wh_workspace_set_output(WhWorkspace *self, WhOutput *output)
{
    if ( output == NULL )
    {
        WhWorkspace *last;
        last = g_queue_peek_head(self->container.workspaces->history);
        output = last->output;
    }
    self->output = output;
    self->container.geometry = wh_output_get_geometry(self->output);
    _wh_container_resize(&self->container);
}

static guint64
_wh_workspaces_get_next_number(WhWorkspaces *self)
{
    guint64 number = 0;
    GList *workspace_;
    for ( workspace_ = self->workspaces_sorted ; workspace_ != NULL ; workspace_ = g_list_next(workspace_) )
    {
        WhWorkspace *workspace = workspace_->data;
        if ( workspace->number == WH_WORKSPACE_NO_NUMBER )
            break;
        if ( workspace->number > number )
            break;
        ++number;
    }
    return number;
}

static gint
_wh_workspace_compare(gconstpointer a_, gconstpointer b_)
{
    const WhWorkspace *a = a_, *b = b_;

    if ( ( a->number == WH_WORKSPACE_NO_NUMBER ) && ( b->number == WH_WORKSPACE_NO_NUMBER ) )
        return g_strcmp0(a->name, b->name);

    if ( a->number == WH_WORKSPACE_NO_NUMBER )
        return 1;
    if ( b->number == WH_WORKSPACE_NO_NUMBER )
        return -1;

    return a->number - b->number;
}

static WhWorkspace *
_wh_workspace_new(WhWorkspaces *workspaces, guint64 number, const gchar *name)
{
    WhWorkspace *self;
    self = g_new0(WhWorkspace, 1);
    _wh_container_init(&self->container, workspaces, WH_CONTAINER_TYPE_WORKSPACE);
    if ( name != NULL )
    {
        gchar *e;

        errno = 0;
        number = g_ascii_strtoull(name, &e, 10);
        if ( ( errno != 0 ) || ( e == name ) )
            number = WH_WORKSPACE_NO_NUMBER;
        self->number = number;
        self->name = g_strdup(name);
    }
    else
    {
        if ( number == WH_WORKSPACE_NO_NUMBER )
            number = _wh_workspaces_get_next_number(workspaces);
        self->number = number;
        self->name = g_strdup_printf("%" G_GUINT64_FORMAT, self->number);
    }

    if ( workspaces->workspaces == NULL )
        self->container.current = TRUE;

    g_hash_table_insert(workspaces->workspaces, self->name, self);
    workspaces->workspaces_sorted = g_list_concat(self->container.link, workspaces->workspaces_sorted);
    workspaces->workspaces_sorted = g_list_sort(workspaces->workspaces_sorted, _wh_workspace_compare);
    g_queue_push_tail_link(workspaces->history, self->container.history_link);

    return self;
}

static void
_wh_workspace_free(gpointer data)
{
    WhWorkspace *self = data;
    _wh_container_hide(&self->container);
    _wh_container_uninit(&self->container);

    WhWorkspaces *workspaces = self->container.workspaces;

    if ( ( self->number != WH_WORKSPACE_NO_NUMBER ) && ( self->number == workspaces->workspace_biggest ) )
    {
        workspaces->workspace_biggest = 0;
        GList *workspace_;
        for ( workspace_ = workspaces->workspaces_sorted ; workspace_ != NULL ; workspace_ = g_list_next(workspace_) )
        {
            WhWorkspace *workspace = workspace_->data;

            if ( ( workspace->number != WH_WORKSPACE_NO_NUMBER ) && ( workspace->number > workspaces->workspace_biggest ) )
                workspaces->workspace_biggest = workspace->number;
        }
    }

    g_free(self->name);

    g_free(self);
}

static void
_wh_container_hide(WhContainer *self)
{
    self->visible = FALSE;

    if ( WH_CONTAINER_IS_SURFACE(self) )
    {
        WhSurface *surface = WH_CONTAINER_SURFACE(self);

        weston_view_damage_below(surface->view);
        weston_layer_entry_remove(&surface->view->layer_link);
    }
    else
    {
        GList *child;
        for ( child = g_queue_peek_head_link(self->children) ; child != NULL ; child = g_list_next(child) )
            _wh_container_hide(child->data);
    }

    /* FIXME ?
    if ( self->workspaces->current == self )
    {
        if ( ! WH_CONTAINER_IS_WORKSPACE(self) )
            self->workspaces->current = self->parent;
        else
        {
            GList *workspace_;
            for ( workspace_ = self->workspaces->workspaces ; workspace_ != NULL ; workspace_ = g_list_next(workspace_) )
            {
                WhWorkspace *workspace = workspace_->data;
                if ( workspace->container.visible )
                {
                    self->workspaces->current = &workspace->container;
                    break;
                }
            }
        }
    }
    * */
}

static void
_wh_container_show(WhContainer *self)
{
    self->visible = TRUE;
    if ( WH_CONTAINER_IS_SURFACE(self) )
    {
        WhSurface *surface = WH_CONTAINER_SURFACE(self);

        weston_view_geometry_dirty(surface->view);
        weston_layer_entry_remove(&surface->view->layer_link);
        if ( weston_desktop_surface_get_fullscreen(surface->desktop_surface) )
            weston_layer_entry_insert(&self->workspaces->fullscreen_layer.view_list, &surface->view->layer_link);
        else
            weston_layer_entry_insert(&self->workspaces->layer.view_list, &surface->view->layer_link);
        weston_desktop_surface_propagate_layer(surface->desktop_surface);
        weston_view_geometry_dirty(surface->view);
        weston_surface_damage(surface->surface);
    }
    else
    {
        gboolean first = TRUE;
        GList *child;
        for ( child = g_queue_peek_head_link(self->children) ; child != NULL ; child = g_list_next(child) )
        {
            switch ( self->layout )
            {
            case WH_CONTAINER_LAYOUT_TABBED_HORIZONTAL:
            case WH_CONTAINER_LAYOUT_TABBED_VERTICAL:
                if ( ! first )
                    _wh_container_hide(child->data);
                else
            case WH_CONTAINER_LAYOUT_SPLIT_HORIZONTAL:
            case WH_CONTAINER_LAYOUT_SPLIT_VERTICAL:
                    _wh_container_show(child->data);
                first = FALSE;
            break;
            }
        }
    }
}

void
wh_workspace_show(WhWorkspace *workspace)
{
    WhContainer *self = &workspace->container;
    g_debug("Show workspaces %s", workspace->name);

    g_queue_unlink(self->workspaces->history, self->history_link);
    g_queue_push_head_link(self->workspaces->history, self->history_link);

    _wh_container_show(self);
}

void
wh_workspace_hide(WhWorkspace *workspace)
{
    WhContainer *self = &workspace->container;
    g_debug("Hide workspaces %s", workspace->name);

    _wh_container_hide(self);

    if ( g_queue_is_empty(self->children) )
        g_hash_table_remove(self->workspaces->workspaces, workspace->name);
}

WhWorkspaces *
wh_workspaces_new(WhCore *core)
{
    struct weston_compositor *compositor = wh_core_get_compositor(core);
    WhWorkspaces *self;

    self = g_new0(WhWorkspaces, 1);
    self->core = core;

    self->workspaces = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, _wh_workspace_free);
    self->workspaces_by_number = g_hash_table_new(NULL, NULL);

    self->history = g_queue_new();
    weston_layer_init(&self->fullscreen_layer, compositor);
    weston_layer_set_position(&self->fullscreen_layer, WESTON_LAYER_POSITION_FULLSCREEN);
    weston_layer_init(&self->layer, compositor);
    weston_layer_set_position(&self->layer, WESTON_LAYER_POSITION_NORMAL);

    return self;
}

void
wh_workspaces_free(WhWorkspaces *self)
{
    if ( self == NULL )
        return;

    g_hash_table_unref(self->workspaces_by_number);
    g_hash_table_unref(self->workspaces);

    g_queue_free(self->history);

    g_free(self);
}

void
wh_workspaces_add_output(WhWorkspaces *self, WhOutput *output)
{
    WhWorkspace *workspace;

    /*
     * TODO: assign workspaces, check config
     */
    workspace = _wh_workspace_new(self, WH_WORKSPACE_NO_NUMBER, NULL);
    _wh_workspace_set_output(workspace, output);
    wh_output_set_current_workspace(output, workspace);
    wh_workspace_show(workspace);
}

static WhContainer *
_wh_workspace_get_last(WhContainer *self)
{
    WhContainer *con = self;
    while ( ! g_queue_is_empty(con->history) )
        con = g_queue_peek_head(con->history);
    return con;
}

static WhContainer *
_wh_workspace_get_current(WhWorkspace *self)
{
    WhContainer *con, *next;
    for ( con = &self->container ; ! g_queue_is_empty(con->history) ; con = next )
    {
        next = g_queue_peek_head(con->history);
        if ( ! next->current )
            return con;
    }
    return con;
}

static WhContainer *
_wh_workspaces_get_current(WhWorkspaces *self)
{
    return _wh_workspace_get_current(g_queue_peek_head(self->history));
}

static void
_wh_workspaces_set_current_recurse(WhContainer *self, gboolean current)
{
    self->current = current;

    GQueue *history;
    if ( WH_CONTAINER_IS_WORKSPACE(self) )
        history = self->workspaces->history;
    else
    {
        history = self->parent->history;
        _wh_workspaces_set_current_recurse(self->parent, current);
    }
    g_queue_unlink(history, self->history_link);
    g_queue_push_head_link(history, self->history_link);
}

static void
_wh_workspaces_set_current(WhWorkspaces *self, WhContainer *next)
{
    WhContainer *current;

    current = _wh_workspaces_get_current(self);

    if ( current == next )
        return;

    _wh_workspaces_set_current_recurse(current, FALSE);
    _wh_workspaces_set_current_recurse(next, TRUE);

    if ( WH_CONTAINER_IS_SURFACE(next) )
        wh_core_set_focus(self->core, WH_CONTAINER_SURFACE(next));
    else if ( WH_CONTAINER_IS_SURFACE(current) )
        wh_core_set_focus(self->core, NULL);
}

static WhWorkspace *
_wh_container_get_workspace(WhContainer *self)
{
    WhContainer *con = self;
    while ( ! WH_CONTAINER_IS_WORKSPACE(con) )
        con = con->parent;
    return WH_CONTAINER_WORKSPACE(con);
}

static WhContainer *
_wh_container_get(WhContainer *self, WhDirection direction)
{
    if ( direction & WH_DIRECTION_TREE_MASK )
    {
        switch ( WH_DIRECTION_GET_TARGET(direction) )
        {
        case WH_TARGET_PREVIOUS:
            if ( ! WH_CONTAINER_IS_WORKSPACE(self) )
                return self->parent;
        break;
        case WH_TARGET_NEXT:
            if ( ! WH_CONTAINER_IS_SURFACE(self) )
                return g_queue_peek_head(self->history);
        break;
        default:
            g_return_val_if_reached(NULL);
        }
        return self;
    }

    WhWorkspace *workspace;
    WhOutput *output = NULL;
    workspace = _wh_container_get_workspace(self);
    output = wh_outputs_get(wh_core_get_outputs(self->workspaces->core), workspace->output, direction);

    if ( ! WH_CONTAINER_IS_WORKSPACE(self) )
    {
        if ( WH_CONTAINER_LAYOUT_GET_ORIENTATION(self->parent->layout) == WH_DIRECTION_GET_ORIENTATION(direction) )
        {
            gpointer (*getter)(GQueue *queue);
            switch ( WH_DIRECTION_GET_TARGET(direction) )
            {
            case WH_TARGET_PREVIOUS:
                if ( self->link->prev != NULL )
                    return self->link->prev->data;
                getter = g_queue_peek_tail;
            break;
            case WH_TARGET_NEXT:
                if ( self->link->next != NULL )
                    return self->link->next->data;
                getter = g_queue_peek_head;
            break;
            default:
                g_return_val_if_reached(NULL);
            }
            WhContainer *target;
            target = _wh_container_get(self->parent, direction);
            if ( target != self->parent )
                return _wh_workspace_get_last(target);
            if ( output == NULL )
                return getter(self->parent->children);
        }
    }

    if ( output != NULL )
    {
        workspace = wh_output_get_current_workspace(output);
        return _wh_workspace_get_last(&workspace->container);
    }

    return self;
}

void
wh_workspaces_focus_container(WhWorkspaces *self, WhSeat *seat, WhDirection direction)
{
    WhContainer *current, *next;

    current = _wh_workspaces_get_current(self);
    next = _wh_container_get(current, direction);

    if ( next == current )
        return;

    _wh_workspaces_set_current(self, next);
}

void
wh_workspaces_focus_workspace(WhWorkspaces *self, WhSeat *seat, WhTarget target)
{
    WhWorkspace *current = g_queue_peek_head(self->history), *workspace = NULL;
    switch ( target )
    {
    case WH_TARGET_NEXT:
        if ( current->container.link->next != NULL )
            workspace = current->container.link->next->data;
        /* TODO: prev output if any */
    break;
    case WH_TARGET_PREVIOUS:
        if ( current->container.link->prev != NULL )
            workspace = current->container.link->prev->data;
        /* TODO: prev output if any */
    break;
    case WH_TARGET_BACK_AND_FORTH:
        workspace = g_queue_peek_nth(self->history, 1);
    break;
    }
    if ( workspace == NULL )
        return;

    wh_output_set_current_workspace(workspace->output, workspace);
}

void
wh_workspaces_focus_workspace_name(WhWorkspaces *self, WhSeat *seat, const gchar *target)
{
    WhWorkspace *workspace;
    workspace = g_hash_table_lookup(self->workspaces, target);
    if ( workspace != NULL )
    {
        if ( ! wh_output_set_current_workspace(workspace->output, workspace) )
                wh_workspaces_focus_workspace(self, seat, WH_TARGET_BACK_AND_FORTH);
            return;
    }
    workspace = _wh_workspace_new(self, WH_WORKSPACE_NO_NUMBER, target);
    _wh_workspace_set_output(workspace, NULL);
    wh_output_set_current_workspace(workspace->output, workspace);
}

void
wh_workspaces_focus_workspace_number(WhWorkspaces *self, WhSeat *seat, guint64 target)
{
    WhWorkspace *workspace;
    workspace = g_hash_table_lookup(self->workspaces_by_number, GUINT_TO_POINTER(target));
    if ( workspace != NULL )
    {
        if ( ! wh_output_set_current_workspace(workspace->output, workspace) )
                wh_workspaces_focus_workspace(self, seat, WH_TARGET_BACK_AND_FORTH);
            return;
    }
    workspace = _wh_workspace_new(self, target, NULL);
    _wh_workspace_set_output(workspace, NULL);
    wh_output_set_current_workspace(workspace->output, workspace);
}

void
wh_workspaces_focus_output(WhWorkspaces *self, WhSeat *seat, WhDirection direction)
{
    if ( direction & WH_DIRECTION_TREE_MASK )
        g_return_if_reached();

    WhWorkspace *workspace;
    WhOutput *output;

    workspace = _wh_container_get_workspace(g_queue_peek_head(self->history));
    output = wh_outputs_get(wh_core_get_outputs(self->core), workspace->output, direction);
    if ( output == NULL )
        return;

    WhContainer *con;
    workspace = wh_output_get_current_workspace(output);
    con = _wh_workspace_get_last(&workspace->container);
    _wh_workspaces_set_current(self, con);
}

void
wh_workspaces_focus_output_name(WhWorkspaces *self, WhSeat *seat, const gchar *target)
{
}

void
wh_workspaces_move_container(WhWorkspaces *self, WhSeat *seat, WhDirection direction)
{
}

void
wh_workspaces_move_container_to_workspace(WhWorkspaces *self, WhSeat *seat, WhTarget target)
{
}

void
wh_workspaces_move_container_to_workspace_name(WhWorkspaces *self, WhSeat *seat, const gchar *target)
{
}

void
wh_workspaces_move_container_to_workspace_number(WhWorkspaces *self, WhSeat *seat, guint64 target)
{
}

void
wh_workspaces_move_workspace_to_output(WhWorkspaces *self, WhSeat *seat, WhDirection target)
{
}

void
wh_workspaces_move_workspace_to_output_name(WhWorkspaces *self, WhSeat *seat, const gchar *target)
{
}


void
wh_workspaces_layout_switch(WhWorkspaces *self, WhSeat *seat, WhContainerLayoutType type, WhOrientation orientation)
{
    WhContainer *con;

    con = _wh_workspaces_get_current(self);

    if ( WH_CONTAINER_IS_SURFACE(con) )
        con = con->parent;

    if ( orientation == WH_ORIENTATION_TOGGLE )
    {
        if ( ( con->layout & 1 ) != type )
            orientation = WH_ORIENTATION_HORIZONTAL;
        else
            orientation = ( ( ~con->layout >> 1 ) & 1 );
    }

    guint layout = type | ( orientation << 1 );

    if ( con->layout == layout )
        return;

    con->layout = layout;
    _wh_container_resize(con);
    if ( con->visible )
        _wh_container_show(con);
}

static void
_wh_workspaces_refocus(WhWorkspaces *self)
{
    WhContainer *con;

    con = g_queue_peek_head(self->history);
    while ( ! g_queue_is_empty(con->history) )
        con = g_queue_peek_head(con->history);
    _wh_workspaces_set_current(self, con);
}


struct weston_view *
wh_surface_get_view(WhSurface *self)
{
    return self->view;
}

struct weston_surface *
wh_surface_get_surface(WhSurface *self)
{
    return self->surface;
}

void
wh_surface_set_size(WhSurface *self, gint32 width, gint32 height)
{
    weston_desktop_surface_set_size(self->desktop_surface, width, height);
    weston_view_set_mask(self->view, 0, 0, width, height);
    weston_view_update_transform(self->view);
}

void
wh_surface_set_activated(WhSurface *self, gboolean activated)
{
    if ( self == NULL )
        return;

    if ( activated )
        _wh_container_show(&self->container);
    weston_desktop_surface_set_activated(self->desktop_surface, activated);
}

void
wh_surface_fullscreen(WhSurface *self, WhStateChange change)
{
    gboolean fullscreen = FALSE;
    switch ( change )
    {
    case WH_STATE_ENABLE:
        fullscreen = TRUE;
    break;
    case WH_STATE_DISABLE:
        fullscreen = FALSE;
    break;
    case WH_STATE_TOGGLE:
        fullscreen = ! weston_desktop_surface_get_fullscreen(self->desktop_surface);
    break;
    }

    if ( fullscreen )
    {
        WhWorkspace *workspace;
        workspace = _wh_container_get_workspace(&self->container);
        wh_surface_set_size(self, workspace->container.geometry.width, workspace->container.geometry.height);
    }
    weston_desktop_surface_set_fullscreen(self->desktop_surface, fullscreen);
}

void
wh_surface_close(WhSurface *self)
{
    if ( self == NULL )
        return;
    weston_desktop_surface_close(self->desktop_surface);
}

static void
_wh_desktop_ping_timeout(struct weston_desktop_client *client, void *user_data)
{
}

static void
_wh_desktop_pong(struct weston_desktop_client *client, void *user_data)
{
}

static void
_wh_desktop_surface_added(struct weston_desktop_surface *surface, void *user_data)
{
    WhWorkspaces *workspaces = user_data;
    WhSurface *self;

    self = g_new0(WhSurface, 1);
    _wh_container_init(&self->container, workspaces, WH_CONTAINER_TYPE_SURFACE);
    self->desktop_surface = surface;

    weston_desktop_surface_set_user_data(self->desktop_surface, self);

    self->surface = weston_desktop_surface_get_surface(self->desktop_surface);
    self->view = weston_desktop_surface_create_view(self->desktop_surface);
    weston_desktop_surface_set_maximized(self->desktop_surface, true);

    const gchar *app_id;
    const WhWorkspaceConfig *config = NULL;
    WhContainer *parent = NULL;

    app_id = weston_desktop_surface_get_app_id(surface);
    if ( app_id != NULL )
        config = wh_config_get_assign(wh_core_get_config(workspaces->core), app_id);
    if ( config != NULL )
    {
        if ( config->name != NULL )
            parent = g_hash_table_lookup(workspaces->workspaces, config->name);
        else
            parent = g_hash_table_lookup(workspaces->workspaces_by_number, GUINT_TO_POINTER(config->number));
        if ( parent == NULL )
        {
            WhWorkspace *workspace;
            workspace = _wh_workspace_new(workspaces, config->number, config->name);
            _wh_workspace_set_output(workspace, NULL);
            parent = &workspace->container;
        }
    }

    if ( parent == NULL )
        parent = _wh_workspaces_get_current(workspaces);
    if ( parent == NULL )
        /* TODO: keep them around for when we have an output */
        return;
    if ( WH_CONTAINER_IS_SURFACE(parent) )
        parent = parent->parent;

    _wh_container_reparent(&self->container, parent);
    if ( parent->visible )
    {
        g_debug("Parent visible, showing us");
        _wh_container_show(parent);
    }

    /* TODO: some focus stealing prevention */
    if ( wh_core_get_focus(workspaces->core) == NULL )
    {
        g_debug("No focus, focusing ourselves");
        _wh_workspaces_set_current(workspaces, &self->container);
    }
}

static void
_wh_desktop_surface_removed(struct weston_desktop_surface *surface, void *user_data)
{
    WhWorkspaces *workspaces = user_data;
    WhSurface *self = weston_desktop_surface_get_user_data(surface);

    if ( self == NULL )
        return;

    gboolean refocus = ( wh_core_get_focus(workspaces->core) == self );

    if ( refocus )
        wh_core_set_focus(workspaces->core, NULL);

    _wh_container_uninit(&self->container);
    weston_desktop_surface_set_user_data(surface, NULL);
    g_free(self);

    if ( refocus )
        _wh_workspaces_refocus(workspaces);
}

static void
_wh_desktop_committed(struct weston_desktop_surface *surface, int32_t sx, int32_t sy, void *user_data)
{
    WhSurface *self = weston_desktop_surface_get_user_data(surface);
    struct weston_geometry geometry = weston_desktop_surface_get_geometry(self->desktop_surface);

    int32_t x, y;
    if ( weston_desktop_surface_get_fullscreen(self->desktop_surface) )
    {
        WhWorkspace *workspace;
        workspace = _wh_container_get_workspace(&self->container);
        x = workspace->container.geometry.x;
        y = workspace->container.geometry.y;
    }
    else
    {
        _wh_container_resize(self->container.parent);
        x = self->container.geometry.x;
        y = self->container.geometry.y;
    }
    x -= geometry.x;
    y -= geometry.y;
    weston_view_set_position(self->view, x, y);
    weston_view_set_mask_infinite(self->view);
    weston_view_update_transform(self->view);
}

static void
_wh_desktop_show_window_menu(struct weston_desktop_surface *surface, struct weston_seat *seat, int32_t x, int32_t y, void *user_data)
{
    /* TODO: make sure a surface is always in a non-tabbed/stacked container with its parent */
    g_warning("Client requesting window menu: not yet implemented");
}

static void
_wh_desktop_set_parent(struct weston_desktop_surface *surface, struct weston_desktop_surface *parent, void *user_data)
{
    /* TODO: make sure a surface is always in a non-tabbed/stacked container with its parent */
    g_warning("Client requesting parent relation: not yet implemented");
}

static void
_wh_desktop_move(struct weston_desktop_surface *surface, struct weston_seat *seat, uint32_t serial, void *user_data)
{
    g_warning("Client requesting move: unsupported");
}

static void
_wh_desktop_resize(struct weston_desktop_surface *surface, struct weston_seat *seat, uint32_t serial, enum weston_desktop_surface_edge edges, void *user_data)
{
    g_warning("Client requesting resize: unsupported");
}

static void
_wh_desktop_fullscreen_requested(struct weston_desktop_surface *surface, bool fullscreen, struct weston_output *output, void *user_data)
{
    WhSurface *self = weston_desktop_surface_get_user_data(surface);
    wh_surface_fullscreen(self, fullscreen ? WH_STATE_ENABLE : WH_STATE_DISABLE);
}

static void
_wh_desktop_maximized_requested(struct weston_desktop_surface *surface, bool maximized, void *user_data)
{
    g_warning("Client requesting maximized state: unsupported");
}

static void
_wh_desktop_minimized_requested(struct weston_desktop_surface *surface, void *user_data)
{
    g_warning("Client requesting minimized state: unsupported");
}

const struct weston_desktop_api wh_workspaces_desktop_api = {
    .struct_size = sizeof(struct weston_desktop_api),
    .ping_timeout = _wh_desktop_ping_timeout,
    .pong = _wh_desktop_pong,
    .surface_added = _wh_desktop_surface_added,
    .surface_removed = _wh_desktop_surface_removed,
    .committed = _wh_desktop_committed,
    .show_window_menu = _wh_desktop_show_window_menu,
    .set_parent = _wh_desktop_set_parent,
    .move = _wh_desktop_move,
    .resize = _wh_desktop_resize,
    .fullscreen_requested = _wh_desktop_fullscreen_requested,
    .maximized_requested = _wh_desktop_maximized_requested,
    .minimized_requested = _wh_desktop_minimized_requested,
};
