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
#include <glib/gprintf.h>
#include <nkutils-enum.h>

#include <wayland-server.h>
#include <libgwater-wayland-server.h>

#include <compositor-wayland.h>
#include <compositor-x11.h>
#include <compositor-drm.h>
#include <windowed-output-api.h>
#include <libinput.h>

#include "types.h"
#include "wayhouse.h"
#include "commands.h"
#include "seats.h"
#include "config_.h"

struct _WhConfig {
    WhCore *core;
    struct xkb_rule_names xkb_names;
    struct wl_listener output_pending_listener;
    enum weston_compositor_backend backend;
    union {
        struct weston_drm_backend_config drm;
        struct weston_wayland_backend_config wayland;
        struct weston_x11_backend_config x11;
    } backend_config;
    union {
        const struct weston_windowed_output_api *windowed;
        const struct weston_drm_output_api *drm;
    } api;
    struct {
        gboolean enable_tap_set;
        gboolean enable_tap;
    } input;
    GHashTable *outputs;
    GHashTable *output_aliases;
    gboolean xwayland;
    gchar **common_modules;
    GHashTable *assigns;
};

typedef struct {
    gchar *name;
    gchar *modeline;
    gint scale;
} WhConfigOutputDrm;

typedef struct {
    gchar *name;
    gint width;
    gint height;
    gint scale;
} WhConfigOutputVirtual;

typedef struct {
    WhConfig *config;
    enum {
        WH_ACTION_COMMAND,
        WH_ACTION_EXEC,
    } type;
    union {
        WhCommand *command;
        gchar **argv;
    };
} WhAction;

typedef enum {
    WH_MODIFIER_CTRL,
    WH_MODIFIER_ALT,
    WH_MODIFIER_SUPER,
    WH_MODIFIER_SHIFT,
} WhModifier;

static const gchar * const _wh_config_modifiers[] = {
    [WH_MODIFIER_CTRL]  = "ctrl",
    [WH_MODIFIER_ALT]   = "alt",
    [WH_MODIFIER_SUPER] = "super",
    [WH_MODIFIER_SHIFT] = "shift",
};

static void
_wh_config_workspace_config_free(gpointer data)
{
    WhWorkspaceConfig *self = data;
    g_free(self->name);
    g_slice_free(WhWorkspaceConfig, self);
}

static void
_wh_config_output_drm_free(gpointer data)
{
    g_slice_free(WhConfigOutputDrm, data);
}

static void
_wh_config_output_virtual_free(gpointer data)
{
    g_slice_free(WhConfigOutputVirtual, data);
}

static void
_wh_config_output_pending_drm(struct wl_listener *listener, void *data)
{
    WhConfig *self = wl_container_of(listener, self, output_pending_listener);
    struct weston_output *woutput = data;
    const gchar *name = woutput->name;
    const gchar *alias;
    WhConfigOutputDrm *output;

    alias = g_hash_table_lookup(self->output_aliases, name);
    if ( alias != NULL )
        name = alias;

    output = g_hash_table_lookup(self->outputs, name);
    if ( output == NULL )
        output = g_hash_table_lookup(self->outputs, "default");
    if ( output == NULL )
        return;

    self->api.drm->set_mode(woutput, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, output->modeline);
    self->api.drm->set_gbm_format(woutput, NULL);
    self->api.drm->set_seat(woutput, NULL);
    weston_output_set_scale(woutput, output->scale);
    weston_output_set_transform(woutput, WL_OUTPUT_TRANSFORM_NORMAL);
    weston_output_enable(woutput);
}

static void
_wh_config_output_pending_virtual(struct wl_listener *listener, void *data)
{
    WhConfig *self = wl_container_of(listener, self, output_pending_listener);
    struct weston_output *woutput = data;
    const gchar *name = woutput->name;
    const gchar *alias;
    WhConfigOutputVirtual *output;

    alias = g_hash_table_lookup(self->output_aliases, name);
    if ( alias != NULL )
        name = alias;

    output = g_hash_table_lookup(self->outputs, name);
    g_return_if_fail(output != NULL);

    weston_output_set_scale(woutput, output->scale);
    weston_output_set_transform(woutput, WL_OUTPUT_TRANSFORM_NORMAL);
    self->api.windowed->output_set_size(woutput, output->width, output->height);
    weston_output_enable(woutput);
}

