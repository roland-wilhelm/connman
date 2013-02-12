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


static DBusConnection *connection = NULL;
static GHashTable *qmi_hash = NULL;


#define QMI_SERVICE "de.bmw.ltz4.qmi"


struct qmi_data {
	gint index;
	gchar *path;
	gchar *name;
	gchar *imsi;
	gchar *serial;
	gchar *group;
	gchar *address;
	gchar *node;
	gchar *interface;
	guint8 strength;
	connman_bool_t roaming;
	connman_bool_t online;
	struct connman_device *device;
	struct connman_network *network;
};

static int network_probe(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("Ńetwork %p", network);

	return 0;
}

static void network_remove(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("Network %p", network);


}

static int network_connect(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("Network %p", network);



	return 0;
}

static int network_disconnect(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("Network %p", network);


	return 0;
}

static struct connman_network_driver network_driver = {
	.name		= "cellular",
	.type		= CONNMAN_DEVICE_TYPE_CELLULAR,
	.probe		= network_probe,
	.remove		= network_remove,
	.connect	= network_connect,
	.disconnect	= network_disconnect,
};

static int qmi_probe(struct connman_device *device)
{
	struct qmi_data *qmi;

	DBG("Device %p", device);


	return 0;
}

static void qmi_remove(struct connman_device *device)
{
	g_return_if_fail(device);

	struct qmi_data *qmi = connman_device_get_data(device);
	if(!qmi) {

		connman_error("Could not get device data");
		return;
	}

	DBG("Device %p %p", device, qmi);

	connman_device_set_data(device, NULL);

	g_free(qmi);
}

static int qmi_enable(struct connman_device *device)
{
	g_return_if_fail(device);

	struct qmi_data *qmi = connman_device_get_data(device);
	if(!qmi) {

		connman_error("Could not get device data");
		return -ENODEV;
	}

	DBG("Device %p", device);

	qmi->address = connman_device_get_string(device, "Address");
	qmi->name = connman_device_get_string(device, "Name");
	qmi->node = connman_device_get_string(device, "Node");
	qmi->interface = connman_device_get_string(device, "Interface");
	qmi->path = connman_device_get_string(device, "Path");
	qmi->index = connman_device_get_index(device);

	return connman_inet_ifup(qmi->index);
}

static int qmi_disable(struct connman_device *device)
{
	struct qmi_data *qmi = connman_device_get_data(device);

	DBG("Device %p %p", device, qmi);

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

static void destroy_device(struct qmi_data *qmi)
{

	g_return_if_fail(qmi);

	DBG("%s", qmi->path);

	connman_device_set_powered(qmi->device, FALSE);

	if (qmi->network != NULL) {
		connman_device_remove_network(qmi->device, qmi->network);
		connman_network_unref(qmi->network);
		qmi->network = NULL;
	}

	connman_device_unregister(qmi->device);
	connman_device_unref(qmi->device);
	qmi->device = NULL;
}

static void remove_qmi(gpointer data)
{
	struct qmi_data *qmi = data;

	DBG("%s", qmi->path);

	if (qmi->device != NULL)
		destroy_device(qmi);

	g_free(qmi->serial);
	g_free(qmi->name);
	g_free(qmi->imsi);
	g_free(qmi->path);
	g_free(qmi);
}

static void remove_network(struct qmi_data *qmi)
{
	g_return_if_fail(qmi);

	DBG("%s", qmi->path);

	if (qmi->network == NULL)
		return;

	DBG("network %p", qmi->network);

	connman_device_remove_network(qmi->device, qmi->network);
	connman_network_unref(qmi->network);
	qmi->network = NULL;
}

static void add_network(struct qmi_data *qmi)
{
	g_return_if_fail(qmi);

	DBG("%s", qmi->path);

	if (qmi->network != NULL)
		return;

	qmi->network = connman_network_create(	qmi->path,
											CONNMAN_NETWORK_TYPE_CELLULAR);
	if (qmi->network == NULL) {

		connman_error("Network could not be created.");
		return;
	}

	DBG("network %p", qmi->network);

	connman_network_set_data(qmi->network, qmi);
	connman_network_set_string(	qmi->network, "Path", qmi->path);

	if (qmi->name != NULL)
		connman_network_set_name(qmi->network, qmi->name);
	else
		connman_network_set_name(qmi->network, "");

	connman_network_set_strength(qmi->network, qmi->strength);
	connman_network_set_group(qmi->network, qmi->group);
	connman_network_set_bool(qmi->network, "Roaming", qmi->roaming);

	if (connman_device_add_network(qmi->device, qmi->network) < 0) {
		connman_network_unref(qmi->network);
		qmi->network = NULL;
		return;
	}
}

static void create_device(struct qmi_data *qmi)
{
	struct connman_device *device;
	char *ident = NULL;

	g_return_if_fail(qmi);

	DBG("%s", qmi->path);

	if (qmi->imsi != NULL) {

		ident = qmi->imsi;
	}
	else if (qmi->serial != NULL) {

		ident = qmi->serial;
	}
	else {

		qmi->imsi = g_strdup("noIdentGiven");
		ident = qmi->imsi;
	}


	if (connman_dbus_validate_ident(ident) == FALSE)
		ident = connman_dbus_encode_string(ident);
	else
		ident = g_strdup(ident);

	device = connman_device_create("qmi", CONNMAN_DEVICE_TYPE_CELLULAR);
	if (device == NULL) {

		connman_error("Failed to create device");
		g_free(ident);
		return;
	}

	DBG("Device %p", device);
	connman_device_set_ident(device, ident);
	connman_device_set_string(device, "Path", qmi->path);
	connman_device_set_data(device, qmi);

	if (connman_device_register(device) < 0) {

		connman_error("Failed to register cellular device");
		connman_device_unref(device);
		g_free(ident);
		return;
	}

	qmi->device = device;
	connman_device_set_powered(qmi->device, qmi->online);

	add_network(qmi);

}

static void add_qmi(const char *path)
{
	struct qmi_data *qmi;

	g_return_if_fail(path);

	DBG("%s", path);

	/*
	 * FIXME: Benötigte Werte über D-Bus abfragen.
	 */

	qmi = g_hash_table_lookup(qmi_hash, path);
	if (qmi != NULL) {

		return;
	}

	qmi = g_try_new0(struct qmi_data, 1);
	if (qmi == NULL) {

		connman_error("Allocation error, no memory available.");
		return;
	}

	g_hash_table_insert(qmi_hash, g_strdup(path), qmi);

	qmi->strength = 80;
	qmi->path = g_strdup(path);
	qmi->imsi = g_strdup("atWork");
	qmi->group = g_strdup("qmi");
	qmi->name = g_strdup("o2-provider");

	create_device(qmi);
}

static void on_handle_qmi_connect(DBusConnection *conn, void *user_data) {

	/*
	 * Device Descriptor via D-Bus anfordern. Dabei wird automatisch das
	 * angschlossene Gerät initialisiert, eine Netzwerkverbindung hergestellt und der
	 * zugewiesene Device Descriptor (dd) zurückgegeben.
	 *
	 * FIXME: Hash-Tabelle anlegen mit destroy function "remove_qmi".
	 */

	DBG("");

	qmi_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, remove_qmi);
	if (qmi_hash == NULL) {

		connman_error("Hash table could not be created.");
		return;
	}

	add_qmi("/dev/cdc-wdm10");

}

