/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2007-2012  Intel Corporation. All rights reserved.
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

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/vfs.h>
#include <sys/inotify.h>
#include <glib.h>

#include <connman/provision.h>
#include "connman.h"

struct connman_config_service {
	char *ident;
	char *name;
	char *type;
	void *ssid;
	char *apn;
	char *imsi;
	char *sim_nr;
	char *username;
	char *provider_name;
	unsigned int ssid_len;
	char *eap;
	char *identity;
	char *ca_cert_file;
	char *client_cert_file;
	char *private_key_file;
	char *private_key_passphrase;
	char *private_key_passphrase_type;
	char *phase2;
	char *passphrase;
	GSList *service_identifiers;
	char *config_ident; /* file prefix */
	char *config_entry; /* entry name */
	connman_bool_t hidden;
};

struct connman_config {
	char *ident;
	char *name;
	char *description;
	connman_bool_t protected;
	GHashTable *service_table;
};

static GHashTable *config_table = NULL;
static GSList *protected_services = NULL;

static connman_bool_t cleanup = FALSE;

#define INTERNAL_CONFIG_PREFIX           "__internal"

/* Definition of possible strings in the .config files */
#define CONFIG_KEY_NAME                "Name"
#define CONFIG_KEY_DESC                "Description"
#define CONFIG_KEY_PROT                "Protected"

#define SERVICE_KEY_TYPE               "Type"
#define SERVICE_KEY_NAME               "Name"
#define SERVICE_KEY_SSID               "SSID"
#define SERVICE_KEY_APN                "APN"
#define SERVICE_KEY_SIM                "SIM"
#define SERVICE_KEY_IMSI               "IMSI"
#define SERVICE_KEY_EAP                "EAP"
#define SERVICE_KEY_CA_CERT            "CACertFile"
#define SERVICE_KEY_CL_CERT            "ClientCertFile"
#define SERVICE_KEY_PRV_KEY            "PrivateKeyFile"
#define SERVICE_KEY_PRV_KEY_PASS       "PrivateKeyPassphrase"
#define SERVICE_KEY_PRV_KEY_PASS_TYPE  "PrivateKeyPassphraseType"
#define SERVICE_KEY_IDENTITY           "Identity"
#define SERVICE_KEY_PHASE2             "Phase2"
#define SERVICE_KEY_PASSPHRASE         "Passphrase"
#define SERVICE_KEY_USERNAME		   "Username"
#define SERVICE_KEY_PROVIDER		   "Provider"
#define SERVICE_KEY_HIDDEN             "Hidden"

static const char *config_possible_keys[] = {
	CONFIG_KEY_NAME,
	CONFIG_KEY_DESC,
	CONFIG_KEY_PROT,
	NULL,
};

static const char *service_possible_keys[] = {
	SERVICE_KEY_TYPE,
	SERVICE_KEY_NAME,
	SERVICE_KEY_SSID,
	SERVICE_KEY_APN,
	SERVICE_KEY_EAP,
	SERVICE_KEY_CA_CERT,
	SERVICE_KEY_CL_CERT,
	SERVICE_KEY_PRV_KEY,
	SERVICE_KEY_PRV_KEY_PASS,
	SERVICE_KEY_PRV_KEY_PASS_TYPE,
	SERVICE_KEY_IDENTITY,
	SERVICE_KEY_PHASE2,
	SERVICE_KEY_PASSPHRASE,
	SERVICE_KEY_IMSI,
	SERVICE_KEY_USERNAME,
	SERVICE_KEY_SIM,
	SERVICE_KEY_HIDDEN,
	SERVICE_KEY_PROVIDER,
	NULL,
};

static void unregister_config(gpointer data)
{
	struct connman_config *config = data;

	connman_info("Removing configuration %s", config->ident);

	g_hash_table_destroy(config->service_table);

	g_free(config->description);
	g_free(config->name);
	g_free(config->ident);
	g_free(config);
}

