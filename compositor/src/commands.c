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
#include <glib-object.h>
#include <nkutils-enum.h>

#include <wayland-server.h>
#include <libgwater-wayland-server.h>

#include <compositor-wayland.h>
#include <compositor-x11.h>
#include <compositor-drm.h>
#include <libinput.h>

#include "types.h"
#include "wayhouse.h"
#include "containers.h"
#include "commands.h"

struct _WhCommands {
    WhCore *core;
    GScanner *scanner;
};

typedef enum {
    WH_COMMAND_SCOPE_ROOT = 0,
    WH_COMMAND_SCOPE_DIRECTION,
    WH_COMMAND_SCOPE_DIRECTION_CROSS,
    WH_COMMAND_SCOPE_TARGET,
    WH_COMMAND_SCOPE_LAYOUT,
    WH_COMMAND_SCOPE_ORIENTATION,
    WH_COMMAND_SCOPE_STATE_CHANGE,
} WhCommandScope;

typedef enum {
    WH_COMMAND_QUIT,
    WH_COMMAND_CLOSE,
    WH_COMMAND_FOCUS,
    WH_COMMAND_MOVE,
    WH_COMMAND_FULLSCREEN,
    WH_COMMAND_LAYOUT,
} WhCommandCommandSymbol;


static const gchar * const _wh_commands_symbols[] = {
    [WH_COMMAND_QUIT]       = "quit",
    [WH_COMMAND_CLOSE]      = "close",
    [WH_COMMAND_FOCUS]      = "focus",
    [WH_COMMAND_MOVE]       = "move",
    [WH_COMMAND_FULLSCREEN] = "fullscreen",
    [WH_COMMAND_LAYOUT]     = "layout",
};

#define WH_DIRECTION_WORKSPACE (WH_DIRECTION_CHILD+1)
#define WH_DIRECTION_OUTPUT (WH_DIRECTION_CHILD+2)
static const gchar * const _wh_commands_directions[] = {
    [WH_DIRECTION_LEFT]   = "left",
    [WH_DIRECTION_RIGHT]  = "right",
    [WH_DIRECTION_TOP]    = "top",
    [WH_DIRECTION_BOTTOM] = "bottom",
    [WH_DIRECTION_PARENT] = "parent",
    [WH_DIRECTION_CHILD]  = "child",
    [WH_DIRECTION_WORKSPACE] = "workspace",
    [WH_DIRECTION_OUTPUT] = "output",
};

static const gchar * const _wh_commands_cross_directions[] = {
    [WH_DIRECTION_LEFT]   = "left",
    [WH_DIRECTION_RIGHT]  = "right",
    [WH_DIRECTION_TOP]    = "top",
    [WH_DIRECTION_BOTTOM] = "bottom",
};

static const gchar * const _wh_commands_targets[] = {
    [WH_TARGET_NEXT] = "next",
    [WH_TARGET_PREVIOUS] = "previous",
    [WH_TARGET_BACK_AND_FORTH] = "back-and-forth",
};

static const gchar * const _wh_commands_layout_types[] = {
    [WH_CONTAINER_LAYOUT_TABBED] = "tabbed",
    [WH_CONTAINER_LAYOUT_SPLIT] = "split",
};
static const gchar * const _wh_commands_layout_orientations[] = {
    [WH_ORIENTATION_HORIZONTAL] = "horizontal",
    [WH_ORIENTATION_VERTICAL]   = "vertical",
    [WH_ORIENTATION_TOGGLE]     = "toggle",
};

static const gchar * const _wh_commands_state_changes[] = {
    [WH_SURFACE_STATE_ENABLE] = "enable",
    [WH_SURFACE_STATE_DISABLE] = "disable",
    [WH_SURFACE_STATE_TOGGLE] = "toggle",
};

typedef enum {
    WH_COMMAND_TARGET_TYPE_ERROR,
    WH_COMMAND_TARGET_TYPE_DIRECTION,
    WH_COMMAND_TARGET_TYPE_WORKSPACE_DIRECTION,
    WH_COMMAND_TARGET_TYPE_WORKSPACE_NAME,
    WH_COMMAND_TARGET_TYPE_WORKSPACE_NUMBER,
    WH_COMMAND_TARGET_TYPE_OUTPUT_DIRECTION,
    WH_COMMAND_TARGET_TYPE_OUTPUT_NAME,
} WhCommandTargetType;

typedef gpointer (*WhCoreGetter)(WhCore *core);
#define WH_CORE_GETTER(func) ((WhCoreGetter) (func))

struct _WhCommand {
    WhCommands *commands;
    gchar *string;
    WhCoreGetter getter;
    GClosure *closure;
    guint n_params;
    GValue params[4];
};