static void on_handle_qmi_disconnect(DBusConnection *conn, void *user_data) {

	/*
	 * FIXME: Disconnect wird aufgerufen, wenn der qmi-dbus-server beendet wird.
	 *
	 */

	DBG("");


	if(qmi_hash) {

		g_hash_table_destroy(qmi_hash);
		qmi_hash = NULL;
	}

}

static int tech_probe(struct connman_technology *technology)
{
	return 0;
}

static void tech_remove(struct connman_technology *technology)
{
}

static struct connman_technology_driver tech_driver = {
	.name		= "cellular",
	.type		= CONNMAN_DEVICE_TYPE_CELLULAR,
	.probe		= tech_probe,
	.remove		= tech_remove,
};

static gint watch = 0;
static gint watch_id = 0;

static int qmi_init(void)
{
	int err = 0;

	DBG("");

//	connection = connman_dbus_get_connection();
//	if (connection == NULL) {
//
//		connman_error("D-Bus connection failed");
//		return -EIO;
//	}
//
//	watch = g_dbus_add_service_watch(	connection,
//										QMI_SERVICE, on_handle_qmi_connect,
//										on_handle_qmi_disconnect, NULL, NULL);

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
//		connman_error("Adding service or signal watch");
//		g_dbus_remove_watch(connection, watch);
//		dbus_connection_unref(connection);
//
//		return -EIO;
//	}

	err = connman_technology_driver_register(&tech_driver);
	if (err < 0) {

		connman_error("Register technology driver");

		return err;
	}

	err = connman_network_driver_register(&network_driver);
	if (err < 0) {

		connman_error("Register network driver");
		connman_technology_driver_unregister(&tech_driver);

		return err;
	}

	err = connman_device_driver_register(&qmi_driver);
	if (err < 0) {

		connman_error("Register device driver");
		connman_technology_driver_unregister(&tech_driver);
		connman_network_driver_unregister(&network_driver);

		return err;
	}


	return 0;
}

static void qmi_exit(void)
{

	DBG("");

	connman_device_driver_unregister(&qmi_driver);
	connman_network_driver_unregister(&network_driver);
	connman_technology_driver_register(&tech_driver);
	g_dbus_remove_watch(connection, watch);
	dbus_connection_unref(connection);

}

CONNMAN_PLUGIN_DEFINE(qmi, "QMI plugin", CONNMAN_VERSION,
		CONNMAN_PLUGIN_PRIORITY_DEFAULT, qmi_init, qmi_exit)
