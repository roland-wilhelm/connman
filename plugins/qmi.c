/*
 * qmi.c
 *
 *  Created on: 07.02.2013
 *      Author: roland
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>

#include <gdbus.h>
#include <string.h>
#include <stdint.h>
#include <glib.h>
#include <glib/gprintf.h>

#define CONNMAN_API_SUBJECT_TO_CHANGE
#include <connman/plugin.h>
#include <connman/device.h>
#include <connman/network.h>
#include <connman/inet.h>
#include <connman/dbus.h>
#include <connman/log.h>
#include <connman/technology.h>


static DBusConnection *connection;


struct qmi_data {
	int index;
	unsigned flags;
	unsigned int watch;
	char *path;
	char *name;
	uint8_t strength;
	connman_bool_t roaming;
	struct connman_device *device;
	struct connman_network *network;
};

static int network_probe(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("%s network %p", "test string", network);

	return 0;
}

static void network_remove(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("%s network %p", "test string", network);


}

static int network_connect(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("%s network %p","test string", network);



	return 0;
}

static int network_disconnect(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("%s network %p", "test string", network);


	return 0;
}

static struct connman_network_driver network_driver = {
	.name		= "cellular",
	.type		= CONNMAN_NETWORK_TYPE_CELLULAR,
	.probe		= network_probe,
	.remove		= network_remove,
	.connect	= network_connect,
	.disconnect	= network_disconnect,
};

static int qmi_probe(struct connman_device *device)
{
	struct qmi_data *qmi;

	DBG("device %p", device);

	qmi = g_try_new0(struct qmi_data, 1);
	if (qmi == NULL)
		return -ENOMEM;

	connman_device_set_data(device, qmi);

	qmi->index = connman_device_get_index(device);
	qmi->flags = 0;
	qmi->watch = 0;
	qmi->path = g_strdup("test path");
	qmi->name = g_strdup("test name");
	qmi->strength = 80;
	qmi->roaming = FALSE;

	return 0;
}

static void qmi_remove(struct connman_device *device)
{
	struct qmi_data *qmi = connman_device_get_data(device);

	DBG("device %p", device);

	connman_device_set_data(device, NULL);



	g_free(qmi);
}

static int qmi_enable(struct connman_device *device)
{
	struct qmi_data *qmi = connman_device_get_data(device);

	DBG("device %p", device);

	return connman_inet_ifup(qmi->index);
}

static int qmi_disable(struct connman_device *device)
{
	struct qmi_data *qmi = connman_device_get_data(device);

	DBG("device %p", device);

	return connman_inet_ifdown(qmi->index);
}


static struct connman_device_driver qmi_driver = {
	.name		= "cellular",
	.type		= CONNMAN_DEVICE_TYPE_CELLULAR,
	.probe		= qmi_probe,
	.remove		= qmi_remove,
	.enable		= qmi_enable,
	.disable	= qmi_disable,
};


static void add_network(struct qmi_data *qmi)
{
	const char *group;

	DBG("%s", qmi->path);

	if (qmi->network != NULL)
		return;

	qmi->network = connman_network_create(	qmi->path,
											CONNMAN_NETWORK_TYPE_CELLULAR);
	if (qmi->network == NULL)
		return;

	DBG("network %p", qmi->network);

	connman_network_set_data(qmi->network, qmi);

	connman_network_set_string(	qmi->network, "Path",
								qmi->path);

	if (qmi->name != NULL)
		connman_network_set_name(qmi->network, qmi->name);
	else
		connman_network_set_name(qmi->network, "");

	connman_network_set_strength(qmi->network, qmi->strength);

	group = get_ident(qmi->path);
	connman_network_set_group(qmi->network, group);

	connman_network_set_bool(	qmi->network, "Roaming",
								qmi->roaming);

	if (connman_device_add_network(qmi->device, qmi->network) < 0) {
		connman_network_unref(qmi->network);
		qmi->network = NULL;
		return;
	}
}

static void remove_network(struct qmi_data *qmi)
{
	DBG("%s", qmi->path);

	if (qmi->network == NULL)
		return;

	DBG("network %p", qmi->network);

	connman_device_remove_network(qmi->device, qmi->network);
	connman_network_unref(qmi->network);
	qmi->network = NULL;
}


static void on_handle_qmi_connect(DBusConnection *conn, void *user_data) {

	/*
	 * Device Descriptor via D-Bus anfordern. Dabei wird automatisch das
	 * angschlossene Gerät initialisiert, eine Netzwerkverbindung hergestellt und der
	 * zugewiesene Device Descriptor (dd) zurückgegeben.
	 *
	 */

	DBG("");


}

static void on_handle_qmi_disconnect(DBusConnection *conn, void *user_data) {

	/*
	 * eventuelle Aufräumarbeiten
	 */

	DBG("");

}

static gint watch = 0;
static gint watch_id = 0;

static int qmi_init(void)
{
	int err = 0;

	DBG("");

	connection = connman_dbus_get_connection();
	if (connection == NULL)
		return -EIO;

	/*watch = g_dbus_add_service_watch(	connection,
										QMI_SERVICE, on_handle_qmi_connect,
										on_handle_qmi_disconnect, NULL, NULL);*/

//	watch_id = g_dbus_add_signal_watch(	connection, QMI_SERVICE,
//										NULL, QMI_MANAGER_INTERFACE,
//										member,
//										function,
//										NULL, NULL);



//	if (watch == 0) {
//
//		err = -EIO;
//	}
//
//	if (err == -EIO) {
//
//		g_dbus_remove_watch(connection, watch);
//		dbus_connection_unref(connection);
//
//		return -EIO;
//	}

	err = connman_device_driver_register(&qmi_driver);
	if (err < 0) {

		return err;
	}

	err = connman_network_driver_register(&network_driver);
	if (err < 0) {

		connman_device_driver_unregister(&qmi_driver);
		return err;
	}

	return 0;
}

static void qmi_exit(void)
{

	DBG("");

	connman_device_driver_unregister(&qmi_driver);
	connman_network_driver_unregister(&network_driver);
	g_dbus_remove_watch(connection, watch);
	dbus_connection_unref(connection);

}

CONNMAN_PLUGIN_DEFINE(qmi, "QMI plugin", CONNMAN_VERSION,
		CONNMAN_PLUGIN_PRIORITY_DEFAULT, qmi_init, qmi_exit)