static WhCommandTargetType
_wh_command_parse_target(GScanner *scanner, WhCommand *self)
{
    g_scanner_set_scope(scanner, WH_COMMAND_SCOPE_DIRECTION);
    if ( g_scanner_get_next_token(scanner) != G_TOKEN_SYMBOL )
        return WH_COMMAND_TARGET_TYPE_ERROR;

    switch ( scanner->value.v_int64 )
    {
    case WH_DIRECTION_WORKSPACE:
        g_scanner_set_scope(scanner, WH_COMMAND_SCOPE_TARGET);
        switch ( g_scanner_get_next_token(scanner) )
        {
        case G_TOKEN_SYMBOL:
            return WH_COMMAND_TARGET_TYPE_WORKSPACE_DIRECTION;
        break;
        case G_TOKEN_STRING:
            return WH_COMMAND_TARGET_TYPE_WORKSPACE_NAME;
        case G_TOKEN_INT:
            return WH_COMMAND_TARGET_TYPE_WORKSPACE_NUMBER;
        default:
        break;
        }
    break;
    case WH_DIRECTION_OUTPUT:
        g_scanner_set_scope(scanner, WH_COMMAND_SCOPE_DIRECTION_CROSS);
        switch ( g_scanner_get_next_token(scanner) )
        {
        case G_TOKEN_SYMBOL:
            return WH_COMMAND_TARGET_TYPE_OUTPUT_DIRECTION;
        break;
        case G_TOKEN_STRING:
            return WH_COMMAND_TARGET_TYPE_OUTPUT_NAME;
        default:
        break;
        }
    break;
    default:
        return WH_COMMAND_TARGET_TYPE_DIRECTION;
    }
    return WH_COMMAND_TARGET_TYPE_ERROR;
}

static gboolean
_wh_command_parse_layout(GScanner *scanner, WhCommand *self)
{
    g_scanner_set_scope(scanner, WH_COMMAND_SCOPE_LAYOUT);
    if ( g_scanner_get_next_token(scanner) != G_TOKEN_SYMBOL )
        return FALSE;

    g_value_init(&self->params[self->n_params], G_TYPE_UINT64);
    g_value_set_uint64(&self->params[self->n_params], scanner->value.v_int64);
    ++self->n_params;

    switch ( g_scanner_peek_next_token(scanner) )
    {
    case G_TOKEN_EOF:
        scanner->value.v_int64 = WH_ORIENTATION_TOGGLE;
    break;
    case G_TOKEN_SYMBOL:
        g_scanner_get_next_token(scanner);
    break;
    default:
        return FALSE;
    }

    return TRUE;
}

static gboolean
_wh_command_parse_state_change(GScanner *scanner, WhCommand *self)
{
    g_scanner_set_scope(scanner, WH_COMMAND_SCOPE_STATE_CHANGE);
    return ( g_scanner_get_next_token(scanner) == G_TOKEN_SYMBOL );
}

static gboolean
_wh_command_parse_command(GScanner *scanner, WhCommand *self)
{
    g_scanner_set_scope(scanner, WH_COMMAND_SCOPE_ROOT);
    if ( g_scanner_get_next_token(scanner) != G_TOKEN_SYMBOL )
        return FALSE;

    /* We use int64 here since we pass enum values as symbol values */
    switch ( scanner->value.v_int64 )
    {
    case WH_COMMAND_QUIT:
        self->closure = g_cclosure_new(G_CALLBACK(wh_stop), NULL, NULL);
        return TRUE;
    case WH_COMMAND_CLOSE:
        self->closure = g_cclosure_new(G_CALLBACK(wh_surface_close), NULL, NULL);
        self->getter = WH_CORE_GETTER(wh_core_get_focus);
        return TRUE;
    case WH_COMMAND_FOCUS:
        switch ( _wh_command_parse_target(scanner, self) )
        {
        case WH_COMMAND_TARGET_TYPE_ERROR:
            return FALSE;
        case WH_COMMAND_TARGET_TYPE_DIRECTION:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_focus_container), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_WORKSPACE_DIRECTION:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_focus_workspace), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_WORKSPACE_NAME:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_focus_workspace_name), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_WORKSPACE_NUMBER:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_focus_workspace_number), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_OUTPUT_DIRECTION:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_focus_output), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_OUTPUT_NAME:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_focus_output_name), NULL, NULL);
        break;
        }
        self->getter = WH_CORE_GETTER(wh_core_get_workspaces);
        return TRUE;
    case WH_COMMAND_MOVE:
        switch ( _wh_command_parse_target(scanner, self) )
        {
        case WH_COMMAND_TARGET_TYPE_ERROR:
            return FALSE;
        case WH_COMMAND_TARGET_TYPE_DIRECTION:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_move_container), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_WORKSPACE_DIRECTION:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_move_container_to_workspace), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_WORKSPACE_NAME:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_move_container_to_workspace_name), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_WORKSPACE_NUMBER:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_move_container_to_workspace_number), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_OUTPUT_DIRECTION:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_move_workspace_to_output), NULL, NULL);
        break;
        case WH_COMMAND_TARGET_TYPE_OUTPUT_NAME:
            self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_move_workspace_to_output_name), NULL, NULL);
        break;
        }
        self->getter = WH_CORE_GETTER(wh_core_get_workspaces);
        return TRUE;
    case WH_COMMAND_FULLSCREEN:
        if ( ! _wh_command_parse_state_change(scanner, self) )
            return FALSE;
        self->closure = g_cclosure_new(G_CALLBACK(wh_surface_fullscreen), NULL, NULL);
        self->getter = WH_CORE_GETTER(wh_core_get_focus);
        return TRUE;
    case WH_COMMAND_LAYOUT:
        if ( ! _wh_command_parse_layout(scanner, self) )
            return FALSE;
        self->getter = WH_CORE_GETTER(wh_core_get_workspaces);
        self->closure = g_cclosure_new(G_CALLBACK(wh_workspaces_layout_switch), NULL, NULL);
        return TRUE;
    }

    return FALSE;
}