static void
_wh_config_drm_input_configure(struct weston_compositor *compositor, struct libinput_device *device)
{
    WhConfig *self = wh_core_get_config(weston_compositor_get_user_data(compositor));
    gboolean enable_tap;

    if ( libinput_device_config_tap_get_finger_count(device) > 0 )
    {
        enable_tap = libinput_device_config_tap_get_default_enabled(device);
        if ( self->input.enable_tap_set )
            enable_tap = self->input.enable_tap;
        libinput_device_config_tap_set_enabled(device, enable_tap);
    }
}

static void
_wh_config_init(WhConfig *self, gboolean use_pixman)
{
    self->assigns = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, _wh_config_workspace_config_free);

    self->backend = WESTON_BACKEND_DRM;
    if ( g_getenv("WAYLAND_DISPLAY") != NULL )
        self->backend = WESTON_BACKEND_WAYLAND;
    else if ( g_getenv("DISPLAY") != NULL )
        self->backend = WESTON_BACKEND_X11;

    struct weston_backend_config *base = &self->backend_config.drm.base;
    GDestroyNotify output_free = NULL;
    switch ( self->backend )
    {
    case WESTON_BACKEND_DRM:
        base->struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
        base->struct_size = sizeof(struct weston_drm_backend_config);
        self->backend_config.drm.configure_device = _wh_config_drm_input_configure;
        self->backend_config.drm.use_pixman = use_pixman;
        output_free = _wh_config_output_drm_free;
    break;
    case WESTON_BACKEND_WAYLAND:
        base->struct_version = WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
        base->struct_size = sizeof(struct weston_wayland_backend_config);
        self->backend_config.wayland.use_pixman = use_pixman;
        output_free = _wh_config_output_virtual_free;
    break;
    case WESTON_BACKEND_X11:
        base->struct_version = WESTON_X11_BACKEND_CONFIG_VERSION;
        base->struct_size = sizeof(struct weston_x11_backend_config);
        self->backend_config.x11.use_pixman = use_pixman;
        output_free = _wh_config_output_virtual_free;
    break;
    default:
        /* Not supported */
        g_return_if_reached();
    }

    self->output_aliases = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    self->outputs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, output_free);
}

#define _wh_config_define_getter(name, type, ...) static gint \
_wh_config_get_##name(GKeyFile *file, const gchar *section, const gchar *key, type *value) \
{ \
    gint ret = 1; \
    GError *error = NULL; \
    \
    if ( g_key_file_has_key(file, section, key, &error) ) \
    { \
        *value = g_key_file_get_##name(file, section, key, ##__VA_ARGS__, &error); \
        if ( error != NULL ) \
            g_warning("Could not get [%s] %s: %s", section, key, error->message); \
        else \
            ret = 0; \
    } \
    else if ( error != NULL ) \
        g_warning("Could not check [%s] %s: %s", section, key, error->message); \
    \
    if ( error != NULL ) \
        g_error_free(error); \
    return ( error == NULL ) ? ret : -1; \
}

_wh_config_define_getter(boolean, gboolean)
_wh_config_define_getter(integer, gint)
_wh_config_define_getter(uint64, guint64)
_wh_config_define_getter(string, gchar *)
_wh_config_define_getter(string_list, gchar **, NULL)

static gint
_wh_config_get_enum(GKeyFile *file, const gchar *section, const gchar *key, const gchar * const *values, guint64 size, guint64 *value)
{
    gint ret;
    gchar *string;

    ret = _wh_config_get_string(file, section, key, &string);
    if ( ret != 0 )
        return ret;

    if ( ! nk_enum_parse(string, values, size, TRUE, value) )
        ret = -1;
    g_free(string);

    return ret;
}

static gint
_wh_config_get_command(WhConfig *self, GKeyFile *file, const gchar *section, const gchar *key, WhCommand **value)
{
    gint ret;
    gchar *string;

    ret = _wh_config_get_string(file, section, key, &string);
    if ( ret != 0 )
        return ret;

    *value = wh_command_parse(wh_core_get_commands(self->core), string);
    if ( *value == NULL )
        ret = -1;

    return ret;
}

