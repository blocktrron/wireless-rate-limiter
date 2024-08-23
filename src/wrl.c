/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2024 David Bauer <mail@david-bauer.net> */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <libubox/uloop.h>

#include "interface.h"
#include "log.h"
#include "mac.h"
#include "wrl.h"

#define WRL_RECURRING_WORK_INTERVAL 1000
#define WRL_UBUS_HOSTAPD_PATH "hostapd."

static struct blob_buf b;

struct wrl_request_clients_priv {
	struct wrl_data *wrl;
	struct wrl_interface *wrl_iface;
};


static int
wrl_execute_command(const char *command)
{
	MSG(DEBUG, "Executing command: %s\n", command);
	return system(command);
}

static struct wrl_client *
wrl_client_get(struct wrl_interface *wrl_iface, uint8_t *mac, uint8_t *allocate)
{
	struct wrl_client *client, *free_client = NULL;

	for (int i = 0; i < 256; i++) {
		client = &wrl_iface->clients[i];

		if (memcmp(client->address, mac, 6) == 0) {
			return client;
		}

		if (wrl_mac_is_zero(client->address) && allocate && !free_client) {
			free_client = client;
			free_client->id = i;
		}
	}

	if (!allocate) {
		MSG(ERROR, "Client not found\n");
		return NULL;
	}

	if (!free_client) {
		MSG(ERROR, "No free client found\n");
		if (allocate)
			*allocate = 0;
		return NULL;
	}

	MSG(DEBUG, "Allocating new client\n");
	*allocate = 1;
	memcpy(free_client->address, mac, 6);
	return free_client;
}

