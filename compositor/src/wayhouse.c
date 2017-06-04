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
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif /* G_OS_UNIX */
#include <gmodule.h>

#include <wayland-server.h>
#include <libgwater-wayland-server.h>

#include <compositor.h>
#include <compositor-wayland.h>
#include <compositor-x11.h>
#include <compositor-drm.h>
#include <libweston-desktop.h>

#include "types.h"
#include "seats.h"
#include "outputs.h"
#include "containers.h"
#include "commands.h"
#include "config_.h"
#include "xwayland.h"
#include "wayhouse.h"

struct _WhCore {
    GWaterWaylandServerSource *source;
    struct wl_display *display;
    struct weston_compositor *compositor;
    struct weston_desktop *desktop;
    struct weston_desktop_api *desktop_api;
    struct weston_layer base;
    WhCommands *commands;
    WhConfig *config;
    WhSeats *seats;
    WhOutputs *outputs;
    WhWorkspaces *workspaces;
    WhXwayland *xwayland;
    WhSurface *focus;
    GMainLoop *loop;
};

struct weston_compositor *
wh_core_get_compositor(WhCore *context)
{
    return context->compositor;
}

WhCommands *
wh_core_get_commands(WhCore *context)
{
    return context->commands;
}

WhConfig *
wh_core_get_config(WhCore *context)
{
    return context->config;
}

WhSeats *
wh_core_get_seats(WhCore *context)
{
    return context->seats;
}

WhOutputs *
wh_core_get_outputs(WhCore *context)
{
    return context->outputs;
}

WhWorkspaces *
wh_core_get_workspaces(WhCore *context)
{
    return context->workspaces;
}

WhSurface *
wh_core_get_focus(WhCore *context)
{
    return context->focus;
}

void
wh_core_set_focus(WhCore *context, WhSurface *surface)
{
    wh_surface_set_activated(context->focus, FALSE);
    context->focus = surface;
    wh_seats_set_focus(context->seats, context->focus);
    wh_surface_set_activated(context->focus, TRUE);
}

static int
_wh_log(const char *format, va_list args)
{
    gsize l;
    gchar *format_;
    l = strlen(format);
    if ( format[l-1] == '\n' )
    {
        format_ = g_alloca(l);
        g_snprintf(format_, l, "%s", format);
        format = format_;
    }
    g_logv("libweston", G_LOG_LEVEL_DEBUG, format, args);
    return 0;
}

static gboolean
_wh_listen(WhCore *context, const gchar *socket_name)
{
    errno = 0;
    if ( socket_name != NULL )
    {
        if ( wl_display_add_socket(context->display, socket_name) < 0)
        {
            g_warning("Couldn’t add socket: %s\n", g_strerror(errno));
            return FALSE;
        }
    }
    else
    {
        socket_name = wl_display_add_socket_auto(context->display);
        if ( socket_name == NULL )
        {
            g_warning("Couldn’t add socket: %s\n", g_strerror(errno));
            return FALSE;
        }
    }

    g_setenv("WAYLAND_DISPLAY", socket_name, TRUE);
    g_unsetenv("DISPLAY");

    return TRUE;
}

static void
_wh_load_common_plugins(WhCore *context, const gchar * const *plugin)
{
    if ( plugin == NULL )
        return;

    for ( ; *plugin != NULL ; ++plugin )
    {
#ifdef LIBWESTON_HAS_COMMON_PLUGINS
        if ( weston_compositor_load_plugin(context->compositor, *plugin) < 0 )
            g_warning("Couldn’t load common plugin %s", *plugin);
#else /* ! LIBWESTON_HAS_COMMON_PLUGINS */
        gchar *path;
        GModule *mod;
        int (*init)(struct weston_compositor *compositor);

        if ( g_path_is_absolute(*plugin) )
            path = g_strdup(*plugin);
        else
            path = g_build_filename(LIBWESTON_PLUGINS_DIR, *plugin, NULL);
        g_debug("Try libweston plugin %s", path);
        mod = g_module_open(path, G_MODULE_BIND_LOCAL | G_MODULE_BIND_LAZY);
        if ( mod != NULL )
        {
            if ( ! g_module_symbol(mod, "weston_plugin_init", (gpointer *) &init) )
                g_debug("Couldn’t find init function for plugin %s", *plugin);
            else if ( init(context->compositor) < 0 )
                g_debug("Plugin init failed %s", *plugin);
            else
                mod = NULL;
            if ( mod != NULL )
                g_module_close(mod);
        }
        else
            g_debug("Couldn’t load plugin %s: %s", *plugin, g_module_error());
        g_free(path);
#endif /* ! LIBWESTON_HAS_COMMON_PLUGINS */
    }
}

