/*
 * qmi.c
 *
 *  Created on: 07.02.2013
 *      Author: roland
 */

#include <connman/plugin.h>


static DBusConnection *connection;


struct qmi_data {
	int index;
	unsigned flags;
	unsigned int watch;
	struct connman_network *network;
};


static int qmi_probe(struct connman_device *device)
{
	struct ethernet_data *ethernet;

	DBG("device %p", device);

	ethernet = g_try_new0(struct ethernet_data, 1);
	if (ethernet == NULL)
		return -ENOMEM;

	connman_device_set_data(device, ethernet);

	ethernet->index = connman_device_get_index(device);
	ethernet->flags = 0;
	ethernet->watch = 0;
//	ethernet->watch = connman_rtnl_add_newlink_watch(ethernet->index, ethernet_newlink, device);

	return 0;
}

static void qmi_remove(struct connman_device *device)
{
	struct ethernet_data *ethernet = connman_device_get_data(device);

	DBG("device %p", device);

	connman_device_set_data(device, NULL);

//	connman_rtnl_remove_watch(ethernet->watch);

	remove_network(device, ethernet);

	g_free(ethernet);
}

static int qmi_enable(struct connman_device *device)
{
	struct ethernet_data *ethernet = connman_device_get_data(device);

	DBG("device %p", device);

	return connman_inet_ifup(ethernet->index);
}

static int qmi_disable(struct connman_device *device)
{
	struct ethernet_data *ethernet = connman_device_get_data(device);

	DBG("device %p", device);

	return connman_inet_ifdown(ethernet->index);
}


static struct connman_device_driver qmi_driver = {
	.name		= "cellular",
	.type		= CONNMAN_DEVICE_TYPE_CELLULAR,
	.probe		= qmi_probe,
	.remove		= qmi_remove,
	.enable		= qmi_enable,
	.disable	= qmi_disable,
};

static void qmi_connect(DBusConnection *conn, void *user_data) {

	/*
	 * Device Descriptor via D-Bus anfordern. Dabei wird automatisch das
	 * angschlossene Gerät initialisiert, eine Netzwerkverbindung hergestellt und der
	 * zugewiesene Device Descriptor (dd) zurückgegeben.
	 *
	 */

	DBG("");
}

static void qmi_disconnect(DBusConnection *conn, void *user_data) {

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

	watch = g_dbus_add_service_watch(	connection,
										QMI_SERVICE, qmi_connect,
										qmi_disconnect, NULL, NULL);

//	watch_id = g_dbus_add_signal_watch(	connection, QMI_SERVICE,
//										NULL, QMI_MANAGER_INTERFACE,
//										member,
//										function,
//										NULL, NULL);



	if (watch == 0) {

		return -EIO;
	}


	err = connman_device_driver_register(&qmi_driver);
	if (err < 0) {
		return err;
	}

	return 0;
}

static void qmi_exit(void)
{

	DBG("");

	connman_device_driver_unregister(&qmi_driver);


//	g_dbus_remove_watch(connection, );


	dbus_connection_unref(connection);

}

CONNMAN_PLUGIN_DEFINE(qmi, "QMI plugin", CONNMAN_VERSION,
						qmi_init, qmi_exit)
