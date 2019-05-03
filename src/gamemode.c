/*
 * Copyright © 2019 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Christian J. Kellner <christian@kellner.me>
 */

#include "config.h"

#include "request.h"
#include "permissions.h"

#include "xdp-dbus.h"
#include "xdp-impl-dbus.h"
#include "xdp-utils.h"

#include <gio/gunixfdlist.h>

#define _GNU_SOURCE 1
#include <errno.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h> /* unlinkat, fork */

/* well known names*/
#define GAMEMODE_DBUS_NAME "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_IFACE "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_PATH "/com/feralinteractive/GameMode"

/* */
typedef struct _GameMode GameMode;
typedef struct _GameModeClass GameModeClass;

static gboolean handle_query_status (XdpGameMode *object,
				     GDBusMethodInvocation *invocation,
				     GVariant *arg_pid);

static gboolean handle_register_game (XdpGameMode *object,
				      GDBusMethodInvocation *invocation,
				      GVariant *arg_pid);

static  gboolean handle_unregister_game (XdpGameMode *object,
					 GDBusMethodInvocation *invocation,
					 GVariant *arg_pid);

/* globals  */
static GameMode *gamemode;

/* gobject  */

struct _GameMode
{
  XdpGameModeSkeleton parent_instance;

  /*  */
  GDBusProxy *client;
};

struct _GameModeClass
{
  XdpGameModeSkeletonClass parent_class;
};

GType gamemode_get_type (void) G_GNUC_CONST;
static void gamemode_iface_init (XdpGameModeIface *iface);

G_DEFINE_TYPE_WITH_CODE (GameMode, gamemode, XDP_TYPE_GAME_MODE_SKELETON,
                         G_IMPLEMENT_INTERFACE (XDP_TYPE_GAME_MODE, gamemode_iface_init));

static void
gamemode_iface_init (XdpGameModeIface *iface)
{
  iface->handle_query_status = handle_query_status;
  iface->handle_register_game = handle_register_game;
  iface->handle_unregister_game = handle_unregister_game;
}

static void
gamemode_init (GameMode *gamemode)
{

}

static void
gamemode_class_init (GameModeClass *klass)
{
}

/* internal */


/* dbus  */
static void
got_gamemode_reply (GObject      *source,
		    GAsyncResult *res,
		    gpointer      user_data)
{
  g_autoptr(GDBusMethodInvocation) inv = user_data;
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) val = NULL;
  int r = -1;

  val = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &err);

  if (val != NULL)
    g_variant_get (val, "(i)", &r);
  else
    g_debug ("call to GameMode failed: %s", err->message);

  g_dbus_method_invocation_return_value (inv, g_variant_new ("(i)", r));
}

static gboolean
make_call_simple (XdpGameMode *object,
		  GDBusMethodInvocation *invocation,
		  GVariant    *params)
{
 

  g_dbus_proxy_call (G_DBUS_PROXY (gamemode->client),
                     msg,
                     g_variant_new ("(i)", (guint32) u.pid),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL, /* cancel */
                     got_gamemode_reply,
                     g_object_ref (invocation));

  return TRUE;
}

static gboolean
handle_query_status (XdpGameMode *object,
		     GDBusMethodInvocation *invocation,
		     GVariant *arg_pid)
{
  
}

static gboolean
handle_register_game (XdpGameMode *object,
		      GDBusMethodInvocation *invocation,
		      GVariant *arg_pid)
{

}

static  gboolean
handle_unregister_game (XdpGameMode *object,
			GDBusMethodInvocation *invocation,
			GVariant *arg_pid)
{

}

/* public API */
GDBusInterfaceSkeleton *
gamemode_create (GDBusConnection *connection)
{
  g_autoptr(GError) err = NULL;
  GDBusProxy *client;
  GDBusProxyFlags flags;

  flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION;
  client = g_dbus_proxy_new_sync (connection,
				  flags,
				  NULL,
				  GAMEMODE_DBUS_NAME,
				  GAMEMODE_DBUS_PATH,
				  GAMEMODE_DBUS_IFACE,
				  NULL,
				  &err);

  if (client == NULL)
    {
      g_warning ("Failed to create GameMode proxy: %s", err->message);
      return NULL;
    }

  gamemode = g_object_new (gamemode_get_type (), NULL);
  gamemode->client = client;

  return G_DBUS_INTERFACE_SKELETON (gamemode);;
}
