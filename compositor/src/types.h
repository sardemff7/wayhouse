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

#ifndef __WAYHOUSE_TYPES_H__
#define __WAYHOUSE_TYPES_H__

typedef enum {
    WH_ORIENTATION_HORIZONTAL = (0 << 0),
    WH_ORIENTATION_VERTICAL   = (1 << 0),
    WH_ORIENTATION_TOGGLE     = (1 << 1),
} WhOrientation;

typedef enum {
    WH_TARGET_PREVIOUS       = (0 << 0),
    WH_TARGET_NEXT           = (1 << 0),
    WH_TARGET_BACK_AND_FORTH = (1 << 1),
} WhTarget;

#define WH_DIRECTION_TREE_MASK (1 << 2)
typedef enum {
    WH_DIRECTION_LEFT   = ( WH_ORIENTATION_HORIZONTAL | (WH_TARGET_PREVIOUS << 1) ),
    WH_DIRECTION_RIGHT  = ( WH_ORIENTATION_HORIZONTAL | (WH_TARGET_NEXT     << 1) ),
    WH_DIRECTION_TOP    = ( WH_ORIENTATION_VERTICAL   | (WH_TARGET_PREVIOUS << 1) ),
    WH_DIRECTION_BOTTOM = ( WH_ORIENTATION_VERTICAL   | (WH_TARGET_NEXT     << 1) ),
    WH_DIRECTION_PARENT = ( WH_ORIENTATION_VERTICAL   | (WH_TARGET_PREVIOUS << 1) | WH_DIRECTION_TREE_MASK ),
    WH_DIRECTION_CHILD  = ( WH_ORIENTATION_VERTICAL   | (WH_TARGET_NEXT     << 1) | WH_DIRECTION_TREE_MASK ),
} WhDirection;

typedef enum {
    WH_CONTAINER_LAYOUT_TABBED = (0 << 0),
    WH_CONTAINER_LAYOUT_SPLIT  = (1 << 0),
} WhContainerLayoutType;

typedef enum {
    WH_STATE_ENABLE,
    WH_STATE_DISABLE,
    WH_STATE_TOGGLE,
} WhStateChange;

#define WH_WORKSPACE_NO_NUMBER ((guint64) -1)

typedef struct {
    guint64 number;
    gchar *name;
} WhWorkspaceConfig;

typedef struct _WhCore WhCore;

typedef struct _WhCommands WhCommands;
typedef struct _WhCommand WhCommand;
typedef struct _WhConfig WhConfig;

typedef struct _WhSeats WhSeats;
typedef struct _WhSeat WhSeat;

typedef struct _WhWorkspaces WhWorkspaces;
typedef struct _WhSurface WhSurface;
typedef struct _WhWorkspace WhWorkspace;
typedef struct _WhContainer WhContainer;

typedef struct _WhOutputs WhOutputs;
typedef struct _WhOutput WhOutput;

typedef struct _WhXwayland WhXwayland;


#endif /* __WAYHOUSE_TYPES_H__ */
