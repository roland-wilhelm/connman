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
#include <connman.h>


static DBusConnection *connection = NULL;
/* Hash table to store all connecting devices */
static GHashTable *qmi_hash = NULL;
/* D-Bus client to address qmi-dbus server */
static GDBusClient *qmi_client = NULL;
/* Proxy client to address qmi-dbus manager interface */
static GDBusProxy *qmi_proxy_manager = NULL;
/* Semaphore to signal init thread */
static sem_t new_device_sem;
/* Init thread id */
static pthread_t init_modems_id;
/*
 * Check, whether qmi-dbus is connected or not.
 * TRUE: qmi-dbus started
 * FALSE: qmi-dbus stopped
 */
static volatile gboolean qmi_service_connected = FALSE;


/* Marcros to communicate with the qmi-dbus */
#define QMI_PATH					"/de/bmw/ltz4/qmi"
#define QMI_MANAGER_PATH			"/"
#define QMI_DEVICE_PATH				QMI_PATH

#define QMI_SERVICE					"de.bmw.ltz4.qmi"
#define QMI_MANAGER_INTERFACE		QMI_SERVICE ".Manager"
#define QMI_DEVICE_INTERFACE		QMI_SERVICE ".Device"

#define OPEN_DEVICE					"OpenDevice"
#define CLOSE_DEVICE				"CloseDevice"
#define RESET_DEVICE				"ResetDevice"

#define CONNECT_DEVICE				"Connect"
#define DISCONNECT_DEVICE			"Disconnect"
#define GET_PROPERTIES				"GetProperties"

#define PROPERTY_CHANGED			"PropertyChanged"
#define STATE_CHANGED				"StateChanged"
#define TECHNOLOGY_CHANGED			"TechnologyChanged"

/**
* Each qmi device modem own this qmi data structure
*/
struct qmi_data {

	gint index;
	gchar *devpath;
	gchar *provider;
	gchar *imsi;
	gchar *passphrase;
	gchar *apn;
	gchar *group;
	gchar *devname;
	gchar *object_path;
	gchar *username;
	gchar *packet_status;
	gchar *rat;
	gchar *network_type;
	gchar *mnc;
	gchar *mcc;
	guint8 strength;
	gint32 rsrq;
	/* Proxy client to address qmi-dbus device interface */
	GDBusProxy *qmi_proxy_device;
	connman_bool_t modem_opening;
	connman_bool_t modem_opened;
	connman_bool_t modem_connected;
	struct connman_device *device;
	struct connman_network *network;

};

/* Forward declarations */
static gboolean set_reply_to_qmi_data(DBusMessageIter *main_iter, struct qmi_data *qmi);

/**
* @brief Do not call it.
* It will be called automatically after an entry in the hash table was removed
*/
static void free_hash_values(gpointer data) {

	struct qmi_data *qmi = (struct qmi_data *)data;

	DBG();

	if(qmi == NULL)
		return;

	g_free(qmi->apn);
	g_free(qmi->provider);
	g_free(qmi->imsi);
	g_free(qmi->devpath);
	g_free(qmi->devname);
	g_free(qmi->group);
	g_free(qmi->passphrase);
	g_free(qmi->object_path);
	g_free(qmi->username);
	g_dbus_proxy_unref(qmi->qmi_proxy_device);

	g_free(qmi);
}

/**
* @brief Remove the network associated to its qmi device
*/
static void delete_network(struct qmi_data *qmi)
{
	g_return_if_fail(qmi);

	DBG("Network %p", qmi->network);

	if(qmi->network == NULL)
		return;

	DBG("network %p", qmi->network);

	connman_device_remove_network(qmi->device, qmi->network);
	qmi->network = NULL;

}

/**
* @brief Add the network associated to its qmi device
*/
static void add_network(struct qmi_data *qmi)
{
	struct connman_network *network = NULL;
	int index;
	struct connman_service *service = NULL;

	g_return_if_fail(qmi);

	DBG("data %p", qmi);

	if(qmi->network != NULL) {

		DBG("network %p already exists.", qmi->network);
		return;
	}

	/* Create an new qmi network */
	network = connman_network_create(qmi->devpath, CONNMAN_NETWORK_TYPE_QMI);
	if(network == NULL) {

		connman_error("Network could not be created.");
		return;
	}

	DBG("network %p", qmi->network);

	/* Set network properties */
	index = connman_device_get_index(qmi->device);
	connman_network_set_index(network, index);
	connman_network_set_data(network, qmi);
	connman_network_set_strength(network, qmi->strength);
	connman_network_set_group(network, qmi->group);

	if(qmi->provider)
		g_free(qmi->provider);
	qmi->provider = g_strdup("no-name");
	connman_network_set_name(network, qmi->provider);

	/* Add the created network */
	if (connman_device_add_network(qmi->device, network) < 0) {

		connman_error("Network not added to the device.");
		connman_network_unref(network);
		network = NULL;
		return;
	}

	qmi->network = network;

	/* Get configured values from the created *.config file */
	service = connman_service_lookup_from_network(network);
	DBG("service %p", service);
	if(service == NULL) {

		connman_error("No service available");
		return;
	}

	service = connman_service_ref(service);
	if(qmi->apn)
		g_free(qmi->apn);
	qmi->apn = g_strdup(connman_service_get_string(service, "APN"));

	if(qmi->passphrase)
		g_free(qmi->passphrase);
	qmi->passphrase = g_strdup(connman_service_get_string(service, "Passphrase"));

	if(qmi->username)
		g_free(qmi->username);
	qmi->username = g_strdup(connman_service_get_string(service, "Username"));

	if(qmi->provider)
		g_free(qmi->provider);
	qmi->provider = g_strdup(connman_service_get_string(service, "Provider"));
	if(qmi->provider == NULL)
		qmi->provider = g_strdup("no-name");

	connman_network_set_name(network, qmi->provider);
	connman_network_update(qmi->network);


    DBG("network %p IMSI %s APN %s PW %s Username %s", qmi->network, qmi->imsi, qmi->apn, qmi->passphrase, qmi->username);
	connman_service_unref(service);
	if((qmi->imsi == NULL) || (qmi->apn == NULL) || (qmi->passphrase == NULL)) {

		connman_error("There are not all required parameters available");
		return;
	}

}

