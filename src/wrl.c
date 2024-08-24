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

	for (int i = 0; i < WRL_INTERFACE_NUM_CLIENTS; i++) {
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
	for (i = 0; i < WRL_INTERFACE_NUM_CLIENTS; i++) {
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
			MSG(DEBUG, "New client, scheudling rate update\n");
			client->rate.applied = 0;
		}

		MSG(DEBUG, "Client %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		client->connected = 1;
	}

	/* Zero all clients that are not connected */
	for (i = 0; i < WRL_INTERFACE_NUM_CLIENTS; i++) {
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
			MSG(WARN, "Interface %s missing, removing\n", interface->name);
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
			MSG(DEBUG, "Update rate-limits for interface %s rx=%d tx=%d\n", interface->name, interface->rate.down, interface->rate.up);
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
wrl_ubus_clear_config(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct wrl_data *wrl = container_of(ctx, struct wrl_data, ubus.ctx);

	if (wrl->full_purge == WRL_PURGE_DONE) {
		/* Already purged */
		return UBUS_STATUS_OK;
	}

	wrl->full_purge = WRL_PURGE_PENDING;

	MSG(INFO, "Clearing Interface configuration\n");
	wrl_config_interface_purge(&wrl->config);
	MSG(INFO, "Clearing Client configuration\n");
	wrl_config_client_purge(&wrl->config);

	return UBUS_STATUS_OK;
}

enum {
	WRL_UBUS_SET_CLIENT_INTERFACE,
	WRL_UBUS_SET_CLIENT_DOWN,
	WRL_UBUS_SET_CLIENT_UP,
	__WRL_UBUS_SET_CLIENT_MAX,
};

static const struct blobmsg_policy wrl_ubus_set_client_policy[] = {
	[WRL_UBUS_SET_CLIENT_INTERFACE] = { .name = "interface", .type = BLOBMSG_TYPE_STRING },
	[WRL_UBUS_SET_CLIENT_DOWN] = { .name = "down", .type = BLOBMSG_TYPE_INT32 },
	[WRL_UBUS_SET_CLIENT_UP] = { .name = "up", .type = BLOBMSG_TYPE_INT32 },
};

static int
wrl_ubus_set_client_config(struct ubus_context *ctx, struct ubus_object *obj,
			   struct ubus_request_data *req, const char *method,
			   struct blob_attr *msg)
{
	struct wrl_data *wrl = container_of(ctx, struct wrl_data, ubus.ctx);
	struct wrl_config_client_selectors client_selectors = {};
	struct blob_attr *tb[__WRL_UBUS_SET_CLIENT_MAX];
	struct wrl_config_client *client;
	int create;
	int ret;

	ret = blobmsg_parse(wrl_ubus_set_client_policy, __WRL_UBUS_SET_CLIENT_MAX, tb, blob_data(msg), blob_len(msg));
	if (ret) {
		MSG(ERROR, "Failed to parse message\n");
		return UBUS_STATUS_INVALID_ARGUMENT;
	}

	if (!tb[WRL_UBUS_SET_CLIENT_DOWN] || !tb[WRL_UBUS_SET_CLIENT_UP]) {
		MSG(ERROR, "Missing arguments\n");
		return UBUS_STATUS_INVALID_ARGUMENT;
	}

	if (tb[WRL_UBUS_SET_CLIENT_INTERFACE])
		strncpy(client_selectors.interface, blobmsg_data(tb[WRL_UBUS_SET_CLIENT_INTERFACE]), sizeof(client_selectors.interface));

	client = wrl_config_client_get(&wrl->config, &client_selectors, &create);
	if (!client) {
		MSG(ERROR, "Failed to get client\n");
		return UBUS_STATUS_UNKNOWN_ERROR;
	}

	client->rate.down = blobmsg_get_u32(tb[WRL_UBUS_SET_CLIENT_DOWN]);
	client->rate.up = blobmsg_get_u32(tb[WRL_UBUS_SET_CLIENT_UP]);

	wrl->full_purge = WRL_PURGE_NONE;

	return UBUS_STATUS_OK;
}

static int
wrl_ubus_get_client_config(struct ubus_context *ctx, struct ubus_object *obj,
			   struct ubus_request_data *req, const char *method,
			   struct blob_attr *msg)
{
	struct wrl_data *wrl = container_of(ctx, struct wrl_data, ubus.ctx);
	struct wrl_config_client *client;
	void *a, *t;

	blob_buf_init(&b, 0);

	a = blobmsg_open_array(&b, "client_config");
	list_for_each_entry(client, &wrl->config.clients, head) {
		t = blobmsg_open_table(&b, "client");
		blobmsg_add_string(&b, "interface", client->selectors.interface);
		blobmsg_add_u32(&b, "down", client->rate.down);
		blobmsg_add_u32(&b, "up", client->rate.up);
		blobmsg_close_table(&b, t);
	}
	blobmsg_close_array(&b, a);

	ubus_send_reply(ctx, req, b.head);

	return UBUS_STATUS_OK;
}

enum {
	WRL_UBUS_SET_INTERFACE_INTERFACE,
	WRL_UBUS_SET_INTERFACE_DOWN,
	WRL_UBUS_SET_INTERFACE_UP,
	__WRL_UBUS_SET_INTERFACE_MAX,
};

static const struct blobmsg_policy wrl_ubus_set_interface_policy[] = {
	[WRL_UBUS_SET_INTERFACE_INTERFACE] = { .name = "interface", .type = BLOBMSG_TYPE_STRING },
	[WRL_UBUS_SET_INTERFACE_DOWN] = { .name = "down", .type = BLOBMSG_TYPE_INT32 },
	[WRL_UBUS_SET_INTERFACE_UP] = { .name = "up", .type = BLOBMSG_TYPE_INT32 },
};

static int
wrl_ubus_set_interface_config(struct ubus_context *ctx, struct ubus_object *obj,
			      struct ubus_request_data *req, const char *method,
			      struct blob_attr *msg)
{
	struct wrl_data *wrl = container_of(ctx, struct wrl_data, ubus.ctx);
	struct wrl_config_interface_selectors interface_selectors = {};
	struct blob_attr *tb[__WRL_UBUS_SET_CLIENT_MAX];
	struct wrl_config_interface *interface;
	int create;
	int ret;

	ret = blobmsg_parse(wrl_ubus_set_interface_policy, __WRL_UBUS_SET_INTERFACE_MAX, tb, blob_data(msg), blob_len(msg));
	if (ret) {
		MSG(ERROR, "Failed to parse message\n");
		return UBUS_STATUS_INVALID_ARGUMENT;
	}

	if (!tb[WRL_UBUS_SET_INTERFACE_DOWN] || !tb[WRL_UBUS_SET_INTERFACE_UP]) {
		MSG(ERROR, "Missing arguments\n");
		return UBUS_STATUS_INVALID_ARGUMENT;
	}

	if (tb[WRL_UBUS_SET_INTERFACE_INTERFACE])
		strncpy(interface_selectors.interface, blobmsg_data(tb[WRL_UBUS_SET_INTERFACE_INTERFACE]), sizeof(interface_selectors.interface));

	interface = wrl_config_interface_get(&wrl->config, &interface_selectors, &create);
	if (!interface) {
		MSG(ERROR, "Failed to get interface\n");
		return UBUS_STATUS_UNKNOWN_ERROR;
	}

	interface->rate.down = blobmsg_get_u32(tb[WRL_UBUS_SET_INTERFACE_DOWN]);
	interface->rate.up = blobmsg_get_u32(tb[WRL_UBUS_SET_INTERFACE_UP]);

	wrl->full_purge = WRL_PURGE_NONE;

	return UBUS_STATUS_OK;
}

static int
wrl_ubus_get_interface_config(struct ubus_context *ctx, struct ubus_object *obj,
			      struct ubus_request_data *req, const char *method,
			      struct blob_attr *msg)
{
	struct wrl_data *wrl = container_of(ctx, struct wrl_data, ubus.ctx);
	struct wrl_config_interface *interface;
	void *a, *t;

	blob_buf_init(&b, 0);

	a = blobmsg_open_array(&b, "interface_config");
	list_for_each_entry(interface, &wrl->config.interfaces, head) {
		t = blobmsg_open_table(&b, "interface");
		blobmsg_add_string(&b, "interface", interface->selectors.interface);
		blobmsg_add_u32(&b, "down", interface->rate.down);
		blobmsg_add_u32(&b, "up", interface->rate.up);
		blobmsg_close_table(&b, t);
	}
	blobmsg_close_array(&b, a);

	ubus_send_reply(ctx, req, b.head);

	return UBUS_STATUS_OK;
}

static int
wrl_ubus_get_interface(struct ubus_context *ctx, struct ubus_object *obj,
		       struct ubus_request_data *req, const char *method,
		       struct blob_attr *msg)
{
	struct wrl_data *wrl = container_of(ctx, struct wrl_data, ubus.ctx);
	struct wrl_interface *interface;
	void *a, *t;

	blob_buf_init(&b, 0);

	a = blobmsg_open_array(&b, "interfaces");
	list_for_each_entry(interface, &wrl->interfaces, head) {
		t = blobmsg_open_table(&b, "interface");
		blobmsg_add_string(&b, "interface", interface->name);
		blobmsg_add_u32(&b, "down", interface->rate.down);
		blobmsg_add_u32(&b, "up", interface->rate.up);
		blobmsg_add_u8(&b, "applied", interface->rate.applied);
		blobmsg_close_table(&b, t);
	}
	blobmsg_close_array(&b, a);

	ubus_send_reply(ctx, req, b.head);

	return UBUS_STATUS_OK;
}

static int
wrl_ubus_get_client(struct ubus_context *ctx, struct ubus_object *obj,
		    struct ubus_request_data *req, const char *method,
		    struct blob_attr *msg)
{
	struct wrl_data *wrl = container_of(ctx, struct wrl_data, ubus.ctx);
	struct wrl_interface *interface;
	struct wrl_client *client;
	void *a, *t;

	blob_buf_init(&b, 0);

	a = blobmsg_open_array(&b, "clients");
	list_for_each_entry(interface, &wrl->interfaces, head) {
		for (int i = 0; i < WRL_INTERFACE_NUM_CLIENTS; i++) {
			client = &interface->clients[i];
			if (wrl_mac_is_zero(client->address))
				continue;
			
			t = blobmsg_open_table(&b, "client");
			blobmsg_add_string(&b, "address", wrl_mac_to_string(client->address, NULL));
			blobmsg_add_string(&b, "interface", interface->name);
			blobmsg_add_u32(&b, "down", client->rate.down);
			blobmsg_add_u32(&b, "up", client->rate.up);
			blobmsg_add_u8(&b, "applied", client->rate.applied);
			blobmsg_close_table(&b, t);
		}
	}
	blobmsg_close_array(&b, a);

	ubus_send_reply(ctx, req, b.head);

	return UBUS_STATUS_OK;
}


static const struct ubus_method wrl_ubus_methods[] = {
	UBUS_METHOD_NOARG("clear_config", wrl_ubus_clear_config),

	UBUS_METHOD("set_client_config", wrl_ubus_set_client_config, wrl_ubus_set_client_policy),
	UBUS_METHOD_NOARG("get_client_config", wrl_ubus_get_client_config),

	UBUS_METHOD("set_interface_config", wrl_ubus_set_interface_config, wrl_ubus_set_interface_policy),
	UBUS_METHOD_NOARG("get_interface_config", wrl_ubus_get_interface_config),

	UBUS_METHOD_NOARG("get_interface", wrl_ubus_get_interface),
	UBUS_METHOD_NOARG("get_client", wrl_ubus_get_client),
};

static struct ubus_object_type wrl_ubus_obj_type =
	UBUS_OBJECT_TYPE("wireless-rate-limiter", wrl_ubus_methods);

struct ubus_object wrl_ubus_obj = {
	.name = "wireless-rate-limiter",
	.type = &wrl_ubus_obj_type,
	.methods = wrl_ubus_methods,
	.n_methods = ARRAY_SIZE(wrl_ubus_methods),
};

static int
wrl_ubus_init(struct wrl_data *wrl)
{
	int ret;

	ret = ubus_connect_ctx(&wrl->ubus.ctx, NULL);
	if (ret) {
		fprintf(stderr, "Failed to connect to ubus: %s\n", ubus_strerror(ret));
		return 1;
	}

	ubus_add_object(&wrl->ubus.ctx, &wrl_ubus_obj);
	ubus_add_uloop(&wrl->ubus.ctx);

	return 0;
}

static void
wrl_rate_apply_interface(struct wrl_interface *interface, uint8_t purge)
{
	char command_buffer[512];
	int tx_rate, rx_rate;

	if (purge) {
		snprintf(command_buffer, sizeof(command_buffer),
			 "sh /lib/wireless-rate-limiter/htb-netdev.sh remove %s",
			 interface->name);
		wrl_execute_command(command_buffer);
		return;
	}

	tx_rate = interface->rate.up;
	rx_rate = interface->rate.down;

	if (tx_rate == 0)
		tx_rate = 1 * 1024 * 1024 * 1024;
	
	if (rx_rate == 0)
		rx_rate = 1 * 1024 * 1024 * 1024;

	snprintf(command_buffer, sizeof(command_buffer),
		 "sh /lib/wireless-rate-limiter/htb-netdev.sh add %s %dkbit %dkbit",
		 interface->name, rx_rate, tx_rate);
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
		if (wrl->full_purge == WRL_PURGE_PENDING) {
			MSG(INFO, "Purge limits for interface %s rx=%dkbit/s tx=%dkbit/s\n",
			    interface->name, interface->rate.down, interface->rate.up);
			wrl_rate_apply_interface(interface, 1);
			interface->rate.applied = 1;
		} else if (wrl->full_purge == WRL_PURGE_DONE) {
			/* Do nothing */
			continue;
		} else {
			/* Apply interface rates */
			if (!interface->rate.applied) {
				MSG(INFO, "Applying rate for interface %s rx=%dkbit/s tx=%dkbit/s\n",
				interface->name, interface->rate.down, interface->rate.up);
				wrl_rate_apply_interface(interface, 0);
			}
		}

		/* Apply client rates */
		for (i = 0; i < WRL_INTERFACE_NUM_CLIENTS; i++) {
			client = &interface->clients[i];
			if (wrl_mac_is_zero(client->address))
				continue;
			
			if (interface->rate.applied && client->rate.applied)
				continue;

			if (wrl->full_purge == WRL_PURGE_PENDING) {
				/* Interface limits purged, do nothing instead of acking 0 limits */
				client->rate.applied = 1;
				continue;
			}

			MSG(INFO, "Applying rate for client %02x:%02x:%02x:%02x:%02x:%02x, rx=%dkbit/s, tx=%dkbit/s\n",
			    client->address[0], client->address[1], client->address[2],
			    client->address[3], client->address[4], client->address[5],
			    interface->rate.down, interface->rate.up);
			
			wrl_rate_apply_client(interface, client, i);
			client->rate.applied = 1;
		}

		interface->rate.applied = 1;
	}

	if (wrl->full_purge == WRL_PURGE_PENDING)
		wrl->full_purge = WRL_PURGE_DONE;
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


int
main(int argc, char *argv[])
{
	struct wrl_data wrl = {0};

	wrl.full_purge = WRL_PURGE_DONE;

	log_syslog(1);
	log_level_set(MSG_INFO);

	INIT_LIST_HEAD(&wrl.interfaces);
	wrl_config_init(&wrl.config);

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
