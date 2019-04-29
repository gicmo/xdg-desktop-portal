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

static gboolean handle_action (XdpGameMode *object,
			       GDBusMethodInvocation *invocation,
			       GUnixFDList *fds,
			       GVariant    *params);

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
  iface->handle_action = handle_action;
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

static ssize_t
recv_fd (int sock, char *data, size_t len, struct ucred *ucred, int *fd_out)
{
  const size_t ucred_size = sizeof (struct ucred);
  struct iovec iov = {
    .iov_base = data,
    .iov_len = len,
  };
  union {
    struct cmsghdr cmh;
    char   data[CMSG_SPACE (sizeof (int)) +
		CMSG_SPACE (ucred_size)];
  } ctrl;
  struct msghdr hdr = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = &ctrl,
    .msg_controllen = sizeof (ctrl),
  };
  int fd = -1;
  int r;

  r = recvmsg (sock, &hdr, MSG_CMSG_CLOEXEC | MSG_TRUNC);

  if (r < 0)
    return -errno;

  for (struct cmsghdr *c = CMSG_FIRSTHDR (&hdr);
       c != NULL;
       c = CMSG_NXTHDR (&hdr, c))
    {
      if (c->cmsg_level != SOL_SOCKET)
        continue;

      if (c->cmsg_type == SCM_CREDENTIALS &&
          c->cmsg_len == CMSG_LEN (ucred_size))
	{
	  if (ucred != NULL)
	    memcpy (ucred, CMSG_DATA (c), ucred_size);
	}
      else if (c->cmsg_type == SCM_RIGHTS)
	{
	  size_t l = (c->cmsg_len - CMSG_LEN(0)) / sizeof (int);

	  if (l == 0)
	    continue;
	  else if (l > 1)
	    return -EIO;

	  memcpy (&fd, CMSG_DATA (c), sizeof (int));
	}
    }

  if (fd)
    {
      if (fd_out)
	*fd_out = fd;
      else
	(void) close (fd);
    }

  return r;
}

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
handle_action (XdpGameMode *object,
	       GDBusMethodInvocation *invocation,
	       GUnixFDList *fd_list,
	       GVariant    *params)
{
  g_autofree int *fds = NULL;
  struct ucred u = {0, };
  char msg[1024] = {0, };
  socklen_t n;
  int data = -1;
  int index;
  int n_fds;
  int r;

  if (g_unix_fd_list_get_length (fd_list) != 1)
    {
      g_dbus_method_invocation_return_error (invocation,
					     G_DBUS_ERROR,
					     G_DBUS_ERROR_INVALID_ARGS,
					     "Exactly one file descriptor should be passed");
      return TRUE;
    }

  g_variant_get (params, "h", &index);
  if (index != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
					     G_DBUS_ERROR,
					     G_DBUS_ERROR_INVALID_ARGS,
					     "Bad file descriptor index %d", index);
      return TRUE;
    }

  fds = g_unix_fd_list_steal_fds (fd_list, &n_fds);
  r = recv_fd (fds[0], msg, sizeof (msg) - 1, NULL, &data);

  (void) close (fds[0]);

  if (r < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
					     G_DBUS_ERROR,
					     G_DBUS_ERROR_INVALID_ARGS,
					     "Could not receive fd: %s",
					     g_strerror (-r));
      return TRUE;
    }

  n = sizeof (u);
  r = getsockopt (data, SOL_SOCKET, SO_PEERCRED, &u, &n);

  (void) close (data);

  if (r < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
					     G_DBUS_ERROR,
					     G_DBUS_ERROR_INVALID_ARGS,
					     "Could not remote credentials: %s",
					     g_strerror (errno));
      return TRUE;
    }
  else if (u.pid == 0)
    {

    }


  g_debug ("gamemode action: %s: %d, %d, %d\n", msg,
	   (int) u.uid, (int) u.gid, (int) u.pid);

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