static int network_probe(struct connman_network *network)
{

	DBG("network %p", network);

	return 0;
}

/**
* @brief Remove connman network
*/
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

/**
* @brief Callback getting properties
*/
static void
get_properties_callback(DBusMessage *message, void *user_data) {

	DBusMessageIter iter;
	struct qmi_data *qmi = (struct qmi_data *)user_data;

	DBG("QMI data %p D-Bus message %p", qmi, message);

	if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {

		const char *dbus_error = dbus_message_get_error_name(message);

		connman_error("%s", dbus_error);
		return;

	}

	if(dbus_message_iter_init(message, &iter) == FALSE) {

		connman_error("Failure init ITER");

		return;
	}

	if(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_INVALID) {

		connman_error("Invalid D-Bus type");

		return;
	}

	/* Set all received properties */
	if(set_reply_to_qmi_data(&iter, qmi) == FALSE) {

		connman_error("Failure parse D-Bus message");

		return;
	}


}

/**
* @brief Callback connecting network
*/
static void
network_connect_callback(DBusMessage *message, void *user_data) {

	struct qmi_data *qmi = (struct qmi_data *)user_data;

	DBG("qmi data %p DBusmessage %p", qmi, message);

	g_return_if_fail(qmi);

	if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {
		const char *dbus_error = dbus_message_get_error_name(message);

		/*Failure during connecting */
		qmi->modem_connected = FALSE;
		if(qmi->network)
			connman_network_set_connected(qmi->network, FALSE);

		connman_error("%s", dbus_error);

		return;
	}

	/* Connected successfully */
	qmi->modem_connected = TRUE;
	connman_network_set_connected(qmi->network, TRUE);

	DBG("Device %s connected %d", qmi->devpath, qmi->modem_connected);

	g_assert(qmi->qmi_proxy_device);
	/* Request all available properties */
	g_dbus_proxy_method_call(	qmi->qmi_proxy_device,
								GET_PROPERTIES,
								NULL,
								get_properties_callback,
								qmi,
								NULL);

    //FIXME Hack NAT enable by default
    __connman_nat_enable("qmi", NULL, 0);


}

/**
* @brief Append required properties to make connecting possible
*/
static void
network_connect_append(DBusMessageIter *iter, void *user_data) {

	struct qmi_data *qmi = (struct qmi_data *)user_data;
	gchar *user, *pw, *apn;

	DBG("qmi data %p messageIter %p", qmi, iter);

	g_return_if_fail(qmi);

	user = qmi->username ? qmi->username : "";
	pw = qmi->passphrase ? qmi->passphrase : "";
	apn = qmi->apn ? qmi->apn : "";


	DBG("Device path %s object path %s", qmi->devpath, qmi->object_path);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &user);
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &pw);
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &apn);

}

/**
* @brief Connect the modem associated to its connman network
*/
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
	/* Request connecting qmi network */
	g_dbus_proxy_method_call(	qmi->qmi_proxy_device,
								CONNECT_DEVICE,
								network_connect_append,
								network_connect_callback,
								qmi,
								NULL);

	return 0;
}

/**
* @brief Callback disconnect modem
*/
static void
network_disconnect_callback(DBusMessage *message, void *user_data) {

	struct qmi_data *qmi = (struct qmi_data *)user_data;

	DBG("qmi data %p DBusmessage %p", qmi, message);

	g_return_if_fail(qmi);

	if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {
		const char *dbus_error = dbus_message_get_error_name(message);

		connman_error("%s", dbus_error);

	}

	DBG("Device %s connected %d", qmi->devpath, qmi->modem_connected);

}

/**
* @brief Disconnect the associated modem
*/
static int network_disconnect(struct connman_network *network)
{
	struct qmi_data *qmi = connman_network_get_data(network);

	DBG("Network %p %p", network, qmi);

	qmi = connman_network_get_data(network);
	if(!qmi) {

		connman_error("Could not get device data.");
		return -ENODEV;
	}

	DBG("Network %p %p", network, qmi);

	qmi->modem_connected = FALSE;
	if(qmi->network)
		connman_network_set_connected(qmi->network, FALSE);

	//FIXME Hack NAT enable by default
	__connman_nat_disable("qmi");

	g_assert(qmi->qmi_proxy_device);

	/* Request disconnect */
	g_dbus_proxy_method_call(	qmi->qmi_proxy_device,
								DISCONNECT_DEVICE,
								NULL,
								network_disconnect_callback,
								qmi,
								NULL);

	return 0;
}

