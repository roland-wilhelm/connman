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
#include <semaphore.h>

#define CONNMAN_API_SUBJECT_TO_CHANGE
#include <connman/plugin.h>
#include <connman/device.h>
#include <connman/network.h>
#include <connman/inet.h>
#include <connman/dbus.h>
#include <connman/log.h>
#include <connman/technology.h>
#include <connman/service.h>


static DBusConnection *connection = NULL;
static GHashTable *qmi_hash = NULL;
static gboolean on_looping_flag = TRUE;
static sem_t new_device_sem;


#define QMI_SERVICE "de.bmw.ltz4.qmi"



struct qmi_data {

	gint index;
	gchar *devpath;
	gchar *provider;
	gchar *imsi;
	gchar *passphrase;
	gchar *apn;
	gchar *group;
	gchar *devname;
	guint8 strength;
	connman_bool_t device_enabled;
	connman_bool_t service_connected;
	connman_bool_t modem_roaming;
	connman_bool_t modem_online;
	struct connman_device *device;
	struct connman_network *network;

	struct {

		gint dd;
		gchar *imsi;
		gint16 rsrp;
		gint16 rssi;
		gint16 snr;
		gint16 rsrq;
		guint32 ue_idle;
		guint32 globel_cell;
		guint32 channel_number;
		guint32 serving_cell;

	}modem;

};