static void
_wh_load_weston_plugins(WhCore *context, const gchar * const *plugin)
{
    if ( plugin == NULL )
        return;

    for ( ; *plugin != NULL ; ++plugin )
    {
        gchar *path;
        GModule *mod;
        int argc = 0;
        int (*init)(struct weston_compositor *compositor, int *argc, char *argv[]);

        if ( g_path_is_absolute(*plugin) )
            path = g_strdup(*plugin);
        else
            path = g_build_filename(WESTON_PLUGINS_DIR, *plugin, NULL);
        g_debug("Try weston plugin %s", path);
        mod = g_module_open(path, G_MODULE_BIND_LOCAL | G_MODULE_BIND_LAZY);
        if ( mod != NULL )
        {
            if ( ! g_module_symbol(mod, "wet_module_init", (gpointer *) &init) )
            {
                if ( ! g_module_symbol(mod, "module_init", (gpointer *) &init) )
                    g_debug("Couldn’t find init function for plugin %s", *plugin);
                else if ( init(context->compositor, &argc, NULL) < 0 )
                    g_debug("Plugin init failed %s", *plugin);
                else
                    mod = NULL;

            }
            else if ( init(context->compositor, &argc, NULL) < 0 )
                g_debug("Plugin init failed %s", *plugin);
            else
                mod = NULL;
            if ( mod != NULL )
                g_module_close(mod);
        }
        else
            g_debug("Couldn’t load plugin %s: %s", *plugin, g_module_error());
        g_free(path);
    }
}

void
wh_stop(WhCore *context, WhSeat *seat)
{
    if ( context->loop != NULL )
        g_main_loop_quit(context->loop);
}

static void
_wh_exit(struct weston_compositor *compositor)
{
    WhCore *context = weston_compositor_get_user_data(compositor);
    wh_stop(context, NULL);
}

#ifdef G_OS_UNIX
static gboolean
_wh_stop(gpointer user_data)
{
    wh_stop(user_data, NULL);
    return FALSE;
}
#endif /* G_OS_UNIX */