static struct connman_network_driver network_driver = {
	.name		= "qmi",
	.type		= CONNMAN_NETWORK_TYPE_QMI,
	.probe		= network_probe,
	.remove		= network_remove,
	.connect	= network_connect,
	.disconnect	= network_disconnect,
};

/**
* @brief Parse network interface name (qmi0) to get the associated device path (/dev/cdc-wdm0).
*/
static gchar* get_device_path_from_name(const gchar *devname) {

	const gchar *cdc_wdm = NULL;
	GString *qmi_path;
	gchar *qmi_link = NULL, *device_link = NULL;
	gchar *help;
	GDir *dir = NULL;
	gboolean ret;
	GError *error = NULL;
	gboolean found = FALSE;


	DBG("device name %s", devname);

	g_return_val_if_fail(devname, NULL);

	qmi_path = g_string_new("/sys/class/net/");
	qmi_path = g_string_append(qmi_path, devname);
	DBG("QMI device %s", qmi_path->str);
	ret = g_file_test(qmi_path->str, G_FILE_TEST_EXISTS |  G_FILE_TEST_IS_DIR);
	if(ret == FALSE) {

		connman_error("Path %s not exists", qmi_path->str);
		g_string_free(qmi_path, TRUE);
		return NULL;
	}

	/* /sys/class/net/qmi0/device */
	qmi_path = g_string_append(qmi_path, "/device");
	DBG("QMI device file %s", qmi_path->str);

	/* read sym link of device */
	qmi_link = g_file_read_link(qmi_path->str, &error);
	if(qmi_link == NULL) {

		connman_error("Failure sym link reading %s", error->message);
		g_string_free(qmi_path, TRUE);
		g_error_free(error);
		error = NULL;
		return NULL;
	}

	/* get last occurrence "/" of device sym link */
	help = g_strdup(g_strrstr(qmi_link, "/"));
	g_free(qmi_link);
	qmi_link = help;
	DBG("Qmi device file sym link %s", qmi_link);

	g_string_free(qmi_path, TRUE);

	/* check, whether "/sys/class/usb/" or "/sys/class/usbmisc/" exists and use it*/
	qmi_path = g_string_new("/sys/class/usb/");
	DBG("QMI cdc-wdm %s", qmi_path->str);
	ret = g_file_test(qmi_path->str, G_FILE_TEST_EXISTS |  G_FILE_TEST_IS_DIR);
	if(ret == FALSE) {

		g_string_free(qmi_path, TRUE);
		qmi_path = g_string_new("/sys/class/usbmisc/");
		DBG("QMI cdc-wdm %s", qmi_path->str);
		ret = g_file_test(qmi_path->str, G_FILE_TEST_EXISTS |  G_FILE_TEST_IS_DIR);
		if(ret == FALSE) {

			connman_error("Path %s not exists", qmi_path->str);
			g_string_free(qmi_path, TRUE);
			g_free(qmi_link);

			return NULL;
		}
	}

	dir = g_dir_open(qmi_path->str, 0, &error);
	if(dir == NULL) {

		connman_error("open path %s not possible, error %s", qmi_path->str, error->message);
		g_string_free(qmi_path, TRUE);
		g_error_free(error);
		error = NULL;
		g_free(qmi_link);

		return NULL;

	}

	while((cdc_wdm = g_dir_read_name(dir)) != NULL) {


		if(strncmp(cdc_wdm, "cdc-wdm", 7) != 0)
			continue;

		help = g_strdup_printf("%s%s/%s", qmi_path->str, cdc_wdm, "device");
		DBG("QMI cdc-wdm device file %s", help);
		device_link = g_file_read_link(help, &error);
		g_free(help);
		if(device_link == NULL) {

			connman_error("Failure sym link reading %s", error->message);
			g_error_free(error);
			error = NULL;

			continue;
		}

		/* get last occurrence "/" of device sym link */
		help = g_strdup(g_strrstr(device_link, "/"));
		g_free(device_link);
		device_link = help;

		DBG("QMI cdc-wdm device file sym link %s", device_link);

		if(strncmp(qmi_link, device_link, 9) == 0) {

			found = TRUE;

			break;
		}

	}

	g_free(qmi_link);
	g_free(device_link);
	g_string_free(qmi_path, TRUE);
	g_dir_close(dir);

	if(found == TRUE) {

		found = FALSE;

		qmi_path = g_string_new(cdc_wdm);
		qmi_path = g_string_prepend(qmi_path, "/dev/");
		DBG("device name %s path %s", devname, qmi_path->str);

		return g_string_free(qmi_path, FALSE);
	}
	else {

		connman_error("No device path to device %s found", devname);
		return NULL;
	}
}