static void unregister_service(gpointer data)
{
	struct connman_config_service *config_service = data;
	struct connman_service *service;
	char *service_id;
	GSList *list;

	if (cleanup == TRUE)
		goto free_only;

	connman_info("Removing service configuration %s",
						config_service->ident);

	protected_services = g_slist_remove(protected_services,
						config_service);

	for (list = config_service->service_identifiers; list != NULL;
							list = list->next) {
		service_id = list->data;

		service = __connman_service_lookup_from_ident(service_id);
		if (service != NULL) {
			__connman_service_set_immutable(service, FALSE);
			__connman_service_remove(service);
		}

		if (__connman_storage_remove_service(service_id) == FALSE)
			DBG("Could not remove all files for service %s",
								service_id);
	}

free_only:
	g_free(config_service->ident);
	g_free(config_service->type);
	g_free(config_service->name);
	g_free(config_service->ssid);
	g_free(config_service->apn);
	g_free(config_service->sim_nr);
	g_free(config_service->imsi);
	g_free(config_service->username);
	g_free(config_service->eap);
	g_free(config_service->identity);
	g_free(config_service->ca_cert_file);
	g_free(config_service->client_cert_file);
	g_free(config_service->private_key_file);
	g_free(config_service->private_key_passphrase);
	g_free(config_service->private_key_passphrase_type);
	g_free(config_service->phase2);
	g_free(config_service->passphrase);
	g_free(config_service->provider_name);
	g_slist_free_full(config_service->service_identifiers, g_free);
	g_free(config_service->config_ident);
	g_free(config_service->config_entry);
	g_free(config_service);
}

static void check_keys(GKeyFile *keyfile, const char *group,
			const char **possible_keys)
{
	char **avail_keys;
	gsize nb_avail_keys, i, j;

	avail_keys = g_key_file_get_keys(keyfile, group, &nb_avail_keys, NULL);
	if (avail_keys == NULL)
		return;

	/*
	 * For each key in the configuration file,
	 * verify it is understood by connman
	 */
	for (i = 0 ; i < nb_avail_keys; i++) {
		for (j = 0; possible_keys[j] ; j++)
			if (g_strcmp0(avail_keys[i], possible_keys[j]) == 0)
				break;

		if (possible_keys[j] == NULL)
			connman_warn("Unknown configuration key %s in [%s]",
					avail_keys[i], group);
	}

	g_strfreev(avail_keys);
}