static gint
_wh_config_get_argv(GKeyFile *file, const gchar *section, const gchar *key, gchar ***value)
{
    gint ret;
    gchar *string;

    ret = _wh_config_get_string(file, section, key, &string);
    if ( ret != 0 )
        return ret;

    gint argc;
    GError *error = NULL;
    if ( ! g_shell_parse_argv(string, &argc, value, &error) )
    {
        g_warning("Could not parse [%s)] %s: %s", section, key, error->message);
        g_error_free(error);
        ret = -1;
    }
    else if ( argc < 1 )
    {
        g_warning("Could not parse [%s)] %s: empty", section, key);
        g_strfreev(*value);
        *value = NULL;
        ret = -1;
    }

    return ret;
}

static WhAction *
_wh_config_binding_parse_common(WhConfig *config, GKeyFile *file, const gchar *section)
{
    WhCommand *command = NULL;
    gchar **argv = NULL;

    if ( ( _wh_config_get_command(config, file, section, "command", &command) != 0 )
         && ( _wh_config_get_argv(file, section, "exec", &argv) != 0 ) )
        goto error;

    WhAction *self;
    self = g_new0(WhAction, 1);
    self->config = config;

    self->type = ( command != NULL ) ? WH_ACTION_COMMAND : WH_ACTION_EXEC;
    switch ( self->type )
    {
    case WH_ACTION_COMMAND:
        self->command = command;
        g_debug("%s -> command %s", section, wh_command_get_string(command));
    break;
    case WH_ACTION_EXEC:
        self->argv = argv;
        g_debug("%s -> exec %s", section, argv[0]);
        argv = NULL;
    break;
    }

    return self;

error:
    g_strfreev(argv);
    return NULL;
}

static void
_wh_config_action_free(WhAction *self)
{
    switch ( self->type )
    {
    case WH_ACTION_COMMAND:
        wh_command_free(self->command);
    break;
    case WH_ACTION_EXEC:
        g_strfreev(self->argv);
    break;
    }

    g_free(self);
}