/**
* @brief Create a new qmi device, set default values and insert it in the hash table.
*/
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
	qmi->qmi_proxy_device = NULL;
	qmi->modem_opening = FALSE;
	qmi->modem_connected = FALSE;
	qmi->modem_opened = FALSE;

	qmi->imsi = NULL;
	qmi->apn = NULL;
	/* Group has to be "IMSI_qmi" */
	qmi->group = NULL;

	qmi->strength = 0;
	/* Name of the provider e.g "o2" */
	qmi->provider = NULL;
	/* Name of the specific QMI-Device e.g. wwan0 */
	qmi->devname = g_strdup(connman_device_get_string(device, "Interface"));
	/* Index of the specific QMI-Device */
	qmi->index = connman_device_get_index(device);

	qmi->devpath = get_device_path_from_name(qmi->devname);
	qmi->object_path = NULL;
	qmi->qmi_proxy_device = NULL;
	DBG("device name %s path %s", qmi->devname, qmi->devpath);
	if(qmi->devpath == NULL) {

		connman_error("No device path available");
		return -ENODEV;
	}

	connman_device_set_string(device, "Path", qmi->devpath);

	g_hash_table_insert(qmi_hash, qmi->devpath, qmi);

	/* Signal to init thread a new connected modem is available. */
	sem_post(&new_device_sem);

	return 0;
}

/**
* @brief Remove qmi device
*/
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

	/*
	 * Just delete the qmi device entry in the hash table.
	 * Modem can not be closed, because modem is not available anymore.
	 */
	g_hash_table_remove(qmi_hash, qmi->devpath);

}

/**
* @brief Enable qmi device and add network if modem opened
*/
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

	/* Add new qmi network interface */
	err = connman_inet_ifup(qmi->index);
	if(err < 0) {

		connman_error("QMI device could not getting up with ifup");
		return err;
	}

	if(qmi->modem_opened == TRUE) {

		add_network(qmi);
	}

	return 0;
}

/**
* @brief Disable qmi device and delete the associated network
*/
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
	/* Remove qmi network interface */
	err = connman_inet_ifdown(qmi->index);
	if(err < 0) {

		connman_error("QMI device could not getting down with ifdown");
		return err;
	}

	/* Delete associated qmi network */
	delete_network(qmi);

	return 0;
}

static struct connman_device_driver qmi_driver = {
	.name		= "qmi",
	.type		= CONNMAN_DEVICE_TYPE_QMI,
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
	.name				= "qmi",
	.type				= CONNMAN_SERVICE_TYPE_QMI,
	.probe				= tech_probe,
	.remove				= tech_remove,
};

/**
* @brief Calculate the new received rsrq to strength
*/
static guint8
calculate_signal_strength(gint32 rsrq) {

	/* rsrp range (-3 - (-20)) to signal strength range (0 - 100) */

	guint8 strength;
	/* 100/ dRSRQ = 100/ 17 = 5,882352941 */
	if(rsrq == 0)
		return 0;

	strength = (rsrq + 20) * 5,882352941;
	if(strength > 100)
		strength = 100;

	DBG("Signal strength %u RSRQ %d", strength, rsrq);

	return strength;
}

