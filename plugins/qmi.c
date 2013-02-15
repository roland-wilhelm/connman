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
#include <errno.h>

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

const static gchar *QMI_DEVICE_DIR_1 = "/sys/class/net/";
const static gchar *QMI_DEVICE_DIR_2 = "/device/usb/";
const static gchar *QMI_DEVICE		 = "cdc-wdm";


struct qmi_data {
	gint index;
	gchar *path;
	gchar *name;
	gchar *imsi;
	gchar *serial;
	gchar *group;
	gchar *devname;
	guint8 strength;
	connman_bool_t device_enabled;
	connman_bool_t service_connected;
	connman_bool_t modem_roaming;
	connman_bool_t modem_online;
	struct connman_device *device;
	struct connman_network *network;
};

static void delete_network(struct qmi_data *qmi)
{
	g_return_if_fail(qmi);

	DBG("%s", qmi->path);

	if(qmi->network == NULL)
		return;

	DBG("network %p", qmi->network);

	connman_device_remove_network(qmi->device, qmi->network);
	qmi->network = NULL;
}

static void add_network(struct qmi_data *qmi)
{
	struct connman_network *network = NULL;
	int index;
	gchar *ident = NULL;

	g_return_if_fail(qmi);

	DBG("data %p", qmi);

	if(qmi->network != NULL) {

		DBG("network %p already exists.", qmi->network);
		return;
	}

	network = connman_network_create(qmi->path,
					CONNMAN_NETWORK_TYPE_CELLULAR);
	if(network == NULL) {

		connman_error("Network could not be created.");
		return;
	}

	DBG("network %p", qmi->network);

	index = connman_device_get_index(qmi->device);
	connman_network_set_index(network, index);
	connman_network_set_name(network, qmi->name);
	connman_network_set_data(network, qmi);
	connman_network_set_strength(network, qmi->strength);
	connman_network_set_group(network, qmi->group);


	/*
	 * TODO: connman_network_set_strength
	 * connman_network_set_frequency
	 * connman_network_set_bool(qmi->network, "Roaming", qmi->modem_roaming);
	 * connman_network_update(connman_network);
	 */

	if (connman_device_add_network(qmi->device, network) < 0) {

		connman_error("Network not added to the device.");
		connman_network_unref(network);
		network = NULL;
		return;
	}

	qmi->network = network;

}

static int network_probe(struct connman_network *network)
{
	struct qmi_data *qmi = NULL;

	DBG("network %p", network);

	g_return_val_if_fail(network, -ENODEV);

	qmi = connman_network_get_data(network);
	if(!qmi) {

		connman_error("No device data available.");
		return -ENODEV;
	}

	DBG("network %p data %p", network, qmi);

	/*
	 * FIXME: Netzwerk erst hinzufügen, wenn Verbindung mit QMI Device
	 * über D-Bus hergestellt wurde.
	 */
	if((qmi->modem_online == TRUE) && (qmi->service_connected == TRUE)) {

		add_network(qmi);
		return 0;
	}
	else {

		connman_error("Modem is not online or the QMI D-Bus server is not activated.");
		return -ENODEV;
	}


}

static void network_remove(struct connman_network *network)
{
	struct qmi_data *qmi = NULL;

	DBG("network %p", network);

	g_return_if_fail(network);

	qmi = connman_network_get_data(network);
	if(!qmi) {

		connman_error("No  device data available.");
		return;
	}

	DBG("network %p data %p", network, qmi);

	delete_network(qmi);
}

static int network_connect(struct connman_network *network)
{
	struct qmi_data *qmi = NULL;
	int err = 0;

	DBG("Network %p", network);

	qmi = connman_network_get_data(network);
	if(!qmi) {

		connman_error("Could not get device data.");
		return -ENODEV;
	}

	DBG("Network %p %p", network, qmi);

	/*
	 * TODO: Verbindung qmi-dbus-server wird automatish beim aktivieren des
	 * 		 Serves hergestellt. Sobald die Verbindungs hergestellt ist, kann
	 * 		 das Netzwerk hinzugefügt werden.
	 * 		 Mögliche Parameter: SSID = Provider evtl. Provider+Technology
	 * 		 					 Strength = RSSI oder SNR
	 * 		 					 Frequenz = Frequenz für LTE
	 */

	return 0;

}