static void
_wh_config_action_trigger(WhAction *self, WhSeat *seat)
{
    switch ( self->type )
    {
    case WH_ACTION_COMMAND:
        g_debug("command %s", wh_command_get_string(self->command));
        wh_command_call(self->command, seat);
    break;
    case WH_ACTION_EXEC:
        g_debug("exec %s", self->argv[0]);
        g_spawn_async(NULL, self->argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
    break;
    }
}

static void
_wh_config_keybind_callback(struct weston_keyboard *keyboard, uint32_t time, uint32_t key, void *user_data)
{
    WhAction *self = user_data;
    WhSeat *seat = wh_seats_get_from_weston_seat(wh_core_get_seats(self->config->core), keyboard->seat);
    _wh_config_action_trigger(self, seat);
}

static const gchar *
_wh_config_binding_parse_key(const gchar *binding, enum weston_keyboard_modifier *modifiers)
{
    guint64 value;
    *modifiers = 0;
    if ( g_utf8_strchr(binding, -1, '+') == NULL )
    {
        if ( ! nk_enum_parse(binding, _wh_config_modifiers, G_N_ELEMENTS(_wh_config_modifiers), TRUE, &value) )
            return binding;
        *modifiers = (1 << value);
        return NULL;
    }

    const gchar *s, *c;
    for ( s = binding ; ( c = g_utf8_strchr(s, -1, '+') ) != NULL ; s = c + 1 )
    {
        gsize l = c - s + 1;
        gchar *b;

        b = g_alloca(l );
        g_snprintf(b, l, "%s", s);
        if ( nk_enum_parse(b, _wh_config_modifiers, G_N_ELEMENTS(_wh_config_modifiers), TRUE, &value) )
            *modifiers |= (1 << value);
    }
    return s;
}

static void
_wh_config_binding_parse_keycode(WhConfig *self, GKeyFile *file, const gchar *section)
{
    const gchar *binding = section + strlen("keycode ");

    enum weston_keyboard_modifier modifiers;
    binding = _wh_config_binding_parse_key(binding, &modifiers);

    if ( binding == NULL )
        return;

    gchar *e;
    guint64 key;

    errno = 0;
    key = g_ascii_strtoull(binding, &e, 10);
    if ( ( errno != 0 ) || ( e == binding ) || ( *e != '\0' ) || ( key > G_MAXUINT32 ) )
        return;

    WhAction *action;

    action = _wh_config_binding_parse_common(self, file, section);
    if ( action == NULL )
        return;

    weston_compositor_add_key_binding(wh_core_get_compositor(self->core), key, modifiers, _wh_config_keybind_callback, action);
}

static void
_wh_config_binding_parse_keysym(WhConfig *self, GKeyFile *file, const gchar *section)
{
    const gchar *binding = section + strlen("keysym ");
    WhAction *action;

    action = _wh_config_binding_parse_common(self, file, section);
    if ( action == NULL )
        return;

    (void)binding;
    _wh_config_action_free(action);
}

static void
_wh_config_binding_mouse_callback(struct weston_pointer *pointer, uint32_t time, uint32_t key, void *user_data)
{
    WhAction *self = user_data;
    WhSeat *seat = wh_seats_get_from_weston_seat(wh_core_get_seats(self->config->core), pointer->seat);
    _wh_config_action_trigger(self, seat);
}

static void
_wh_config_binding_parse_mouse(WhConfig *self, GKeyFile *file, const gchar *section)
{
    const gchar *binding = section + strlen("mouse ");

    enum weston_keyboard_modifier modifiers;
    binding = _wh_config_binding_parse_key(binding, &modifiers);

    if ( binding == NULL )
        return;

    gchar *e;
    guint64 button;

    errno = 0;
    button = g_ascii_strtoull(binding, &e, 10);
    if ( ( errno != 0 ) || ( e == binding ) || ( *e != '\0' ) || ( button > G_MAXUINT32 ) )
        return;

    WhAction *action;

    action = _wh_config_binding_parse_common(self, file, section);
    if ( action == NULL )
        return;

    weston_compositor_add_button_binding(wh_core_get_compositor(self->core), button, modifiers, _wh_config_binding_mouse_callback, action);
}

static void
_wh_config_workspace_parse(WhConfig *self, GKeyFile *file, const gchar *section)
{
    const gchar *name = section + strlen("workspace ");
    gchar **outputs = NULL;
    GList *workspaces;

    if ( _wh_config_get_string_list(file, section, "outputs", &outputs) < 0 )
        return;

    gchar **output;
    for ( output = outputs ; *output != NULL ; ++output )
    {

    }
}

static void
_wh_config_assign_parse(WhConfig *self, GKeyFile *file, const gchar *section)
{
    const gchar *app_id = section + strlen("assign ");

    guint64 number = WH_WORKSPACE_NO_NUMBER;
    gchar *name = NULL;

    if ( _wh_config_get_uint64(file, section, "number", &number) < 0 )
        return;
    if ( _wh_config_get_string(file, section, "name", &name) < 0 )
        return;

    if ( ( number == WH_WORKSPACE_NO_NUMBER ) && ( name == NULL ) )
        return;

    WhWorkspaceConfig *config;

    config = g_slice_new(WhWorkspaceConfig);
    config->number = number;
    config->name = name;

    g_hash_table_insert(self->assigns, g_strdup(app_id), config);
}

static void
_wh_config_global_parse(WhConfig *self, GKeyFile *file)
{
    if ( g_key_file_has_group(file, "wayhouse") )
    {
        _wh_config_get_boolean(file, "wayhouse", "xwayland", &self->xwayland);
        _wh_config_get_string_list(file, "wayhouse", "common-modules", &self->common_modules);
    }
    if ( g_key_file_has_group(file, "keymap") )
    {
        _wh_config_get_string(file, "keymap", "layout", (gchar **) &self->xkb_names.layout);
        _wh_config_get_string(file, "keymap", "variant", (gchar **) &self->xkb_names.variant);
    }

    gchar **groups;
    groups = g_key_file_get_groups(file, NULL);
    if ( groups == NULL )
        return;

    gchar **group;
    for ( group = groups ; *group != NULL ; ++group )
    {
        gsize l = strlen(*group);
        if ( g_str_has_prefix(*group, "keycode ") && ( l > strlen("keycode ") ) )
            _wh_config_binding_parse_keycode(self, file, *group);
        else if ( g_str_has_prefix(*group, "keysym ") && ( l > strlen("keysym ") ) )
            _wh_config_binding_parse_keysym(self, file, *group);
        else if ( g_str_has_prefix(*group, "mouse ") && ( l > strlen("mouse ") ) )
            _wh_config_binding_parse_mouse(self, file, *group);
        else if ( g_str_has_prefix(*group, "workspace ") && ( l > strlen("workspace ") ) )
            _wh_config_workspace_parse(self, file, *group);
        else if ( g_str_has_prefix(*group, "assign ") && ( l > strlen("assign ") ) )
            _wh_config_assign_parse(self, file, *group);
        g_free(*group);
    }
    g_free(groups);
}

static void
_wh_config_output_parse_drm(WhConfig *self, GKeyFile *file, const gchar *section)
{
    const gchar *name = section + strlen("drm ");
    gint ret;
    gchar *alias = NULL;
    gchar *modeline = NULL;
    gint scale = 1;

    ret = _wh_config_get_string(file, section, "alias", &alias);
    if ( ret < 0 )
        return;
    if ( ret == 0 )
    {
        g_hash_table_insert(self->output_aliases, g_strdup(name), alias);
        return;
    }

    if ( _wh_config_get_integer(file, section, "scale", &scale) < 0 )
        return;

    if ( _wh_config_get_string(file, section, "modeline", &modeline) < 0 )
        return;

    WhConfigOutputDrm *output;
    output = g_slice_new(WhConfigOutputDrm);
    output->name = g_strdup(name);
    output->scale = scale;
    output->modeline = modeline;

    g_hash_table_insert(self->outputs, output->name, output);
}

static void
_wh_config_output_parse_virtual(WhConfig *self, GKeyFile *file, const gchar *section)
{
    const gchar *name = section + strlen("virtual ");
    gint ret;
    gint width, height, scale = 1;

    ret = _wh_config_get_integer(file, section, "width", &width);
    if ( ret != 0 )
    {
        if ( ret > 0 )
            g_warning("You must provide a width for virtual output %s", name);
        return;
    }
    ret = _wh_config_get_integer(file, section, "height", &height);
    if ( ret != 0 )
    {
        if ( ret > 0 )
            g_warning("You must provide an height for virtual output %s", name);
        return;
    }
    if ( _wh_config_get_integer(file, section, "scale", &scale) < 0 )
        return;

    if ( ( width < 1 ) || ( height < 1 ) || ( scale < 1 ) )
    {
        g_warning("Wrong size or scale for virtual output %s", name);
        return;
    }

    WhConfigOutputVirtual *output;

    output = g_slice_new(WhConfigOutputVirtual);
    output->name = g_strdup(name);
    output->width = width;
    output->height = height;
    output->scale = scale;

    g_hash_table_insert(self->outputs, output->name, output);
}

static void
_wh_config_output_parse_all(WhConfig *self, GKeyFile *file)
{
    gchar **groups;
    groups = g_key_file_get_groups(file, NULL);
    if ( groups == NULL )
        return;

    const gchar *prefix;
    gsize l;
    void (*parse)(WhConfig *self, GKeyFile *file, const gchar *section);
    switch ( self->backend )
    {
    case WESTON_BACKEND_DRM:
        prefix = "drm ";
        l = strlen("drm ");
        parse = _wh_config_output_parse_drm;
    break;
    case WESTON_BACKEND_WAYLAND:
    case WESTON_BACKEND_X11:
        prefix = "virtual ";
        l = strlen("virtual ");
        parse = _wh_config_output_parse_virtual;
    break;
    default:
        g_return_if_reached();
    }

    gchar **group;
    for ( group = groups ; *group != NULL ; ++group )
    {
        if ( g_str_has_prefix(*group, prefix) && ( strlen(*group) > l ) )
            parse(self, file, *group);
        g_free(*group);
    }
    g_free(groups);
}

static void
_wh_config_load_file(WhConfig *self, const gchar *dir, const gchar *filename, void (*parse)(WhConfig *self, GKeyFile *file))
{
    gchar *path;
    GError *error = NULL;

    path = g_build_filename(dir, filename, NULL);
    if ( ! g_file_test(path, G_FILE_TEST_IS_REGULAR) )
        goto end;

    GKeyFile *file;

    file = g_key_file_new();
    if ( g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, &error) )
        parse(self, file);
    else
        g_warning("Could not load '%s': %s", path, error->message);
    g_key_file_unref(file);

end:
    g_clear_error(&error);
    g_free(path);
}