/**
* @brief Set the newly replied key/ value properties
*/
static gboolean
update_network(struct qmi_data *qmi, DBusMessageIter *entry_iter) {

	DBusMessageIter variant_iter;
	gboolean updating_strength = FALSE;
	gboolean updating_others = FALSE;
	gchar *key;
	gchar *help;



	if(dbus_message_iter_get_arg_type(entry_iter) != DBUS_TYPE_STRING)
		return FALSE;

	dbus_message_iter_get_basic(entry_iter, &key);
	DBG("Property %s", key);
	if(key != NULL) {

		dbus_message_iter_next(entry_iter);

		if(dbus_message_iter_get_arg_type(entry_iter) != DBUS_TYPE_VARIANT)
			return FALSE;

		dbus_message_iter_recurse(entry_iter, &variant_iter);

		if(g_strcmp0(key, "IMSI") == 0) {
			/* Set new IMSI */
			if(dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING)
				return FALSE;

			dbus_message_iter_get_basic(&variant_iter, &help);
			if(qmi->imsi)
				g_free(qmi->imsi);
			qmi->imsi = g_strdup(help);

		}
		else if(g_strcmp0(key, "RSRQ") == 0) {
			/* Set new RSRQ */
			guint8 help = 0;

			if(qmi->network == NULL)
				return FALSE;

			if(dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_INT32)
				return FALSE;

			dbus_message_iter_get_basic(&variant_iter, &qmi->rsrq);
			help = calculate_signal_strength(qmi->rsrq);
			if(help != qmi->strength) {

				qmi->strength = help;
				/* Set new calculated strength */

				connman_network_set_strength(qmi->network, qmi->strength);

				updating_strength = TRUE;
			}


		}
		else if(g_strcmp0(key, "PacketStatus") == 0) {
			/* Set new packet status */
			if(qmi->network == NULL)
				return FALSE;

			if(dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING)
				return FALSE;

			dbus_message_iter_get_basic(&variant_iter, &help);
			if(g_strcmp0(qmi->packet_status, help) != 0) {

				if(qmi->packet_status)
					g_free(qmi->packet_status);
				qmi->packet_status = g_strdup(help);

				/* Check new packet status and change network status if required */
				if(g_strcmp0(qmi->packet_status, "connected") == 0) {

					connman_network_set_connected(qmi->network, TRUE);
				}
				else {

					connman_network_set_connected(qmi->network, FALSE);
				}

			}


		}
		else if(g_strcmp0(key, "NetworkType") == 0) {
			/* Set new network type */
			if(dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING)
				return FALSE;

			dbus_message_iter_get_basic(&variant_iter, &help);
			if(g_strcmp0(qmi->network_type, help) != 0) {

				if(qmi->network_type)
					g_free(qmi->network_type);
				qmi->network_type = g_strdup(help);

				updating_others = TRUE;

			}


		}
		else if(g_strcmp0(key, "MCC") == 0) {
			/* Set new MCC */
			if(dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING)
				return FALSE;

			dbus_message_iter_get_basic(&variant_iter, &help);
			if(g_strcmp0(qmi->mcc, help) != 0) {

				if(qmi->mcc)
					g_free(qmi->mcc);
				qmi->mcc = g_strdup(help);

				updating_others = TRUE;

			}


		}
		else if(g_strcmp0(key, "MNC") == 0) {
			/* Set new MNC */
			if(dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING)
				return FALSE;

			dbus_message_iter_get_basic(&variant_iter, &help);
			if(g_strcmp0(qmi->mnc, help) != 0) {

				if(qmi->mnc)
					g_free(qmi->mnc);
				qmi->mnc = g_strdup(help);

				updating_others = TRUE;

			}


		}
		else if(g_strcmp0(key, "RAT") == 0) {
			/*Set new RAT */
			if(dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING)
				return FALSE;

			dbus_message_iter_get_basic(&variant_iter, &help);
			if(g_strcmp0(qmi->rat, help) != 0) {

				if(qmi->rat)
					g_free(qmi->rat);
				qmi->rat = g_strdup(help);

				updating_others = TRUE;

			}

		}

		/* Check whether network already exists, because some properties as IMSI or IMEI are set before network is added */
		if(qmi->network) {

			if(updating_strength == TRUE) {
				/* Just update new strength property */
				connman_network_update(qmi->network);
			}
			else if(updating_others == TRUE) {
				/* Build a new network name and update it */
				if(qmi->provider)
					g_free(qmi->provider);

				qmi->provider = g_strdup_printf("%s-%s-%s", qmi->mnc, qmi->mcc, qmi->rat);

				connman_network_set_name(qmi->network, qmi->provider);
				connman_network_update(qmi->network);

			}

		}

	}

	return TRUE;
}

/**
* @brief Parse reply, D-Bus array type
*/
static gboolean
set_reply_to_qmi_data(DBusMessageIter *main_iter, struct qmi_data *qmi) {

	DBusMessageIter dict;

	DBG();

	if(dbus_message_iter_get_arg_type(main_iter) != DBUS_TYPE_ARRAY) {

		connman_error("Message is not a D-Bus array type");

		return FALSE;
	}

	dbus_message_iter_recurse(main_iter, &dict);

	/* Parse each D-Bus dictionary entry */
	while(dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {

		DBusMessageIter entry;

		dbus_message_iter_recurse(&dict, &entry);

		/* Set new key/ value properties */
		update_network(qmi, &entry);

		dbus_message_iter_next(&dict);
	}


	return TRUE;
}



/**
* @brief Callback getting available properties (IMSI, IMEI, ...) from qmi-dbus and
* finally add connman network
*/
static void
open_modem_get_properties_callback(DBusMessage *message, void *user_data) {

	DBusMessageIter iter;
	struct qmi_data *qmi = (struct qmi_data *)user_data;

	DBG("QMI data %p D-Bus message %p", qmi, message);

	if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {

		const char *dbus_error = dbus_message_get_error_name(message);
		qmi->modem_opening = FALSE;
		connman_error("%s", dbus_error);
		return;

	}

	if(dbus_message_iter_init(message, &iter) == FALSE) {

		connman_error("Failure init ITER");
		qmi->modem_opening = FALSE;
		return;
	}

	if(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_INVALID) {

		connman_error("Invalid D-Bus type");
		qmi->modem_opening = FALSE;
		return;
	}

	/* Set new D-Bus reply properties */
	if(set_reply_to_qmi_data(&iter, qmi) == FALSE) {

		connman_error("Failure parse D-Bus message");

		return;
	}


	DBG("Modem opened %d", qmi->modem_opened);

	DBG("IMSI %s", qmi->imsi);

	/* Set a new connman network group of the newly replied IMSI */
	if(qmi->group)
		g_free(qmi->group);
	qmi->group = g_strdup_printf("%s_none", qmi->imsi);

	/* Modem was opened successfully, add the new device to connman */
	qmi->modem_opened = TRUE;
	qmi->modem_opening = FALSE;

	add_network(qmi);

}

/**
* @brief Callback qmi device open modem
*/
static void
open_modem_callback(DBusMessage *message, void *user_data) {

	DBusMessageIter iter;
	struct qmi_data *qmi = (struct qmi_data *)user_data;
	gchar *help;

	DBG("qmi data %p DBusmessage %p", qmi, message);

	if(qmi == NULL) {

		connman_error("No QMI device");
		qmi->modem_opening = FALSE;
		return;
	}

	if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {
		const char *dbus_error = dbus_message_get_error_name(message);

		connman_error("%s", dbus_error);
		qmi->modem_opening = FALSE;
		return;

	}

	if((dbus_message_iter_init(message, &iter) == TRUE) &&
			(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH)) {

		dbus_message_iter_get_basic(&iter, &help);
		qmi->object_path = g_strdup(help);
	}
	else {

		connman_error("Return type invalid");
		qmi->modem_opening = FALSE;
		return;
	}

	DBG("Modem opened object path %s", qmi->object_path);

	if(!qmi->qmi_proxy_device) {

		qmi->qmi_proxy_device = g_dbus_proxy_new(	qmi_client,
												qmi->object_path,
												QMI_DEVICE_INTERFACE);
	}

	if(qmi->qmi_proxy_device == NULL) {

		connman_error("QMI proxy device not created");
		/* Just return, if failure we have a memory issue, then. */
		return;
	}

	g_dbus_proxy_method_call(	qmi->qmi_proxy_device,
								GET_PROPERTIES,
								NULL,
								open_modem_get_properties_callback,
								qmi,
								NULL);

}

/**
* @brief Append qmi device to open modem
*/
static void
open_modem_append(DBusMessageIter *iter, void *user_data) {

	struct qmi_data *qmi = (struct qmi_data *)user_data;

	DBG("qmi data %p messageIter %p", qmi, iter);

	if((qmi == NULL) || (iter == NULL)) {

		qmi->modem_opening = FALSE;
		return;
	}

	DBG("Device path %s", qmi->devpath);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &qmi->devpath);

}