static void
wrl_ubus_get_clients_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
	struct wrl_request_clients_priv *priv = req->priv;
	struct wrl_interface *wrl_iface;
	struct wrl_data *wrl;
	struct wrl_client *client;
	const char *mac_string;
	uint8_t mac[6];
	uint8_t allocate = 0;

	struct blob_attr *cur;
	int remaining;
	int i;

	enum {
		MSG_CLIENTS,
		__MSG_MAX,
	};
	static struct blobmsg_policy policy[__MSG_MAX] = {
		[MSG_CLIENTS] = { "clients", BLOBMSG_TYPE_TABLE },
	};
	struct blob_attr *tb[__MSG_MAX];

	wrl = priv->wrl;
	wrl_iface = priv->wrl_iface;

	MSG(DEBUG, "Received list of clients for Interface %s\n", wrl_iface->name);

	blobmsg_parse(policy, __MSG_MAX, tb, blob_data(msg), blob_len(msg));

	if (!tb[MSG_CLIENTS]) {
		MSG(ERROR, "No clients found\n");
		return;
	}

	/* Mark all clients as gone */
	for (i = 0; i < 256; i++) {
		wrl_iface->clients[i].connected = 0;
	}

	blobmsg_for_each_attr(cur, tb[MSG_CLIENTS], remaining) {
		mac_string = blobmsg_name(cur);
		MSG(DEBUG, "Client mac=%s interface=%s\n", mac_string, wrl_iface->name);
		if (wrl_mac_from_string(mac_string, mac) == NULL) {
			MSG(ERROR, "Failed to parse MAC address\n");
			continue;
		}

		/* Get Client */
		client = wrl_client_get(wrl_iface, mac, &allocate);
		if (!client) {
			MSG(ERROR, "Failed to get client\n");
			continue;
		}

		/* Update policy */
		if (wrl_config_client_update(&wrl->config, wrl_iface, client)) {
			MSG(INFO, "Update rate-limits for client %02x:%02x:%02x:%02x:%02x:%02x rx=%d tx=%d\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], client->rate.down, client->rate.up);
		}

		if (allocate) {
			MSG(INFO, "New client, scheudling rate update\n");
			client->rate.applied = 0;
		}

		MSG(DEBUG, "Client %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		client->connected = 1;
	}

	/* Zero all clients that are not connected */
	for (i = 0; i < 256; i++) {
		client = &wrl_iface->clients[i];
		if (client->connected)
			continue;
		client = &wrl_iface->clients[i];
		memset(client, 0, sizeof(struct wrl_client));
	}
}

static void
wrl_ubus_interfaces_update_cb(struct ubus_context *ctx, struct ubus_object_data *obj, void *priv)
{
	struct wrl_data *wrl = priv;

	const char *path = obj->path;
	uint32_t id = obj->id;

	struct wrl_interface *interface;
	uint8_t found;

	found = 0;

	MSG(DEBUG, "Interface %s available on ubus\n", path);

	list_for_each_entry(interface, &wrl->interfaces, head) {
		if (strcmp(interface->name, path + strlen(WRL_UBUS_HOSTAPD_PATH)) == 0) {
			if (interface->ubus.id != id) {
				/* Delete interface */
				MSG(INFO, "Interface %s changed ID from %d to %d\n", interface->name, interface->ubus.id, id);
				memset(interface, 0, sizeof(struct wrl_interface));
				break;
			}
			found = 1;
			break;
		}
	}

	if (!found) {
		MSG(INFO, "New interface %s found\n", path + strlen(WRL_UBUS_HOSTAPD_PATH));
		interface = calloc(1, sizeof(struct wrl_interface));
		if (!interface) {
			MSG(ERROR, "Failed to allocate memory for new interface\n");
			return;
		}

		INIT_LIST_HEAD(&interface->head);

		/* Update metdata from ubus */
		strncpy(interface->name, path + strlen(WRL_UBUS_HOSTAPD_PATH), sizeof(interface->name));

		list_add_tail(&interface->head, &wrl->interfaces);
	}

	interface->ubus.id = id;
	interface->missing = 0;
}


static void
wrl_ubus_interfaces_update(struct wrl_data *wrl)
{
	struct wrl_interface *interface, *tmp;
	struct wrl_request_clients_priv priv = {
		.wrl = wrl,
	};

	list_for_each_entry_safe(interface, tmp, &wrl->interfaces, head) {
		interface->missing++;

		if (interface->missing >= 3) {
			MSG(INFO, "Interface %s missing, removing\n", interface->name);
			list_del_init(&interface->head);
			free(interface);
		}
	}

	ubus_lookup(&wrl->ubus.ctx, "hostapd.*", wrl_ubus_interfaces_update_cb, wrl);

	blob_buf_init(&b, 0);

	/* Update interface */
	list_for_each_entry(interface, &wrl->interfaces, head) {
		/* Update interface rate-limits */
		if (wrl_config_interface_update(&wrl->config, interface)) {
			MSG(INFO, "Update rate-limits for interface %s rx=%d tx=%d\n", interface->name, interface->rate.down, interface->rate.up);
			interface->rate.applied = 0;
		}

		/* Request Clients */
		MSG(DEBUG, "Requesting clients for interface %s\n", interface->name);
		if (interface->ubus.req_pending) {
			MSG(DEBUG, "Request already pending for interface %s\n", interface->name);
			continue;
		}

		priv.wrl_iface = interface;
		ubus_invoke(&wrl->ubus.ctx, interface->ubus.id, "get_clients", b.head, wrl_ubus_get_clients_cb, &priv, 1000);
		interface->ubus.req_pending = 0;
	}
}

static int
wrl_ubus_init(struct wrl_data *wrl)
{
	int ret;

	ret = ubus_connect_ctx(&wrl->ubus.ctx, NULL);
	if (ret) {
		fprintf(stderr, "Failed to connect to ubus: %s\n", ubus_strerror(ret));
		return 1;
	}

	return 0;
}

static void
wrl_rate_apply_interface(struct wrl_interface *interface)
{
	char command_buffer[512];
	int tx_rate, rx_rate;

	tx_rate = interface->rate.up;
	rx_rate = interface->rate.down;

	if (tx_rate == 0)
		tx_rate = 1 * 1024 * 1024 * 1024;
	
	if (rx_rate == 0)
		rx_rate = 1 * 1024 * 1024 * 1024;

	snprintf(command_buffer, sizeof(command_buffer), "sh /lib/wireless-rate-limiter/htb-netdev.sh add %s %dkbit %dkbit", interface->name, rx_rate, tx_rate);
	wrl_execute_command(command_buffer);
}

static void
wrl_rate_apply_client(struct wrl_interface *interface, struct wrl_client *client, int client_id)
{
	char command_buffer[512];
	char mac_string[18];
	int tx_rate, rx_rate;

	client_id = client->id + 10;

	/* Check if we should remove the rate limit */
	if (client->rate.down == 0 && client->rate.up == 0) {
		snprintf(command_buffer, sizeof(command_buffer), "sh /lib/wireless-rate-limiter/htb-client.sh remove %d %s", client_id, interface->name);
		wrl_execute_command(command_buffer);
		return;
	}

	/* Add rate limit */
	tx_rate = client->rate.up;
	rx_rate = client->rate.down;

	if (tx_rate == 0)
		tx_rate = 1 * 1024 * 1024 * 1024;
	if (rx_rate == 0)
		rx_rate = 1 * 1024 * 1024 * 1024;

	wrl_mac_to_string(client->address, mac_string);

	snprintf(command_buffer, sizeof(command_buffer), "sh /lib/wireless-rate-limiter/htb-client.sh add %d %s %s %dkbit %dkbit", client_id, interface->name, mac_string, rx_rate, tx_rate);
	wrl_execute_command(command_buffer);
}

static void
wrl_rate_apply(struct wrl_data *wrl)
{
	struct wrl_interface *interface;
	struct wrl_client *client;
	int i;

	list_for_each_entry(interface, &wrl->interfaces, head) {
		/* Apply interface rates */
		if (!interface->rate.applied) {
			MSG(INFO, "Applying rate for interface %s\n", interface->name);
			wrl_rate_apply_interface(interface);
		}

		/* Apply client rates */
		for (i = 0; i < 256; i++) {
			client = &interface->clients[i];
			if (wrl_mac_is_zero(client->address))
				continue;
			
			if (interface->rate.applied && client->rate.applied)
				continue;

			MSG(INFO, "Applying rate for client %02x:%02x:%02x:%02x:%02x:%02x\n", client->address[0], client->address[1], client->address[2], client->address[3], client->address[4], client->address[5]);
			
			wrl_rate_apply_client(interface, client, i);
			client->rate.applied = 1;
		}

		interface->rate.applied = 1;
	}
}

static void
wrl_recurring_work_timeout(struct uloop_timeout *timeout)
{
	struct wrl_data *wrl = container_of(timeout, struct wrl_data, recurring);

	MSG(DEBUG, "Recurring work\n");

	/* Update interface information */
	wrl_ubus_interfaces_update(wrl);

	/* Apply client rate settings */
	wrl_rate_apply(wrl);

	uloop_timeout_set(&wrl->recurring, WRL_RECURRING_WORK_INTERVAL);
}

static void
wrl_demo_config(struct wrl_data *wrl)
{
	struct wrl_config_interface_selectors iface_selectors_list[] = {
		{ .interface = "owe0" },
		{ .interface = "owe1" },
		{ .interface = "client0" },
		{ .interface = "client1" },
		{}
	};
	struct wrl_config_interface_selectors *iface_selectors;

	struct wrl_config_client_selectors client_selectors_list[] = {
		{ .interface = "owe0" },
		{ .interface = "owe1" },
		{ .interface = "client0" },
		{ .interface = "client1" },
		{}
	};
	struct wrl_config_client_selectors *client_selectors;

	struct wrl_config_interface *iface;
	struct wrl_config_client *client;
	int create;
	int i;
	int num_rules = 4;

	/* Interfaces */
	for (i = 0; i < num_rules; i++) {
		iface_selectors = &iface_selectors_list[i];
		iface = wrl_config_interface_get(&wrl->config, iface_selectors, &create);
		if (!iface) {
			MSG(ERROR, "Failed to get interface\n");
			return;
		}

		iface->rate.down = 1024 * 20;
		iface->rate.up = 1024 * 10;
	}

	/* Clients */
	for (i = 0; i < num_rules; i++) {
		client_selectors = &client_selectors_list[i];
		client = wrl_config_client_get(&wrl->config, client_selectors, &create);
		if (!client) {
			MSG(ERROR, "Failed to get client\n");
			return;
		}

		client->rate.down = 1024 * 8;
		client->rate.up = 1024 * 3;
	}
}

int
main(int argc, char *argv[])
{
	struct wrl_data wrl = {0};

	INIT_LIST_HEAD(&wrl.interfaces);
	wrl_config_init(&wrl.config);

	/* Load demo config */
	wrl_demo_config(&wrl);

	uloop_init();

	/* ubus */
	if (wrl_ubus_init(&wrl)) {
		MSG(ERROR, "Failed to initialize ubus\n");
		return 1;
	}
	ubus_add_uloop(&wrl.ubus.ctx);

	/* Recurring work */
	wrl.recurring.cb = wrl_recurring_work_timeout;
	uloop_timeout_set(&wrl.recurring, WRL_RECURRING_WORK_INTERVAL);

	/* Cya */
	uloop_run();
	uloop_done();

	return 0;
}