static gboolean
_wh_config_load_dir(WhConfig *self, const gchar *dirbase)
{
    gboolean found = FALSE;
    gchar *dir;

    dir = g_build_filename(dirbase, PACKAGE_NAME, NULL);
    if ( ! g_file_test(dir, G_FILE_TEST_IS_DIR) )
        goto end;

    _wh_config_load_file(self, dir, PACKAGE_NAME ".conf", _wh_config_global_parse);
    _wh_config_load_file(self, dir, "outputs.conf", _wh_config_output_parse_all);

    found = TRUE;

end:
    g_free(dir);
    return found;
}

static void
_wh_config_load(WhConfig *self)
{
    if ( _wh_config_load_dir(self, g_get_user_config_dir()) )
        return;

    const gchar * const *system_dir;
    for ( system_dir = g_get_system_config_dirs() ; *system_dir != NULL ; ++system_dir )
    {
        if ( _wh_config_load_dir(self, *system_dir) )
            return;
    }
}

WhConfig *
wh_config_new(WhCore *core, gboolean use_pixman)
{
    WhConfig *self;

    self = g_new0(WhConfig, 1);
    self->core = core;

    _wh_config_init(self, use_pixman);
    _wh_config_load(self);

    return self;
}

void
wh_config_free(WhConfig *self)
{
    g_hash_table_unref(self->assigns);

    g_hash_table_unref(self->outputs);
    g_hash_table_unref(self->output_aliases);

    g_free(self);
}