/**
* @brief Thread to init qmi devices
*
* Will be called to init a new connected qmi devices.
* Open Modem via qmi-dbus manager interface, get all currently available properties,
* set connman properties and finally add a new connman device.
*/
static void*
init_modems_thread(gpointer unused) {

	GHashTableIter iter;
	gpointer key, value;


	DBG("qmi hash %p", qmi_hash);

	if(qmi_hash == NULL) {

		return NULL;
	}

	/* Run as long as the qmi-dbus runs */
	while(qmi_service_connected) {

		g_hash_table_iter_init(&iter, qmi_hash);
		while(g_hash_table_iter_next(&iter, &key, &value) == TRUE) {

			/* Only connected devices should be in the hash table, hopefully */
			struct qmi_data *qmi = (struct qmi_data *)value;

			if( (qmi->modem_opened == TRUE) || 		/* Device already opened */
				(qmi->modem_opening == TRUE) ) {	/* Device trying to connect */

				continue;
			}

			DBG("Trying to open device %s", qmi->devpath);
			qmi->modem_opening = TRUE;
			g_dbus_proxy_method_call(	qmi_proxy_manager,
										OPEN_DEVICE,
										open_modem_append,
										open_modem_callback,
										value,
										NULL);

		}

		sem_wait(&new_device_sem);
	}
	DBG("Thread aborted");

	return NULL;

}

/**
* @brief Signal handler for qmi connect signal from qmi-dbus.
*
* Will be called, if qmi-dbus started.
*/
static void on_handle_qmi_connect(DBusConnection *conn, gpointer unused) {

	int err = 0;

	DBG("");

	/* qmi-dbus started */
	qmi_service_connected = TRUE;
	/* Create thread to init qmi devices */
	err = pthread_create(&init_modems_id, NULL, &init_modems_thread, qmi_proxy_manager);
	if(err < 0) {

		connman_error("Thread not created");
		return;
	}
	DBG("Thread started");

}

/**
* @brief Signal handler for qmi disconnect signal from qmi-dbus.
*
* Will be called, if qmi-dbus stopped.
*/
static void on_handle_qmi_disconnect(DBusConnection *conn, gpointer unused) {

	GHashTableIter iter;
	gpointer key, value;

	DBG("");

	/* qmi-dbus was stopped */
	qmi_service_connected = FALSE;

	g_hash_table_iter_init(&iter, qmi_hash);
	while(g_hash_table_iter_next(&iter, &key, &value) == TRUE) {

		/* Set all parameter to default */
		struct qmi_data *qmi = (struct qmi_data *)value;
		if(qmi->network)
			connman_network_set_connected(qmi->network, FALSE);
		qmi->modem_opened = FALSE;
		qmi->modem_opening = FALSE;
		qmi->modem_connected = FALSE;
		/* Delete connman network */
		delete_network(qmi);

	}

	/* Required for terminating init thread */
	sem_post(&new_device_sem);
	pthread_join(init_modems_id, NULL);
}

