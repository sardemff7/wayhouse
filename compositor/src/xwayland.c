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

#include <errno.h>
#include <signal.h>
#include <sys/socket.h>

#include <glib-unix.h>

#include <compositor.h>
#include <xwayland-api.h>

#include "types.h"
#include "wayhouse.h"
#include "xwayland.h"

struct _WhXwayland {
    WhCore *core;
    const struct weston_xwayland_api *api;
    struct weston_xwayland *xwayland;
    GPid pid;
    struct wl_client *client;
    int wm_fd;
};

static void
_wh_xwayland_spawn_child(gpointer user_data)
{
    signal(SIGUSR1, SIG_IGN);
}

static gboolean
_wh_xwayland_sigusr1(gpointer user_data)
{
    WhXwayland *self = user_data;
    self->api->xserver_loaded(self->xwayland, self->client, self->wm_fd);
    return G_SOURCE_REMOVE;
}

static void
_wh_xwayland_child_watch(GPid pid, gint status, gpointer user_data)
{
    WhXwayland *self = user_data;

    self->api->xserver_exited(self->xwayland, status);

    self->client = NULL;

    g_spawn_close_pid(self->pid);
    self->pid = -1;
}

static pid_t
_wh_xwayland_spawn_xserver(void *user_data, const char *display, int abstract_fd, int unix_fd)
{
    WhXwayland *self = user_data;
    int wayland_pair[2], x_pair[2];
    int wayland_fd, x_fd;
    gchar wayland_fd_str[8], abstract_fd_str[8], unix_fd_str[8], x_fd_str[8];

    if ( socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wayland_pair) < 0 )
    {
        weston_log("wl connection socketpair failed\n");
        g_warning("Couldn't create Wayland socket pair: %s", g_strerror(errno));
        return -1;
    }

    if ( socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, x_pair) < 0 )
    {
        g_warning("Couldn't create X WM socket pair: %s", g_strerror(errno));
        return -1;
    }

#define check_dup(a, b) G_STMT_START { \
        a = dup(b); \
        if ( a < 0 ) \
        { \
            g_warning("Couldn't duplicate fd: %s", g_strerror(errno)); \
            return -1; \
        } \
    } G_STMT_END

    /* Remove CLOEXEC */
    check_dup(wayland_fd, wayland_pair[1]);
    check_dup(abstract_fd, abstract_fd);
    check_dup(unix_fd, unix_fd);
    check_dup(x_fd, x_pair[1]);

#undef check_dup

    close(wayland_pair[1]);
    close(x_pair[1]);

#define fd_to_str(name) g_snprintf(name##_fd_str, sizeof(name##_fd_str), "%d", name##_fd);
    fd_to_str(wayland);
    fd_to_str(abstract);
    fd_to_str(unix);
    fd_to_str(x);
#undef fd_to_str

    gchar *argv[] = {
        "Xwayland",
        (gchar *) display,
        "-rootless",
        "-listen", abstract_fd_str,
        "-listen", unix_fd_str,
        "-wm", x_fd_str,
        "-terminate",
        NULL
    };

    gchar **envp = g_get_environ();
    envp = g_environ_setenv(envp, "WAYLAND_SOCKET", wayland_fd_str, TRUE);

    GError *error = NULL;

    if ( g_spawn_async(NULL, argv, envp, G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN | G_SPAWN_DO_NOT_REAP_CHILD, _wh_xwayland_spawn_child, self, &self->pid, &error) )
    {
        g_child_watch_add(self->pid, _wh_xwayland_child_watch, self);
        g_unix_signal_add(SIGUSR1, _wh_xwayland_sigusr1, self);
        self->client = wl_client_create(wh_core_get_compositor(self->core)->wl_display, wayland_pair[0]);
        self->wm_fd = x_pair[0];
    }
    else
    {
        g_warning("Couldn't spawn Xwayland: %s", error->message);
        g_clear_error(&error);

        self->pid = -1;
        close(wayland_pair[0]);
        close(x_pair[0]);
    }

    /* Close child-side fds */
    close(wayland_fd);
    close(x_fd);
    close(abstract_fd);
    close(unix_fd);

    return self->pid;
}

WhXwayland *
wh_xwayland_new(WhCore *core)
{
    WhXwayland *self;
    struct weston_compositor *compositor;

    self = g_new0(WhXwayland, 1);
    self->core = core;
    self->pid = -1;

    compositor = wh_core_get_compositor(self->core);

    if ( weston_compositor_load_xwayland(compositor) < 0 )
        return NULL;

    self->api = weston_xwayland_get_api(compositor);
    if ( self->api == NULL )
    {
        g_warning("Failed to get the xwayland module API.");
        return NULL;
    }

    self->xwayland = self->api->get(compositor);
    if ( self->xwayland == NULL)
    {
        g_warning("Failed to get the xwayland object.");
        return NULL;
    }

    if ( self->api->listen(self->xwayland, self, _wh_xwayland_spawn_xserver) < 0 )
        return NULL;

    return self;
}

void
wh_xwayland_free(WhXwayland *self)
{
    if ( self->pid != -1 )
        g_spawn_close_pid(self->pid);

    g_free(self);
}
