#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "client.h"
#include "config.h"
#include "interface.h"
#include "list.h"
#include "log.h"
#include "mac.h"
#include "rate.h"

void
wrl_config_init(struct wrl_config *config)
{
	INIT_LIST_HEAD(&config->interfaces);
	INIT_LIST_HEAD(&config->clients);
}


/* Interface config */
static void
vrl_config_interface_free(struct wrl_config_interface *interface)
{
	list_del(&interface->head);
	free(interface);
}


struct wrl_config_interface *
wrl_config_interface_get(struct wrl_config *config, struct wrl_config_interface_selectors *selectors, int *create)
{
	struct wrl_config_interface *interface = NULL;
	struct wrl_config_interface *wildcard = NULL;

	list_for_each_entry(interface, &config->interfaces, head) {
		/* ToDo: Only interface supported for now */
		if (strncmp(interface->selectors.interface, selectors->interface, sizeof(selectors->interface)) == 0) {
			return interface;
		}

		if (interface->selectors.interface[0] == 0) {
			wildcard = interface;
		}
	}

	if (wildcard) {
		return wildcard;
	}

	if (!create) {
		return NULL;
	}

	interface = calloc(1, sizeof(*interface));
	if (!interface) {
		return NULL;
	}

	memcpy(&interface->selectors, selectors, sizeof(*selectors));
	INIT_LIST_HEAD(&interface->head);
	list_add_tail(&interface->head, &config->interfaces);
	*create = 1;

	return interface;
}


void
wrl_config_interface_purge(struct wrl_config *config)
{
	struct wrl_config_interface *interface, *tmp;

	list_for_each_entry_safe(interface, tmp, &config->interfaces, head) {
		vrl_config_interface_free(interface);
	}
}


/* Client config */
static void
wrl_config_client_free(struct wrl_config_client *client)
{
	list_del(&client->head);
	free(client);
}


struct wrl_config_client *
wrl_config_client_get(struct wrl_config *config, struct wrl_config_client_selectors *selectors, int *create)
{
	struct wrl_config_client *client;
	struct wrl_config_client *wildcard = NULL;

	list_for_each_entry(client, &config->clients, head) {
		/* ToDo: Only interface supported for now */
		if (strncmp(client->selectors.interface, selectors->interface, sizeof(selectors->interface)) == 0) {
			return client;
		}

		if (client->selectors.interface[0] == 0) {
			wildcard = client;
		}
	}

	if (wildcard)
		return wildcard;

	if (!create) {
		return NULL;
	}

	client = calloc(1, sizeof(*client));
	if (!client) {
		return NULL;
	}

	memcpy(&client->selectors, selectors, sizeof(*selectors));
	INIT_LIST_HEAD(&client->head);
	list_add_tail(&client->head, &config->clients);
	*create = 1;

	return client;
}


void
wrl_config_client_purge(struct wrl_config *config)
{
	struct wrl_config_client *client, *tmp;

	list_for_each_entry_safe(client, tmp, &config->clients, head) {
		wrl_config_client_free(client);
	}
}

/* State update methods */
int
wrl_config_interface_update(struct wrl_config *config, struct wrl_interface *interface)
{
	struct wrl_config_interface_selectors selectors = {};
	struct wrl_config_interface *config_interface;
	int tx_rate, rx_rate;

	/* ToDo: Only interface supported for now */
	strncpy(selectors.interface, interface->name, sizeof(selectors.interface));

	config_interface = wrl_config_interface_get(config, &selectors, NULL);
	if (!config_interface) {
		rx_rate = 0;
		tx_rate = 0;
	} else {
		rx_rate = config_interface->rate.down;
		tx_rate = config_interface->rate.up;
	}

	if (rx_rate != interface->rate.down || tx_rate != interface->rate.up) {
		interface->rate.down = rx_rate;
		interface->rate.up = tx_rate;
		interface->rate.applied = 0;
	}

	return !interface->rate.applied;
}

int
wrl_config_client_update(struct wrl_config *config, struct wrl_interface *interface, struct wrl_client *client)
{
	struct wrl_config_client_selectors selectors = {};
	struct wrl_config_client *config_client;
	int tx_rate, rx_rate;

	/* ToDo: Only interface supported for now */
	strncpy(selectors.interface, interface->name, sizeof(selectors.interface));

	config_client = wrl_config_client_get(config, &selectors, NULL);
	if (!config_client) {
		rx_rate = 0;
		tx_rate = 0;
	} else {
		rx_rate = config_client->rate.down;
		tx_rate = config_client->rate.up;
	}

	if (rx_rate != client->rate.down || tx_rate != client->rate.up) {
		client->rate.down = rx_rate;
		client->rate.up = tx_rate;
		client->rate.applied = 0;
	}

	return !client->rate.applied;
}