WhCommand *
wh_command_parse(WhCommands *commands, gchar *string)
{
    GScanner *scanner = commands->scanner;
    WhCommand *self;

    self = g_slice_new0(WhCommand);
    self->commands = commands;
    self->string = string;

    g_scanner_input_text(scanner, string, strlen(string));
    self->n_params = 2;
    g_value_init(&self->params[0], G_TYPE_POINTER);
    g_value_init(&self->params[1], G_TYPE_POINTER);

    if ( ! _wh_command_parse_command(scanner, self) )
    {
        wh_command_free(self);
        return NULL;
    }

    switch ( scanner->token )
    {
    case G_TOKEN_SYMBOL:
    case G_TOKEN_INT:
        g_value_init(&self->params[self->n_params], G_TYPE_UINT64);
        g_value_set_uint64(&self->params[self->n_params], scanner->value.v_int64);
        ++self->n_params;
    break;
    case G_TOKEN_STRING:
        g_value_init(&self->params[self->n_params], G_TYPE_STRING);
        g_value_set_string(&self->params[self->n_params], scanner->value.v_string);
        ++self->n_params;
    break;
    default:
    break;
    }

    if ( ( scanner->token != G_TOKEN_EOF ) && ( g_scanner_get_next_token(scanner) != G_TOKEN_EOF ) )
    {
        g_warning("Garbage at the end of the command: %s", string + g_scanner_cur_position(scanner));
        wh_command_free(self);
        return NULL;
    }

    g_closure_set_marshal(self->closure, g_cclosure_marshal_generic);

    return self;
}

void
wh_command_free(WhCommand *self)
{
    guint i;
    for ( i = 0 ; i < self->n_params ; ++i )
        g_value_unset(&self->params[i]);
    if ( self->closure != NULL )
        g_closure_unref(self->closure);
    g_free(self->string);

    g_slice_free(WhCommand, self);
}

void
wh_command_call(WhCommand *self, WhSeat *seat)
{
    gpointer target;

    if ( self->getter != NULL )
        target = self->getter(self->commands->core);
    else
        target = self->commands->core;
    g_value_set_pointer(&self->params[0], target);
    g_value_set_pointer(&self->params[1], seat);
    g_closure_invoke(self->closure, NULL, self->n_params, self->params, NULL);
}

const gchar *
wh_command_get_string(WhCommand *self)
{
    return self->string;
}

#define _wh_commands_add_symbols(scanner, scope, list) G_STMT_START { \
        for ( i = 0 ; i < G_N_ELEMENTS(list) ; ++i ) \
        { \
            if ( list[i] != NULL ) \
                g_scanner_scope_add_symbol(scanner, scope, list[i], GUINT_TO_POINTER(i)); \
        } \
    } G_STMT_END

WhCommands *
wh_commands_new(WhCore *core)
{
    WhCommands *self;

    self = g_new0(WhCommands, 1);
    self->core = core;

    self->scanner = g_scanner_new(NULL);
    self->scanner->config->store_int64 = TRUE;

    guint i;
    _wh_commands_add_symbols(self->scanner, WH_COMMAND_SCOPE_ROOT, _wh_commands_symbols);
    _wh_commands_add_symbols(self->scanner, WH_COMMAND_SCOPE_DIRECTION, _wh_commands_directions);
    _wh_commands_add_symbols(self->scanner, WH_COMMAND_SCOPE_DIRECTION_CROSS, _wh_commands_cross_directions);
    _wh_commands_add_symbols(self->scanner, WH_COMMAND_SCOPE_TARGET, _wh_commands_targets);
    _wh_commands_add_symbols(self->scanner, WH_COMMAND_SCOPE_LAYOUT, _wh_commands_layout_types);
    _wh_commands_add_symbols(self->scanner, WH_COMMAND_SCOPE_ORIENTATION, _wh_commands_layout_orientations);

    return self;
}

void
wh_commands_free(WhCommands *self)
{
    if ( self == NULL )
        return;

    g_scanner_destroy(self->scanner);

    g_free(self);
}
