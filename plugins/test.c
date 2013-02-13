/*
 * test.c
 *
 *  Created on: 13.02.2013
 *      Author: roland
 */



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
	connman_device_set_powered(qmi->device, qmi->modem_online);



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