static connman_bool_t
is_protected_service(struct connman_config_service *service)
{
	GSList *list;

	DBG("ident %s", service->ident);

	for (list = protected_services; list; list = list->next) {
		struct connman_config_service *s = list->data;

		if (g_strcmp0(s->type, service->type) != 0)
			continue;

		if (s->ssid == NULL || service->ssid == NULL)
			continue;

		if (s->ssid_len != service->ssid_len)
			continue;

		if (g_strcmp0(service->type, "wifi") == 0 &&
			strncmp(s->ssid, service->ssid, s->ssid_len) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

static int load_service(GKeyFile *keyfile, const char *group,
						struct connman_config *config)
{
	struct connman_config_service *service;
	const char *ident;
	char *str, *hex_ssid;
	gboolean service_created = FALSE;
	int err;

	/* Strip off "service_" prefix */
	ident = group + 8;

	if (strlen(ident) < 1)
		return -EINVAL;

	/* Verify that provided keys are good */
	check_keys(keyfile, group, service_possible_keys);

	service = g_hash_table_lookup(config->service_table, ident);
	if (service == NULL) {
		service = g_try_new0(struct connman_config_service, 1);
		if (service == NULL)
			return -ENOMEM;

		service->ident = g_strdup(ident);

		service_created = TRUE;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_TYPE, NULL);
	if (str != NULL) {
		g_free(service->type);
		service->type = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_NAME, NULL);
	if (str != NULL) {
		g_free(service->name);
		service->name = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_APN, NULL);
	if (str != NULL) {
		g_free(service->apn);
		service->apn = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_SIM, NULL);
	if (str != NULL) {
		g_free(service->sim_nr);
		service->sim_nr = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_IMSI, NULL);
	if (str != NULL) {
		g_free(service->imsi);
		service->imsi = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_USERNAME, NULL);
	if (str != NULL) {
		g_free(service->username);
		service->username = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_PROVIDER, NULL);
	if (str != NULL) {
		g_free(service->provider_name);
		service->provider_name = str;
	}

	hex_ssid = g_key_file_get_string(keyfile, group, SERVICE_KEY_SSID,
					 NULL);
	if (hex_ssid != NULL) {
		char *ssid;
		unsigned int i, j = 0, hex;
		size_t hex_ssid_len = strlen(hex_ssid);

		ssid = g_try_malloc0(hex_ssid_len / 2);
		if (ssid == NULL) {
			err = -ENOMEM;
			g_free(hex_ssid);
			goto err;
		}

		for (i = 0; i < hex_ssid_len; i += 2) {
			if (sscanf(hex_ssid + i, "%02x", &hex) <= 0) {
				connman_warn("Invalid SSID %s", hex_ssid);
				g_free(ssid);
				g_free(hex_ssid);
				err = -EILSEQ;
				goto err;
			}
			ssid[j++] = hex;
		}

		g_free(hex_ssid);

		g_free(service->ssid);
		service->ssid = ssid;
		service->ssid_len = hex_ssid_len / 2;
	} else if (service->name != NULL) {
		char *ssid;
		unsigned int ssid_len;

		ssid_len = strlen(service->name);
		ssid = g_try_malloc0(ssid_len);
		if (ssid == NULL) {
			err = -ENOMEM;
			goto err;
		}

		memcpy(ssid, service->name, ssid_len);
		g_free(service->ssid);
		service->ssid = ssid;
		service->ssid_len = ssid_len;
	}

	if (is_protected_service(service) == TRUE) {
		connman_error("Trying to provision a protected service");
		err = -EACCES;
		goto err;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_EAP, NULL);
	if (str != NULL) {
		g_free(service->eap);
		service->eap = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_CA_CERT, NULL);
	if (str != NULL) {
		g_free(service->ca_cert_file);
		service->ca_cert_file = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_CL_CERT, NULL);
	if (str != NULL) {
		g_free(service->client_cert_file);
		service->client_cert_file = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_PRV_KEY, NULL);
	if (str != NULL) {
		g_free(service->private_key_file);
		service->private_key_file = str;
	}

	str = g_key_file_get_string(keyfile, group,
						SERVICE_KEY_PRV_KEY_PASS, NULL);
	if (str != NULL) {
		g_free(service->private_key_passphrase);
		service->private_key_passphrase = str;
	}

	str = g_key_file_get_string(keyfile, group,
					SERVICE_KEY_PRV_KEY_PASS_TYPE, NULL);
	if (str != NULL) {
		g_free(service->private_key_passphrase_type);
		service->private_key_passphrase_type = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_IDENTITY, NULL);
	if (str != NULL) {
		g_free(service->identity);
		service->identity = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_PHASE2, NULL);
	if (str != NULL) {
		g_free(service->phase2);
		service->phase2 = str;
	}

	str = g_key_file_get_string(keyfile, group, SERVICE_KEY_PASSPHRASE,
					NULL);
	if (str != NULL) {
		g_free(service->passphrase);
		service->passphrase = str;
	}

	service->config_ident = g_strdup(config->ident);
	service->config_entry = g_strdup_printf("service_%s", service->ident);

	service->hidden = g_key_file_get_boolean(keyfile, group,
						SERVICE_KEY_HIDDEN, NULL);

	if (service_created)
		g_hash_table_insert(config->service_table, service->ident,
					service);

	if (config->protected == TRUE)
		protected_services =
			g_slist_prepend(protected_services, service);

	connman_info("Adding service configuration %s", service->ident);

	return 0;

err:
	if (service_created == TRUE) {
		g_free(service->ident);
		g_free(service->type);
		g_free(service->name);
		g_free(service->ssid);
		g_free(service->apn);
		g_free(service->username);
		g_free(service->sim_nr);
		g_free(service->imsi);
		g_free(service->provider_name);
		g_free(service);
	}

	return err;
}

static int load_config(struct connman_config *config)
{
	GKeyFile *keyfile;
	GError *error = NULL;
	gsize length;
	char **groups;
	char *str;
	gboolean protected, found = FALSE;
	int i;

	DBG("config %p", config);

	keyfile = __connman_storage_load_config(config->ident);
	if (keyfile == NULL)
		return -EIO;

	/* Verify keys validity of the global section */
	check_keys(keyfile, "global", config_possible_keys);

	str = g_key_file_get_string(keyfile, "global", CONFIG_KEY_NAME, NULL);
	if (str != NULL) {
		g_free(config->name);
		config->name = str;
	}

	str = g_key_file_get_string(keyfile, "global", CONFIG_KEY_DESC, NULL);
	if (str != NULL) {
		g_free(config->description);
		config->description = str;
	}

	protected = g_key_file_get_boolean(keyfile, "global",
					CONFIG_KEY_PROT, &error);
	if (error == NULL)
		config->protected = protected;
	else
		config->protected = TRUE;
	g_clear_error(&error);

	groups = g_key_file_get_groups(keyfile, &length);

	for (i = 0; groups[i] != NULL; i++) {
		if (g_str_has_prefix(groups[i], "service_") == TRUE) {
			if (load_service(keyfile, groups[i], config) == 0)
				found = TRUE;
		}
	}

	if (found == FALSE)
		connman_warn("Config file %s/%s.config does not contain any "
			"configuration that can be provisioned!",
			STORAGEDIR, config->ident);

	g_strfreev(groups);

	g_key_file_free(keyfile);

	return 0;
}

static struct connman_config *create_config(const char *ident)
{
	struct connman_config *config;

	DBG("ident %s", ident);

	if (g_hash_table_lookup(config_table, ident) != NULL)
		return NULL;

	config = g_try_new0(struct connman_config, 1);
	if (config == NULL)
		return NULL;

	config->ident = g_strdup(ident);

	config->service_table = g_hash_table_new_full(g_str_hash, g_str_equal,
						NULL, unregister_service);

	g_hash_table_insert(config_table, config->ident, config);

	connman_info("Adding configuration %s", config->ident);

	return config;
}

static connman_bool_t validate_ident(const char *ident)
{
	unsigned int i;

	if (ident == NULL)
		return FALSE;

	for (i = 0; i < strlen(ident); i++)
		if (g_ascii_isprint(ident[i]) == FALSE)
			return FALSE;

	return TRUE;
}

static int read_configs(void)
{
	GDir *dir;

	DBG("");

	dir = g_dir_open(STORAGEDIR, 0, NULL);
	if (dir != NULL) {
		const gchar *file;

		while ((file = g_dir_read_name(dir)) != NULL) {
			GString *str;
			gchar *ident;

			if (g_str_has_suffix(file, ".config") == FALSE)
				continue;

			ident = g_strrstr(file, ".config");
			if (ident == NULL)
				continue;

			str = g_string_new_len(file, ident - file);
			if (str == NULL)
				continue;

			ident = g_string_free(str, FALSE);

			if (validate_ident(ident) == TRUE) {
				struct connman_config *config;

				config = create_config(ident);
				if (config != NULL)
					load_config(config);
			} else {
				connman_error("Invalid config ident %s", ident);
			}
			g_free(ident);
		}

		g_dir_close(dir);
	}

	return 0;
}

static void config_notify_handler(struct inotify_event *event,
                                        const char *ident)
{
	char *ext;

	if (ident == NULL)
		return;

	if (g_str_has_suffix(ident, ".config") == FALSE)
		return;

	ext = g_strrstr(ident, ".config");
	if (ext == NULL)
		return;

	*ext = '\0';

	if (validate_ident(ident) == FALSE) {
		connman_error("Invalid config ident %s", ident);
		return;
	}

	if (event->mask & IN_CREATE)
		create_config(ident);

	if (event->mask & IN_MODIFY) {
		struct connman_config *config;

		config = g_hash_table_lookup(config_table, ident);
		if (config != NULL) {
			int ret;

			g_hash_table_remove_all(config->service_table);
			load_config(config);
			ret = __connman_service_provision_changed(ident);
			if (ret > 0) {
				/*
				 * Re-scan the config file for any
				 * changes
				 */
				g_hash_table_remove_all(config->service_table);
				load_config(config);
				__connman_service_provision_changed(ident);
			}
		}
	}

	if (event->mask & IN_DELETE)
		g_hash_table_remove(config_table, ident);
}

int __connman_config_init(void)
{
	DBG("");

	config_table = g_hash_table_new_full(g_str_hash, g_str_equal,
						NULL, unregister_config);

	connman_inotify_register(STORAGEDIR, config_notify_handler);

	return read_configs();
}

void __connman_config_cleanup(void)
{
	DBG("");

	cleanup = TRUE;

	connman_inotify_unregister(STORAGEDIR, config_notify_handler);

	g_hash_table_destroy(config_table);
	config_table = NULL;

	cleanup = FALSE;
}

static char *config_pem_fsid(const char *pem_file)
{
	struct statfs buf;
	unsigned *fsid = (unsigned *) &buf.f_fsid;
	unsigned long long fsid64;

	if (pem_file == NULL)
		return NULL;

	if (statfs(pem_file, &buf) < 0) {
		connman_error("statfs error %s for %s",
						strerror(errno), pem_file);
		return NULL;
	}

	fsid64 = ((unsigned long long) fsid[0] << 32) | fsid[1];

	return g_strdup_printf("%llx", fsid64);
}

static void provision_service(gpointer key, gpointer value, gpointer user_data)
{
	struct connman_service *service = user_data;
	struct connman_config_service *config = value;
	struct connman_network *network;
	const void *ssid, *service_id;
	unsigned int ssid_len;

	if((g_strcmp0(config->type, "wifi") != 0) &&
			(g_strcmp0(config->type, "qmi") != 0)) {

		return;
	}

			network = __connman_service_get_network(service);
			if (network == NULL) {
				connman_error("Service has no network set");
				return;
			}

			if(g_strcmp0(config->type, "wifi") == 0) {

				ssid = connman_network_get_blob(network, "WiFi.SSID", &ssid_len);
				if (ssid == NULL) {
					connman_error("Network SSID not set");
					return;
				}

				if (config->ssid == NULL || ssid_len != config->ssid_len)
					return;

				if (memcmp(config->ssid, ssid, ssid_len) != 0)
					return;

			}

			if((g_strcmp0(config->type, "qmi") == 0)) {

				const char *group = NULL, *imsi = NULL;
				GString *str;

				if(connman_network_get_type(network) != CONNMAN_NETWORK_TYPE_QMI)
					return;

				group = connman_network_get_group(network);
				if(group == NULL) {

					connman_error("Network IMSI not set");
					return;
				}

				if(g_str_has_suffix(group, "_none") == FALSE) {

					connman_error("Network IMSI not valid");
					return;
				}

				imsi = g_strrstr(group, "_none");
				if(imsi == NULL) {

					connman_error("Network IMSI not valid");
					return;
				}

				str = g_string_new_len(group, imsi - group);
				if(str == NULL) {

					connman_error("Network IMSI not given");
					return;
				}

				imsi = g_string_free(str, FALSE);

				if((g_strcmp0(config->imsi, imsi) != 0)) {

					connman_error("No valid IMSI found");
					return;
				}

			}

			service_id = __connman_service_get_ident(service);
			config->service_identifiers =
				g_slist_prepend(config->service_identifiers,
						g_strdup(service_id));

			__connman_service_set_immutable(service, TRUE);

			__connman_service_set_favorite_delayed(service, TRUE, TRUE);

			__connman_service_set_config(service, config->config_ident,
								config->config_entry);

			if (config->eap != NULL)
				__connman_service_set_string(service, "EAP", config->eap);

			if (config->identity != NULL)
				__connman_service_set_string(service, "Identity",
									config->identity);

			if (config->ca_cert_file != NULL)
				__connman_service_set_string(service, "CACertFile",
									config->ca_cert_file);

			if (config->client_cert_file != NULL)
				__connman_service_set_string(service, "ClientCertFile",
								config->client_cert_file);

			if (config->private_key_file != NULL)
				__connman_service_set_string(service, "PrivateKeyFile",
								config->private_key_file);

			if (g_strcmp0(config->private_key_passphrase_type, "fsid") == 0 &&
							config->private_key_file != NULL) {
				char *fsid;

				fsid = config_pem_fsid(config->private_key_file);
				if (fsid == NULL)
					return;

				g_free(config->private_key_passphrase);
				config->private_key_passphrase = fsid;
			}

			if (config->private_key_passphrase != NULL) {
				__connman_service_set_string(service, "PrivateKeyPassphrase",
								config->private_key_passphrase);
				/*
				 * TODO: Support for PEAP with both identity and key passwd.
				 * In that case, we should check if both of them are found
				 * from the config file. If not, we should not set the
				 * service passphrase in order for the UI to request for an
				 * additional passphrase.
				 */
			}

			if (config->phase2 != NULL)
				__connman_service_set_string(service, "Phase2", config->phase2);

			if (config->passphrase != NULL)
				__connman_service_set_string(service, "Passphrase", config->passphrase);

			if (config->apn != NULL)
					__connman_service_set_string(service, "APN", config->apn);

			if (config->sim_nr != NULL)
					__connman_service_set_string(service, "SIM", config->sim_nr);

			if (config->imsi != NULL)
					__connman_service_set_string(service, "IMSI", config->imsi);

			if (config->username != NULL)
					__connman_service_set_string(service, "Username", config->username);

			if (config->provider_name != NULL)
					__connman_service_set_string(service, "Provider", config->provider_name);

			if (config->hidden == TRUE)
				__connman_service_set_hidden(service);

			__connman_service_mark_dirty();

			__connman_service_save(service);

}

int __connman_config_provision_service(struct connman_service *service)
{
	enum connman_service_type type;
	GHashTableIter iter;
	gpointer value, key;

	DBG("service %p", service);

	type = connman_service_get_type(service);
	switch(type) {

		case CONNMAN_SERVICE_TYPE_QMI:
		case CONNMAN_SERVICE_TYPE_WIFI:

			g_hash_table_iter_init(&iter, config_table);

			while (g_hash_table_iter_next(&iter, &key, &value) == TRUE) {
				struct connman_config *config = value;

				g_hash_table_foreach(config->service_table,
								provision_service, service);
			}
			break;

		default:
			return -ENOSYS;

	}

	return 0;
}

int __connman_config_provision_service_ident(struct connman_service *service,
			const char *ident, const char *file, const char *entry)
{
	struct connman_config *config;
	enum connman_service_type type;
	int ret = 0;

	DBG("service %p", service);
	type = connman_service_get_type(service);
	if((type != CONNMAN_SERVICE_TYPE_WIFI) &&
			(type != CONNMAN_SERVICE_TYPE_QMI)) {

		return -ENOSYS;
	}

	config = g_hash_table_lookup(config_table, ident);
	if (config != NULL) {
		GHashTableIter iter;
		gpointer value, key;
		gboolean found = FALSE;

		g_hash_table_iter_init(&iter, config->service_table);

		/*
		 * Check if we need to remove individual service if it
		 * is missing from config file.
		 */
		if (file != NULL && entry != NULL) {
			while (g_hash_table_iter_next(&iter, &key,
							&value) == TRUE) {
				struct connman_config_service *config_service;

				config_service = value;

				if (g_strcmp0(config_service->config_ident,
								file) != 0)
					continue;

				if (g_strcmp0(config_service->config_entry,
								entry) != 0)
					continue;

				found = TRUE;
				break;
			}

			DBG("found %d ident %s file %s entry %s", found, ident,
								file, entry);

			if (found == FALSE) {
				/*
				 * The entry+8 will skip "service_" prefix
				 */
				g_hash_table_remove(config->service_table,
						entry + 8);
				ret = 1;
			}
		}

		g_hash_table_foreach(config->service_table,
						provision_service, service);
	}

	return ret;
}

struct connman_config_entry **connman_config_get_entries(void)
{
	GHashTableIter iter_file, iter_config;
	gpointer value, key;
	struct connman_config_entry **entries = NULL;
	int i = 0, count;

	g_hash_table_iter_init(&iter_file, config_table);
	while (g_hash_table_iter_next(&iter_file, &key, &value) == TRUE) {
		struct connman_config *config_file = value;

		count = g_hash_table_size(config_file->service_table);

		entries = g_try_realloc(entries, (i + count + 1) *
					sizeof(struct connman_config_entry *));
		if (entries == NULL)
			return NULL;

		g_hash_table_iter_init(&iter_config,
						config_file->service_table);
		while (g_hash_table_iter_next(&iter_config, &key,
							&value) == TRUE) {
			struct connman_config_service *config = value;

			entries[i] = g_try_new0(struct connman_config_entry,
						1);
			if (entries[i] == NULL)
				goto cleanup;

			entries[i]->ident = g_strdup(config->ident);
			entries[i]->name = g_strdup(config->name);
			entries[i]->ssid = g_try_malloc0(config->ssid_len + 1);
			if (entries[i]->ssid == NULL)
				goto cleanup;

			memcpy(entries[i]->ssid, config->ssid,
							config->ssid_len);
			entries[i]->ssid_len = config->ssid_len;
			entries[i]->hidden = config->hidden;

			i++;
		}
	}

	if (entries != NULL) {
		entries = g_try_realloc(entries, (i + 1) *
					sizeof(struct connman_config_entry *));
		if (entries == NULL)
			return NULL;

		entries[i] = NULL;

		DBG("%d provisioned AP found", i);
	}

	return entries;

cleanup:
	connman_config_free_entries(entries);
	return NULL;
}

void connman_config_free_entries(struct connman_config_entry **entries)
{
	int i;

	if (entries == NULL)
		return;

	for (i = 0; entries[i]; i++) {
		g_free(entries[i]->ident);
		g_free(entries[i]->name);
		g_free(entries[i]->ssid);
		g_free(entries[i]);
	}

	g_free(entries);
	return;
}