static int network_disconnect(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("Network %p %p", network, qmi);

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

static gchar* get_device_path_from_name(const gchar *devname) {

	gchar *device_path = NULL;
	GString *dev;
	GDir *dir = NULL;
	gboolean ret;
	GError *error;

	DBG("device name %s", devname);

	g_return_val_if_fail(devname, NULL);

	/*
	 * TODO: Pfad zu Gerätenamen devname
	 * 		 1. In sys/class/net/ muss devname als ordner vorhanden sein
	 * 		 2. In sys/class/net/devname/device/usb/cdc-wdm0
	 */

	dev = g_string_new(QMI_DEVICE_DIR_1);
	dev = g_string_append(dev, devname);
	DBG("path QMI device %s", dev->str);
	ret = g_file_test(dev->str, G_FILE_TEST_EXISTS |  G_FILE_TEST_IS_DIR);
	if(ret == FALSE) {

		connman_error("Path %s not exists", dev->str);
		g_string_free(dev, TRUE);
		return NULL;
	}

	dev = g_string_append(dev, QMI_DEVICE_DIR_2);
	DBG("path QMI device %s", dev->str);
	dir = g_dir_open(dev->str, 0, &error);
	if(dir == NULL) {

		connman_error("open path %s not possible, error %s", dev->str, error->message);
		g_string_free(dev, TRUE);
		g_error_free(error);
		error = NULL;
		return NULL;

	}
	device_path = g_dir_read_name(dir);

	g_string_free(dev, TRUE);
	dev = g_string_new(device_path);
	dev = g_string_prepend(dev, "/dev/");

	DBG("device name %s path %s", devname, dev->str);

	g_dir_close(dir);

	return g_string_free(dev, FALSE);
}


static int qmi_probe(struct connman_device *device)
{
	struct qmi_data *qmi;

	DBG("device %p", device);

	g_return_val_if_fail(device, -ENODEV);

	qmi = g_try_new0(struct qmi_data, 1);
	if(qmi == NULL) {

		connman_error("Allocation error, no memory available.");
		return -ENOMEM;
	}

	DBG("device %p data %p", device, qmi);

	connman_device_set_data(device, qmi);
	qmi->device = connman_device_ref(device);
	qmi->network = NULL;
	qmi->device_enabled = FALSE;
	qmi->service_connected = TRUE;
	qmi->modem_online = TRUE;
	qmi->modem_roaming = FALSE;

	/*
	 * TODO: Werte werden vom D-Bus ermittelt und sollen später auf
	 * 0 gesetzt werde.
	 */
	qmi->imsi = g_strdup("1234567890");
	qmi->serial = NULL;
	qmi->group = g_strdup("qmi");

	qmi->strength = 50;
	/* Name of the provider e.g "o2" */
	qmi->name = g_strdup("o2");
	/* Name of the specific QMI-Device e.g. wwan0 */
	qmi->devname = g_strdup(connman_device_get_string(device, "Interface"));
	/* Index of the specific QMI-Device */
	qmi->index = connman_device_get_index(device);
	/*
	 * TODO: Pfad muss noch automatisch durch devname bestimmt werden.
	 */
	qmi->path = get_device_path_from_name(qmi->devname);
	DBG("device name %s path %s", qmi->devname, qmi->path);
//	if(qmi->path == NULL) {
//
//		connman_error("No device path available");
//		return -ENODEV;
//	}
	/*
	 * FIXME: Falls ein unsinniger Pfad übergebn wird, kann es zu Endlosschleifen
	 * kommen.
	 */
	connman_device_set_string(device, "Path", qmi->path);

	return 0;
}

static void qmi_remove(struct connman_device *device)
{
	struct qmi_data *qmi = NULL;

	DBG("device %p", device);

	g_return_if_fail(device);

	qmi = connman_device_get_data(device);
	if(!qmi) {

		connman_error("Could not get device data");
		return;
	}

	DBG("device %p data %p", device, qmi);

	connman_device_set_data(device, NULL);
	connman_device_unref(qmi->device);

	g_free(qmi->serial);
	g_free(qmi->name);
	g_free(qmi->imsi);
	g_free(qmi->path);
	g_free(qmi->devname);
	g_free(qmi->group);
	g_free(qmi);

}

static int qmi_enable(struct connman_device *device)
{
	int err = 0;
	struct qmi_data *qmi = NULL;

	DBG("device %p", device);

	g_return_val_if_fail(device, -ENODEV);

	qmi = connman_device_get_data(device);
	if(!qmi) {

		connman_error("No device data available");
		return -ENODEV;
	}

	DBG("device %p data %p", device, qmi);

	err = connman_inet_ifup(qmi->index);
	if(err < 0) {

		connman_error("QMI device could not getting up with ifup");
		return err;
	}

	qmi->device_enabled = TRUE;
	add_network(qmi);

	return 0;
}

static int qmi_disable(struct connman_device *device)
{
	int err = 0;
	struct qmi_data *qmi = NULL;

	DBG("device %p", device);

	g_return_val_if_fail(device, -ENODEV);

	qmi = connman_device_get_data(device);
	if(!qmi) {

		connman_error("Could not get device data");
		return -ENODEV;
	}

	DBG("device %p data %p", device, qmi);
	err = connman_inet_ifdown(qmi->index);
	if(err < 0) {

		connman_error("QMI device could not getting down with ifdown");
		return err;
	}

	qmi->device_enabled = FALSE;
	/*
	 * TODO: Entfernen der Netzwerke;
	 * 		 qmi-dbus-server runterfahren;
	 * 		 Allokierte Bereiche wieder freigeben;
	 */

	delete_network(qmi);

	return 0;
}


static struct connman_device_driver qmi_driver = {
	.name		= "cellular",
	.type		= CONNMAN_DEVICE_TYPE_CELLULAR,
	.probe		= qmi_probe,
	.remove		= qmi_remove,
	.enable		= qmi_enable,
	.disable	= qmi_disable,
};



static void on_handle_qmi_connect(DBusConnection *conn, void *user_data) {

	/*
	 * Device Descriptor via D-Bus anfordern. Dabei wird automatisch das
	 * angschlossene Gerät initialisiert, eine Netzwerkverbindung hergestellt und der
	 * zugewiesene Device Descriptor (dd) zurückgegeben.
	 *
	 * FIXME: Hash-Tabelle anlegen mit destroy function "remove_qmi".
	 */

	DBG("");

//	qmi_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, remove_qmi);
//	if (qmi_hash == NULL) {
//
//		connman_error("Hash table could not be created.");
//		return;
//	}



}

static void on_handle_qmi_disconnect(DBusConnection *conn, void *user_data) {

	/*
	 * FIXME: Disconnect wird aufgerufen, wenn der qmi-dbus-server beendet wird.
	 *
	 */

	DBG("");

//	if(qmi_hash) {
//
//		g_hash_table_destroy(qmi_hash);
//		qmi_hash = NULL;
//	}

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
	.type		= CONNMAN_SERVICE_TYPE_CELLULAR,
	.probe		= tech_probe,
	.remove		= tech_remove,
};

static gint watch = 0;
//static gint watch_id = 0;

static int qmi_init(void)
{
	int err = 0;

	DBG("");

	connection = connman_dbus_get_connection();
	if (connection == NULL) {

		connman_error("D-Bus connection failed");
		return -EIO;
	}

	watch = g_dbus_add_service_watch(	connection,
										QMI_SERVICE, on_handle_qmi_connect,
										on_handle_qmi_disconnect, NULL, NULL);

//	watch_id = g_dbus_add_signal_watch(	connection, QMI_SERVICE,
//										NULL, QMI_MANAGER_INTERFACE,
//										member,
//										function,
//										NULL, NULL);



	if (watch == 0) {

		err = -EIO;
	}

	if (err == -EIO) {

		connman_error("Adding service or signal watch");
		g_dbus_remove_watch(connection, watch);
		dbus_connection_unref(connection);

		return -EIO;
	}



	err = connman_network_driver_register(&network_driver);
	if (err < 0) {

		connman_error("Register network driver");
		return err;
	}

	err = connman_device_driver_register(&qmi_driver);
	if (err < 0) {

		connman_error("Register device driver");
		connman_network_driver_unregister(&network_driver);
		return err;
	}

	err = connman_technology_driver_register(&tech_driver);
	if (err < 0) {

		connman_error("Register technology driver");
		connman_network_driver_unregister(&network_driver);
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
	connman_technology_driver_register(&tech_driver);
	g_dbus_remove_watch(connection, watch);
	dbus_connection_unref(connection);

}

CONNMAN_PLUGIN_DEFINE(qmi, "QMI plugin", CONNMAN_VERSION,
		CONNMAN_PLUGIN_PRIORITY_DEFAULT, qmi_init, qmi_exit)