/**
* @brief Signal handler for property changed signal from qmi-dbus
*/
static gboolean
on_handle_property_changed(DBusConnection *conn, DBusMessage *message,	gpointer unused) {

	DBusMessageIter message_iter;
	GHashTableIter hash_iter;
	gpointer key, value;
	struct qmi_data *qmi = NULL;
	/* Get object path of this message */
	const gchar *object_path = dbus_message_get_path(message);;

	DBG("Object path %s", object_path);

	if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {

		const char *dbus_error = dbus_message_get_error_name(message);

		connman_error("%s", dbus_error);
		return FALSE;

	}

	if(dbus_message_iter_init(message, &message_iter) == FALSE) {

		connman_error("Failure init ITER");

		return FALSE;
	}

	if(dbus_message_iter_get_arg_type(&message_iter) == DBUS_TYPE_INVALID) {

		connman_error("Invalid D-Bus type");

		return FALSE;
	}

	if(object_path) {
		/* Find qmi device data to object path */
		g_hash_table_iter_init (&hash_iter, qmi_hash);
		while (g_hash_table_iter_next (&hash_iter, &key, &value)) {

			qmi = (struct qmi_data *)value;

			if(g_strcmp0(object_path, qmi->object_path) == 0) {

				break;
			}
			else {

				qmi = NULL;
			}
		}
	}

	if(qmi == NULL) {

		connman_error("No qmi device to object path %s", object_path);

		return FALSE;
	}

	/* Update new property value */
	update_network(qmi, &message_iter);


	return TRUE;

}

/**
* @brief Signal handler for state changed signal from qmi-dbus
*/
static gboolean
on_handle_state_changed(DBusConnection *conn, DBusMessage *message,	gpointer unused) {

	DBusMessageIter message_iter;
	GHashTableIter hash_iter;
	gpointer key, value;
	struct qmi_data *qmi = NULL;
	/* Get object path of this message */
	const gchar *object_path = dbus_message_get_path(message);;

	DBG("Object path %s", object_path);

	if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {

		const char *dbus_error = dbus_message_get_error_name(message);

		connman_error("%s", dbus_error);
		return FALSE;

	}

	if(dbus_message_iter_init(message, &message_iter) == FALSE) {

		connman_error("Failure init ITER");

		return FALSE;
	}

	if(dbus_message_iter_get_arg_type(&message_iter) == DBUS_TYPE_INVALID) {

		connman_error("Invalid D-Bus type");

		return FALSE;
	}

	if(object_path) {
		/* Find qmi device data to object path */
		g_hash_table_iter_init (&hash_iter, qmi_hash);
		while (g_hash_table_iter_next (&hash_iter, &key, &value)) {

			qmi = (struct qmi_data *)value;

			if(g_strcmp0(object_path, qmi->object_path) == 0) {

				break;
			}
			else {

				qmi = NULL;
			}
		}
	}

	if(qmi == NULL) {

		connman_error("No qmi device to object path %s", object_path);

		return FALSE;
	}

	/* Update new property value */
	update_network(qmi, &message_iter);

	return TRUE;
}

/**
* @brief Signal handler for technology changed signal from qmi-dbus
*/
static gboolean
on_handle_technology_changed(DBusConnection *conn, DBusMessage *message, gpointer unused) {

	DBusMessageIter message_iter;
	GHashTableIter hash_iter;
	gpointer key, value;
	struct qmi_data *qmi = NULL;
	/* Get object path of this message */
	const gchar *object_path = dbus_message_get_path(message);;

	DBG("Object path %s", object_path);

	if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {

		const char *dbus_error = dbus_message_get_error_name(message);

		connman_error("%s", dbus_error);
		return FALSE;

	}

	if(dbus_message_iter_init(message, &message_iter) == FALSE) {

		connman_error("Failure init ITER");

		return FALSE;
	}

	if(dbus_message_iter_get_arg_type(&message_iter) == DBUS_TYPE_INVALID) {

		connman_error("Invalid D-Bus type");

		return FALSE;
	}

	if(object_path) {
		/* Find qmi device data to object path */
		g_hash_table_iter_init (&hash_iter, qmi_hash);
		while (g_hash_table_iter_next (&hash_iter, &key, &value)) {

			qmi = (struct qmi_data *)value;

			if(g_strcmp0(object_path, qmi->object_path) == 0) {

				break;
			}
			else {

				qmi = NULL;
			}
		}
	}

	if(qmi == NULL) {

		connman_error("No qmi device to object path %s", object_path);

		return FALSE;
	}

	/* Update new property value */
	update_network(qmi, &message_iter);

	return TRUE;
}


static gint watch_service = 0;
static gint watch_property_changed = 0;
static gint watch_state_changed = 0;
static gint watch_technology_changed = 0;


