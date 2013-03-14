/*
 * mk3.c
 *
 *  Created on: 13.03.2013
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



struct mk3_data {

	gint index;
	gchar *name;
	gchar *group;
	gchar *devname;
	guint8 strength;
	struct connman_device *device;
	struct connman_network *network;
};

static void delete_network(struct mk3_data *mk3) {

	g_return_if_fail(mk3);

	DBG("%s", mk3->devname);

	if(mk3->network == NULL)
		return;

	DBG("network %p", mk3->network);

	connman_device_remove_network(mk3->device, mk3->network);
	mk3->network = NULL;

}

static void add_network(struct mk3_data *mk3) {

	struct connman_network *network = NULL;

	g_return_if_fail(mk3);

	DBG("data %p", mk3);


	if(mk3->network != NULL) {

		DBG("network %p already exists.", mk3->network);
		return;
	}

	network = connman_network_create(mk3->devname,	CONNMAN_NETWORK_TYPE_MK3);
	if(network == NULL) {

		connman_error("Network could not be created.");
		return;

	}

	DBG("network %p", mk3->network);

	connman_network_set_index(network, mk3->index);
	connman_network_set_name(network, mk3->name);
	connman_network_set_data(network, mk3);
	connman_network_set_strength(network, mk3->strength);
	connman_network_set_group(network, mk3->group);

	if(connman_device_add_network(mk3->device, network) < 0) {

		connman_network_unref(network);
		network = NULL;
		return;
	}

	mk3->network = network;


}

static int network_probe(struct connman_network *network) {

	struct mk3_data *mk3 = NULL;

	DBG("network %p", network);

	g_return_val_if_fail(network, -ENODEV);

	mk3 = connman_network_get_data(network);
	if(!mk3) {

		connman_error("No device data available.");
		return -ENODEV;
	}

	return 0;

}

static void network_remove(struct connman_network *network) {

	struct mk3_data *mk3 = NULL;

	DBG("network %p", network);

	g_return_if_fail(network);

	mk3 = connman_network_get_data(network);
	if(!mk3) {

		connman_error("No device data available.");
		return;
	}

	DBG("network %p data %p", network, mk3);

	delete_network(mk3);
}

static int network_connect(struct connman_network *network) {

	struct mk3_data *mk3 = NULL;


	DBG("Network %p", network);

	mk3 = connman_network_get_data(network);
	if(!mk3) {

		connman_error("Could not get device data.");
		return -ENODEV;
	}

	DBG("Network %p %p", network, mk3);

	return 0;

}

static int network_disconnect(struct connman_network *network) {

	struct mk3_data *mk3 = connman_network_get_data(network);

	DBG("Network %p %p", network, mk3);

	return 0;
}

static struct connman_network_driver network_driver = {

	.name		= "mk3",
	.type		= CONNMAN_NETWORK_TYPE_MK3,
	.probe		= network_probe,
	.remove		= network_remove,
	.connect	= network_connect,
	.disconnect	= network_disconnect,
};


static int mk3_probe(struct connman_device *device) {

	struct mk3_data *mk3;

	DBG("device %p", device);

	g_return_val_if_fail(device, -ENODEV);

	mk3 = g_try_new0(struct mk3_data, 1);
	if(mk3 == NULL) {

		connman_error("Allocation error, no memory available.");
		return -ENOMEM;
	}

	DBG("device %p data %p", device, mk3);

	connman_device_set_data(device, mk3);
	mk3->device = connman_device_ref(device);

	/* Index of the specific QMI-Device */
	mk3->index = connman_device_get_index(device);
	/* Name of the specific Device  */
	mk3->devname = g_strdup(connman_device_get_string(device, "Interface"));
	mk3->group = g_strdup("Car2Car");
	mk3->name = g_strdup("Car2Car");


	return 0;
}

static void mk3_remove(struct connman_device *device) {

	struct mk3_data *mk3 = NULL;

	DBG("device %p", device);

	g_return_if_fail(device);

	mk3 = connman_device_get_data(device);
	if(!mk3) {

		connman_error("Could not get device data");
		return;
	}

	DBG("device %p data %p", device, mk3);

	delete_network(mk3);

	connman_device_set_data(device, NULL);
	connman_device_unref(mk3->device);

	g_free(mk3->devname);
	g_free(mk3->group);
	g_free(mk3->name);

	g_free(mk3);

}

static int mk3_enable(struct connman_device *device) {

	int err = 0;
	struct mk3_data *mk3 = NULL;

	DBG("device %p", device);

	g_return_val_if_fail(device, -ENODEV);

	mk3 = connman_device_get_data(device);
	if(!mk3) {

		connman_error("No device data available");
		return -ENODEV;
	}

	DBG("device %p data %p", device, mk3);

	err = connman_inet_ifup(mk3->index);
	if(err < 0) {

		connman_error("QMI device could not getting up with ifup");
		return err;
	}

	add_network(mk3);

	return 0;
}

static int mk3_disable(struct connman_device *device) {

	int err = 0;
	struct mk3_data *mk3 = NULL;

	DBG("device %p", device);

	g_return_val_if_fail(device, -ENODEV);

	mk3 = connman_device_get_data(device);
	if(!mk3) {

		connman_error("Could not get device data");
		return -ENODEV;
	}

	DBG("device %p data %p", device, mk3);
	err = connman_inet_ifdown(mk3->index);
	if(err < 0) {

		connman_error("QMI device could not getting down with ifdown");
		return err;
	}

	delete_network(mk3);

	return 0;
}


static struct connman_device_driver mk3_driver = {

	.name		= "mk3",
	.type		= CONNMAN_DEVICE_TYPE_MK3,
	.probe		= mk3_probe,
	.remove		= mk3_remove,
	.enable		= mk3_enable,
	.disable	= mk3_disable,
};



static int tech_probe(struct connman_technology *technology) {

	return 0;
}

static void tech_remove(struct connman_technology *technology) {

}

static struct connman_technology_driver tech_driver = {

	.name	= "mk3",
	.type	= CONNMAN_SERVICE_TYPE_MK3,
	.probe	= tech_probe,
	.remove	= tech_remove,
};


static int
mk3_init(void) {

	int err;

	DBG("");


	err = connman_network_driver_register(&network_driver);
	if(err < 0) {

		connman_error("Register network driver");
		return err;
	}

	err = connman_device_driver_register(&mk3_driver);
	if(err < 0) {

		connman_error("Register device driver");
		connman_network_driver_unregister(&network_driver);
		return err;
	}

	err = connman_technology_driver_register(&tech_driver);
	if(err < 0) {

		connman_error("Register technology driver");
		connman_network_driver_unregister(&network_driver);
		connman_device_driver_unregister(&mk3_driver);

		return err;
	}

	return 0;
}

static void
mk3_exit(void) {

	DBG("");

	connman_device_driver_unregister(&mk3_driver);
	connman_network_driver_unregister(&network_driver);
	connman_technology_driver_register(&tech_driver);


}

CONNMAN_PLUGIN_DEFINE(mk3, "MK3 plugin", CONNMAN_VERSION,
CONNMAN_PLUGIN_PRIORITY_DEFAULT, mk3_init, mk3_exit)
