/*
* Copyright (C) 2015 Red Hat, Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "config.h"
#include "ostree.h"

#include <libglnx.h>

#include "rpmostreed-sysroot.h"
#include "rpmostreed-daemon.h"
#include "rpmostreed-deployment-utils.h"
#include "rpmostree-package-variants.h"
#include "rpmostreed-errors.h"
#include "rpmostree-origin.h"
#include "rpmostreed-os-experimental.h"
#include "rpmostreed-utils.h"
#include "rpmostree-util.h"
#include "rpmostreed-transaction.h"
#include "rpmostreed-transaction-monitor.h"
#include "rpmostreed-transaction-types.h"

typedef struct _RpmostreedOSExperimentalClass RpmostreedOSExperimentalClass;

struct _RpmostreedOSExperimental
{
  RPMOSTreeOSSkeleton parent_instance;
  RpmostreedTransactionMonitor *transaction_monitor;
};

struct _RpmostreedOSExperimentalClass
{
  RPMOSTreeOSSkeletonClass parent_class;
};

static void rpmostreed_osexperimental_iface_init (RPMOSTreeOSExperimentalIface *iface);


G_DEFINE_TYPE_WITH_CODE (RpmostreedOSExperimental,
                         rpmostreed_osexperimental,
                         RPMOSTREE_TYPE_OSEXPERIMENTAL_SKELETON,
                         G_IMPLEMENT_INTERFACE (RPMOSTREE_TYPE_OSEXPERIMENTAL,
                                                rpmostreed_osexperimental_iface_init)
                         );

static RpmostreedTransaction *
merge_compatible_txn (RpmostreedOSExperimental *self,
                      GDBusMethodInvocation *invocation)
{
  glnx_unref_object RpmostreedTransaction *transaction = NULL;

  /* If a compatible transaction is in progress, share its bus address. */
  transaction = rpmostreed_transaction_monitor_ref_active_transaction (self->transaction_monitor);
  if (transaction != NULL)
    {
      if (rpmostreed_transaction_is_compatible (transaction, invocation))
        return g_steal_pointer (&transaction);
    }

  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
os_dispose (GObject *object)
{
  RpmostreedOSExperimental *self = RPMOSTREED_OSEXPERIMENTAL (object);
  const gchar *object_path;

  object_path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON(self));
  if (object_path != NULL)
    {
      rpmostreed_daemon_unpublish (rpmostreed_daemon_get (),
                                   object_path, object);
    }

  g_clear_object (&self->transaction_monitor);

  G_OBJECT_CLASS (rpmostreed_osexperimental_parent_class)->dispose (object);
}

static void
os_constructed (GObject *object)
{
  G_GNUC_UNUSED RpmostreedOSExperimental *self = RPMOSTREED_OSEXPERIMENTAL (object);

  /* TODO Integrate with PolicyKit via the "g-authorize-method" signal. */

  G_OBJECT_CLASS (rpmostreed_osexperimental_parent_class)->constructed (object);
}

static void
rpmostreed_osexperimental_class_init (RpmostreedOSExperimentalClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = os_dispose;
  gobject_class->constructed  = os_constructed;

}

static void
rpmostreed_osexperimental_init (RpmostreedOSExperimental *self)
{
}

static gboolean
osexperimental_handle_moo (RPMOSTreeOSExperimental *interface,
                           GDBusMethodInvocation   *invocation,
                           gboolean                 is_utf8)
{
  static const char ascii_cow[] = "\n" \
"                 (__)\n" \
"                 (oo)\n" \
"           /------\\/\n" \
"          / |    ||\n" \
"         *  /\\---/\\\n" \
"            ~~   ~~\n";
  const char *result = is_utf8 ? "🐄\n" : ascii_cow;
  rpmostree_osexperimental_complete_moo (interface, invocation, result);
  return TRUE;
}
static RpmOstreeTransactionLiveFsFlags
livefs_flags_from_options (GVariant *options)
{
  RpmOstreeTransactionLiveFsFlags ret = 0;
  GVariantDict options_dict;
  gboolean opt = FALSE;

  g_variant_dict_init (&options_dict, options);
  if (g_variant_dict_lookup (&options_dict, "dry-run", "b", &opt) && opt)
    ret |= RPMOSTREE_TRANSACTION_LIVEFS_FLAG_DRY_RUN;
  if (g_variant_dict_lookup (&options_dict, "replace", "b", &opt) && opt)
    ret |= RPMOSTREE_TRANSACTION_LIVEFS_FLAG_REPLACE;

  g_variant_dict_clear (&options_dict);

  return ret;
}

static gboolean
osexperimental_handle_live_fs (RPMOSTreeOSExperimental *interface,
                               GDBusMethodInvocation *invocation,
                               GVariant *arg_options)
{
  RpmostreedOSExperimental *self = RPMOSTREED_OSEXPERIMENTAL (interface);
  glnx_unref_object RpmostreedTransaction *transaction = NULL;
  glnx_unref_object OstreeSysroot *ot_sysroot = NULL;
  g_autoptr(GCancellable) cancellable = g_cancellable_new ();
  GError *local_error = NULL;

  transaction = merge_compatible_txn (self, invocation);
  if (transaction)
    goto out;

  if (!rpmostreed_sysroot_load_state (rpmostreed_sysroot_get (),
                                      cancellable,
                                      &ot_sysroot,
                                      NULL,
                                      &local_error))
    goto out;

  transaction = rpmostreed_transaction_new_livefs (invocation,
                                                   ot_sysroot,
                                                   livefs_flags_from_options (arg_options),
                                                   cancellable,
                                                   &local_error);
  if (transaction == NULL)
    goto out;

  rpmostreed_transaction_monitor_add (self->transaction_monitor, transaction);

out:
  if (local_error != NULL)
    {
      g_dbus_method_invocation_take_error (invocation, local_error);
    }
  else
    {
      const char *client_address;
      client_address = rpmostreed_transaction_get_client_address (transaction);
      rpmostree_osexperimental_complete_live_fs (interface, invocation, client_address);
    }

  return TRUE;
}

static void
rpmostreed_osexperimental_iface_init (RPMOSTreeOSExperimentalIface *iface)
{
  iface->handle_moo = osexperimental_handle_moo;
  iface->handle_live_fs = osexperimental_handle_live_fs;
}

/* ---------------------------------------------------------------------------------------------------- */

RPMOSTreeOSExperimental *
rpmostreed_osexperimental_new (OstreeSysroot *sysroot,
                               OstreeRepo *repo,
                               const char *name,
                               RpmostreedTransactionMonitor *monitor)
{
  RpmostreedOSExperimental *obj = NULL;
  g_autofree char *path = NULL;

  g_return_val_if_fail (OSTREE_IS_SYSROOT (sysroot), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (RPMOSTREED_IS_TRANSACTION_MONITOR (monitor), NULL);

  path = rpmostreed_generate_object_path (BASE_DBUS_PATH, name, NULL);

  obj = g_object_new (RPMOSTREED_TYPE_OSEXPERIMENTAL, NULL);

  /* FIXME Make this a construct-only property? */
  obj->transaction_monitor = g_object_ref (monitor);

  rpmostreed_daemon_publish (rpmostreed_daemon_get (), path, FALSE, obj);

  return RPMOSTREE_OSEXPERIMENTAL (obj);
}