gboolean
wh_config_load_backend(WhConfig *self)
{
    struct weston_compositor *compositor = wh_core_get_compositor(self->core);

    weston_compositor_load_backend(compositor, self->backend, &self->backend_config.drm.base);

    switch ( self->backend )
    {
    case WESTON_BACKEND_DRM:
        self->api.drm = weston_drm_output_get_api(compositor);
        if ( self->api.drm == NULL )
            return FALSE;

        self->output_pending_listener.notify = _wh_config_output_pending_drm;
    break;
    case WESTON_BACKEND_X11:
    case WESTON_BACKEND_WAYLAND:
    {
        self->api.windowed = weston_windowed_output_get_api(compositor);
        if ( self->api.windowed == NULL )
            return FALSE;

        GHashTableIter iter;
        gchar *name;
        g_hash_table_iter_init(&iter, self->outputs);
        while ( g_hash_table_iter_next(&iter, (gpointer *) &name, NULL) )
            self->api.windowed->output_create(compositor, name);

        self->output_pending_listener.notify = _wh_config_output_pending_virtual;
    }
    break;
    default:
        g_return_val_if_reached(FALSE);
    }

    wl_signal_add(&compositor->output_pending_signal, &self->output_pending_listener);
    weston_pending_output_coldplug(compositor);

    return TRUE;
}

struct xkb_rule_names *
wh_config_get_xkb_names(WhConfig *self)
{
    return &self->xkb_names;
}

gboolean
wh_config_get_xwayland(WhConfig *self)
{
    return self->xwayland;
}

const gchar * const *
wh_config_get_common_modules(WhConfig *self)
{
    return (const gchar * const *) self->common_modules;
}

const WhWorkspaceConfig *
wh_config_get_assign(WhConfig *self, const gchar *app_id)
{
    return g_hash_table_lookup(self->assigns, app_id);
}