int
main(int argc, char *argv[])
{
#ifdef WAYHOUSE_DEBUG
    g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
#endif /* WAYHOUSE_DEBUG */

    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, WAYHOUSE_LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");

    if ( ! g_get_filename_charsets(NULL) )
    {
        g_warning(PACKAGE_NAME " does not support non-UTF-8 filename encoding");
        return 1;
    }


    if ( ! g_module_supported() )
    {
        g_warning("No module support");
        return 2;
    }

    WhCore *context;
    context = g_new0(WhCore, 1);

    int retval = 0;
    GError *error = NULL;
    gboolean use_pixman = FALSE;
    gchar *runtime_dir = NULL;
    gchar *socket_name = NULL;
    gchar **common_plugins = NULL;
    gchar **weston_plugins = NULL;
    gboolean print_version = FALSE;

    GOptionContext *option_context = NULL;
    GOptionEntry entries[] =
    {
        { "use-pixman",           'p', 0,                     G_OPTION_ARG_NONE,         &use_pixman,        "Use Pixman rendering",                  NULL },
        { "socket",               's', 0,                     G_OPTION_ARG_STRING,       &socket_name,       "Socket name to use",                    "<socket-name>" },
        { "common-plugins",       'm', 0,                     G_OPTION_ARG_STRING_ARRAY, &common_plugins,    "Common libweston plugins to load",      "<plugin>" },
        { "weston-plugins",       'w', 0,                     G_OPTION_ARG_STRING_ARRAY, &weston_plugins,    "weston plugins to load",                "<plugin>" },
        { "version",              'V', 0,                     G_OPTION_ARG_NONE,         &print_version,     "Print version",                         NULL },
        { .long_name = NULL }
    };

    option_context = g_option_context_new("- tiling Wayland compositor");
    g_option_context_add_main_entries(option_context, entries, GETTEXT_PACKAGE);
    if ( ! g_option_context_parse(option_context, &argc, &argv, &error) )
    {
        g_warning("Option parsing failed: %s\n", error->message);
        retval = 2;
        goto end;
    }
    g_option_context_free(option_context);

    if ( print_version )
    {
        g_printf(PACKAGE_NAME " " WAYHOUSE_VERSION "\n");
        goto end;
    }

    runtime_dir = g_build_filename(g_get_user_runtime_dir(), PACKAGE_NAME, NULL);
    if ( ( ! g_file_test(runtime_dir, G_FILE_TEST_IS_DIR) ) && ( g_mkdir_with_parents(runtime_dir, 0755) < 0 ) )
    {
        g_warning("Couldn't create the run dir '%s': %s", runtime_dir, g_strerror(errno));
        retval = 3;
        goto end;
    }

    weston_log_set_handler(_wh_log, _wh_log);

#ifdef G_OS_UNIX
    g_unix_signal_add(SIGTERM, _wh_stop, context);
    g_unix_signal_add(SIGINT, _wh_stop, context);

    /* Ignore SIGPIPE as it is useless */
    signal(SIGPIPE, SIG_IGN);
#endif /* G_OS_UNIX */

    context->source = g_water_wayland_server_source_new(NULL);
    context->display = g_water_wayland_server_source_get_display(context->source);
    context->compositor = weston_compositor_create(context->display, context);
    if ( context->compositor == NULL )
    {
        g_warning("Couldn’t create compositor");
        goto end;
    }

    context->compositor->vt_switching = 1;
    context->compositor->exit = _wh_exit;

    weston_layer_init(&context->base, context->compositor);
    weston_layer_set_position(&context->base, WESTON_LAYER_POSITION_BACKGROUND);

    context->seats = wh_seats_new(context);
    context->workspaces = wh_workspaces_new(context);
    context->outputs = wh_outputs_new(context);

    context->commands = wh_commands_new(context);
    context->config = wh_config_new(context, use_pixman);
    weston_compositor_set_xkb_rule_names(context->compositor, wh_config_get_xkb_names(context->config));
    if ( ! wh_config_load_backend(context->config) )
        goto error;

    context->desktop = weston_desktop_create(context->compositor, &wh_workspaces_desktop_api, context->workspaces);
    if ( context->desktop == NULL )
    {
        g_warning("Couldn’t create desktop");
        goto error;
    }

    if ( ! _wh_listen(context, socket_name) )
        goto error;

    if ( wh_config_get_xwayland(context->config) )
        context->xwayland = wh_xwayland_new(context);

    _wh_load_common_plugins(context, (const gchar * const *) common_plugins);
    _wh_load_common_plugins(context, wh_config_get_common_plugins(context->config));
    _wh_load_weston_plugins(context, (const gchar * const *) weston_plugins);

    gchar back_colour[8];
    gchar *back_argv[] = {
        "ww-background",
        "-c",
        back_colour,
        NULL
    };
    g_snprintf(back_colour, sizeof(back_colour), "#%06x", getpid());
    g_debug("Spawn %s %s %s", back_argv[0], back_argv[1], back_argv[2]);
    g_spawn_async(NULL, back_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

    weston_compositor_wake(context->compositor);

    context->loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(context->loop);
    g_main_loop_unref(context->loop);

    if ( wh_config_get_xwayland(context->config) )
        wh_xwayland_free(context->xwayland);

error:
    weston_desktop_destroy(context->desktop);
    weston_compositor_destroy(context->compositor);

    wh_config_free(context->config);
    wh_commands_free(context->commands);
    wh_outputs_free(context->outputs);
    wh_workspaces_free(context->workspaces);
    wh_seats_free(context->seats);

    g_water_wayland_server_source_free(context->source);

end:
    g_strfreev(weston_plugins);
    g_strfreev(common_plugins);
    g_free(runtime_dir);
    g_free(context);

    return retval;
}