static int qmi_init(void)
{
	int err = 0;

	DBG("");

	err = sem_init(&new_device_sem, 0, 0);
	if(err == -1) {

		connman_error("Failure init semaphore, error %d", errno);
		return -errno;
	}

	/* Create new hash table to store all connecting devices */
	qmi_hash = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free_hash_values);
	if(qmi_hash == NULL) {

		connman_error("Hash table could not be created.");
		return -ENOMEM;
	}

	connection = connman_dbus_get_connection();
	if(connection == NULL) {

		connman_error("D-Bus connection failed");
		return -EIO;
	}

	/* Create new D-Bus client to address qmi-dbus server */
	qmi_client = g_dbus_client_new(	connection,
									QMI_SERVICE,
									QMI_PATH);
	if(qmi_client == NULL) {

		connman_error("D-Bus client not created");
		return -EIO;
	}

	/* Create new proxy client to address qmi-dbus manager interface */
	qmi_proxy_manager = g_dbus_proxy_new(	qmi_client,
											QMI_MANAGER_PATH,
											QMI_MANAGER_INTERFACE);

	if(qmi_proxy_manager == NULL) {

		connman_error("QMI proxy manager not created");
		g_dbus_client_unref(qmi_client);
		return -EIO;
	}

	/* Watch appearing qmi-dbus */
	watch_service = g_dbus_add_service_watch(	connection,
												QMI_SERVICE,
												on_handle_qmi_connect,
												on_handle_qmi_disconnect,
												NULL, NULL);

	/* Watching qmi-dbus signals */
	watch_property_changed = g_dbus_add_signal_watch(	connection,
														QMI_SERVICE, NULL,
														QMI_DEVICE_INTERFACE, PROPERTY_CHANGED,
														on_handle_property_changed,
														NULL, NULL);

	watch_state_changed = g_dbus_add_signal_watch(	connection,
													QMI_SERVICE, NULL,
													QMI_DEVICE_INTERFACE, STATE_CHANGED,
													on_handle_state_changed,
													NULL, NULL);

	watch_technology_changed = g_dbus_add_signal_watch(	connection,
														QMI_SERVICE, NULL,
														QMI_DEVICE_INTERFACE, TECHNOLOGY_CHANGED,
														on_handle_technology_changed,
														NULL, NULL);


	if((watch_service == 0) ||
		(watch_property_changed == 0) ||
		(watch_state_changed == 0) ||
		(watch_technology_changed == 0))	{

		connman_error("Adding service or signal watch");
		err = -EIO;
		goto remove;
	}

	err = connman_network_driver_register(&network_driver);
	if(err < 0) {

		connman_error("Register network driver");
		goto remove;
	}

	err = connman_device_driver_register(&qmi_driver);
	if(err < 0) {

		connman_error("Register device driver");
		connman_network_driver_unregister(&network_driver);
		goto remove;
	}

	err = connman_technology_driver_register(&tech_driver);
	if(err < 0) {

		connman_error("Register technology driver");
		connman_network_driver_unregister(&network_driver);
		connman_device_driver_unregister(&qmi_driver);
		goto remove;
	}

	return 0;

remove:

	g_dbus_remove_watch(connection, watch_service);
	g_dbus_remove_watch(connection, watch_property_changed);
	g_dbus_remove_watch(connection, watch_state_changed);
	g_dbus_remove_watch(connection, watch_technology_changed);
	g_dbus_proxy_unref(qmi_proxy_manager);
	g_dbus_client_unref(qmi_client);
	dbus_connection_unref(connection);

	return err;
}

/**
* @brief Callback closing modem
*/
static void close_modem_callback(DBusMessage *message, void *unused) {

	DBG("DBusmessage %p", message);


	if(dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {
		const char *dbus_error = dbus_message_get_error_name(message);

		connman_error("%s", dbus_error);
		return;

	}

}

/**
* @brief Append modem device path to close
*/
static void close_modem_append(DBusMessageIter *iter, void *user_data) {

	struct qmi_data *qmi = (struct qmi_data *)user_data;

	DBG("qmi data %p messageIter %p", qmi, iter);

	g_return_if_fail(qmi);

	DBG("Device path %s", qmi->devpath);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &qmi->devpath);

}

/**
* @brief Close a opened modem
*/
static void close_modem(gpointer key, gpointer value, gpointer user_data) {

	struct qmi_data *qmi = (struct qmi_data *)value;

	DBG("QMI data %p", qmi);

	g_return_if_fail(qmi);

	/* Only request closing modem if qmi-dbus runs */
	if(qmi_service_connected == FALSE)
		return;

	g_dbus_proxy_method_call(	qmi_proxy_manager,
								CLOSE_DEVICE,
								close_modem_append,
								close_modem_callback,
								qmi,
								NULL);


}

static void qmi_exit(void)
{
	DBG("");

	/* Remove all watching signals */
	g_dbus_remove_watch(connection, watch_service);
	g_dbus_remove_watch(connection, watch_property_changed);
	g_dbus_remove_watch(connection, watch_state_changed);
	g_dbus_remove_watch(connection, watch_technology_changed);

	/* Close all opened qmi devices, do this before unref all D-Bus connections */
	if(qmi_hash) {

		g_hash_table_foreach(qmi_hash, close_modem, NULL);

	}

	qmi_service_connected = FALSE;

	/* Unregister all drivers */
	connman_device_driver_unregister(&qmi_driver);
	connman_network_driver_unregister(&network_driver);
	connman_technology_driver_unregister(&tech_driver);

	g_dbus_proxy_unref(qmi_proxy_manager);
	g_dbus_client_unref(qmi_client);
	dbus_connection_unref(connection);

	sem_post(&new_device_sem);
	pthread_join(init_modems_id, NULL);
	sem_destroy(&new_device_sem);

	/* Free all devices and its allocated memory */
	if(qmi_hash) {

		g_hash_table_destroy(qmi_hash);
		qmi_hash = NULL;
	}

}

CONNMAN_PLUGIN_DEFINE(qmi, "QMI plugin", CONNMAN_VERSION,
		CONNMAN_PLUGIN_PRIORITY_DEFAULT, qmi_init, qmi_exit)
