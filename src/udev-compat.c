/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2007-2009  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include "connman.h"

static GSList *device_list = NULL;

static struct connman_device *find_device(int index)
{
	GSList *list;

	for (list = device_list; list; list = list->next) {
		struct connman_device *device = list->data;

		if (connman_device_get_index(device) == index)
			return device;
	}

	return NULL;
}

static void detect_newlink(unsigned short type, int index,
					unsigned flags, unsigned change)
{
	struct connman_device *device;

	DBG("type %d index %d", type, index);

	device = find_device(index);
	if (device != NULL)
		return;

	device = connman_inet_create_device(index);
	if (device == NULL)
		return;

	if (connman_device_register(device) < 0) {
		connman_device_unref(device);
		return;
	}

	device_list = g_slist_append(device_list, device);
}

static void detect_dellink(unsigned short type, int index,
					unsigned flags, unsigned change)
{
	struct connman_device *device;

	DBG("type %d index %d", type, index);

	device = find_device(index);
	if (device == NULL)
		return;

	device_list = g_slist_remove(device_list, device);

	connman_device_unregister(device);
	connman_device_unref(device);
}

static struct connman_rtnl detect_rtnl = {
	.name		= "detect",
	.priority	= CONNMAN_RTNL_PRIORITY_LOW,
	.newlink	= detect_newlink,
	.dellink	= detect_dellink,
};

char *__connman_udev_get_devtype(const char *ifname)
{
	return NULL;
}

char *__connman_udev_get_mbm_devnode(const char *ifname)
{
	return NULL;
}

int __connman_udev_init(void)
{
	int err;

	DBG("");

	err = connman_rtnl_register(&detect_rtnl);
	if (err < 0)
		return err;

	connman_rtnl_send_getlink();

	return 0;
}

void __connman_udev_cleanup(void)
{
	GSList *list;

	DBG("");

	connman_rtnl_unregister(&detect_rtnl);

	for (list = device_list; list; list = list->next) {
		struct connman_device *device = list->data;

		connman_device_unregister(device);
		connman_device_unref(device);
	}

	g_slist_free(device_list);
	device_list = NULL;
}