static void delete_network(struct qmi_data *qmi)
{
	g_return_if_fail(qmi);

	DBG("%s", qmi->devpath);

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
	struct connman_service *service = NULL;

	g_return_if_fail(qmi);

	DBG("data %p", qmi);

	if(qmi->network != NULL) {

		DBG("network %p already exists.", qmi->network);
		return;
	}

	network = connman_network_create(qmi->devpath,
					CONNMAN_NETWORK_TYPE_CELLULAR);
	if(network == NULL) {

		connman_error("Network could not be created.");
		return;
	}

	DBG("network %p", qmi->network);

	index = connman_device_get_index(qmi->device);
	connman_network_set_index(network, index);
	connman_network_set_name(network, qmi->provider);
	connman_network_set_data(network, qmi);
	connman_network_set_strength(network, qmi->strength);
	connman_network_set_group(network, qmi->group);
//	connman_network_set_frequency();

	/*
	 * TODO:
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

	service = connman_service_lookup_from_network(network);
	DBG("service %p", service);
	if(service == NULL) {

		connman_error("No service available");
		return;
	}

	service = connman_service_ref(service);
	qmi->imsi = g_strdup(connman_service_get_string(service, "IMSI"));
	qmi->apn = g_strdup(connman_service_get_string(service, "APN"));
	qmi->passphrase = g_strdup(connman_service_get_string(service, "Passphrase"));
	DBG("network %p ISMI %s APN %s PW %s", qmi->network, qmi->imsi, qmi->apn, qmi->passphrase);
	connman_service_unref(service);
	if((qmi->imsi == NULL) || (qmi->apn == NULL) || (qmi->passphrase == NULL)) {

		connman_error("There are not all required parameters given");
		return;
	}

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
	 * FIXME: Netzwerk entfernen, wenn Verbindung mit QMI Device
	 * oder modem nicht mehr online ist.
	 */
//	if((qmi->modem_online == FALSE) || (qmi->service_connected == TRUE)) {
//
//		connman_error("Modem is not online or the QMI D-Bus server is not activated.");
//		return -ENODEV;
//	}


	return 0;

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

	const gchar *device_path = NULL;
	GString *dev;
	GDir *dir = NULL;
	gboolean ret;
	GError *error;
	int i;

	DBG("device name %s", devname);

	g_return_val_if_fail(devname, NULL);

	dev = g_string_new("/sys/class/net/");
	dev = g_string_append(dev, devname);
	DBG("path QMI device %s", dev->str);
	ret = g_file_test(dev->str, G_FILE_TEST_EXISTS |  G_FILE_TEST_IS_DIR);
	if(ret == FALSE) {

		connman_error("Path %s not exists", dev->str);
		g_string_free(dev, TRUE);
		return NULL;
	}

	dev = g_string_append(dev, "/device/usb/");
	DBG("path QMI device %s", dev->str);
	dir = g_dir_open(dev->str, 0, &error);
	if(dir == NULL) {

		connman_error("open path %s not possible, error %s", dev->str, error->message);
		g_string_free(dev, TRUE);
		g_error_free(error);
		error = NULL;
		return NULL;

	}

	i = 0;
	do {

		device_path = g_dir_read_name(dir);
		i++;
		if(i > 10) {
			return NULL;
		}
	}while(strncmp(device_path, "cdc-wdm", 7) != 0);

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
	qmi->imsi = NULL;
	qmi->apn = NULL;
	/* Group has to be "IMSI_qmi" */
	qmi->group = NULL;

	qmi->strength = 50;
	/* Name of the provider e.g "o2" */
	qmi->provider = NULL;
	/* Name of the specific QMI-Device e.g. wwan0 */
	qmi->devname = g_strdup(connman_device_get_string(device, "Interface"));
	/* Index of the specific QMI-Device */
	qmi->index = connman_device_get_index(device);
	/*
	 * TODO: Pfad muss noch automatisch durch devname bestimmt werden.
	 */
	qmi->devpath = get_device_path_from_name(qmi->devname);
	DBG("device name %s path %s", qmi->devname, qmi->devpath);
	if(qmi->devpath == NULL) {

		connman_error("No device path available");
		return -ENODEV;
	}

	connman_device_set_string(device, "Path", qmi->devpath);
	g_hash_table_insert(qmi_hash, qmi->devpath, qmi);

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
	delete_network(qmi);
	connman_device_set_data(device, NULL);
	connman_device_unref(qmi->device);

	g_free(qmi->apn);
	g_free(qmi->provider);
	g_free(qmi->imsi);
	g_free(qmi->devpath);
	g_free(qmi->devname);
	g_free(qmi->group);
	g_free(qmi->passphrase);
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

static void init_modems() {

	GHashTableIter iter;
	gpointer key, value;

	DBG("qmi hash %p", qmi_hash);
	if(qmi_hash == NULL) {

		return;
	}

	g_hash_table_iter_init(&iter, qmi_hash);

	while(on_looping_flag) {

		while(g_hash_table_iter_next(&iter, &key, &value) == TRUE) {

			gchar *devname = (gchar *)key;
			struct qmi_data *qmi = (struct qmi_data *)value;

			qmi->service_connected = TRUE;
		}

		sem_wait(&new_device_sem);

	}

}

static void shutdown_modems() {


}

static void on_handle_qmi_connect(DBusConnection *conn, void *user_data) {

	/*
	 * Device Descriptor via D-Bus anfordern. Dabei wird automatisch das
	 * angschlossene Gerät initialisiert, eine Netzwerkverbindung hergestellt und der
	 * zugewiesene Device Descriptor (dd) zurückgegeben.
	 *
	 * Hash-Tabelle durchgehen und alle vorhandenen Geräte initialisieren. Wenn fertig
	 * schläfen legen und auf weitere Geräte warten.
	 * thread ini_modems anlegen
	 */

	pthread_t init_modems_id;

	DBG("");

	pthread_create(&init_modems_id, NULL, (void *)init_modems, NULL);

	pthread_join(init_modems_id, NULL);

}

static void on_handle_qmi_disconnect(DBusConnection *conn, void *user_data) {

	/*
	 * FIXME: Disconnect wird aufgerufen, wenn der qmi-dbus-server beendet wird.
	 * Alle Netze entfernen. thread init_modem beenden
	 */

	DBG("");



}



static gint watch = 0;
//static gint watch_id = 0;

static int qmi_init(void)
{
	int err = 0;

	DBG("");

	err = sem_init(&new_device_sem, 0, 0);
	if(err == -1) {

		connman_error("Failure init semaphore, error %d", errno);
		return -errno;
	}

	qmi_hash = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
	if(qmi_hash == NULL) {

		connman_error("Hash table could not be created.");
		return -ENOMEM;
	}

	connection = connman_dbus_get_connection();
	if(connection == NULL) {

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

	if(qmi_hash) {

		g_hash_table_destroy(qmi_hash);
		qmi_hash = NULL;
	}

	sem_destroy(&new_device_sem);

}

CONNMAN_PLUGIN_DEFINE(qmi, "QMI plugin", CONNMAN_VERSION,
		CONNMAN_PLUGIN_PRIORITY_DEFAULT, qmi_init, qmi_exit)
