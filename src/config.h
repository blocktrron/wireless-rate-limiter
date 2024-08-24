#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "client.h"
#include "interface.h"
#include "list.h"
#include "rate.h"

enum selector_type {
	SELECTOR_TYPE_INTERFACE = 0x01,
	SELECTOR_TYPE_SSID = 0x02,
	SELECTOR_TYPE_MAC = 0x04,
};

struct wrl_config_interface_selectors {
	char interface[32];
	char ssid[32];
};

struct wrl_config_interface {
	struct list_head head;

	/* Policy selectors */
	struct wrl_config_interface_selectors selectors;

	struct wrl_rate rate;
};

struct wrl_config_client_selectors {
	char interface[32];
	char ssid[32];
	char mac[6];
};

struct wrl_config_client {
	struct list_head head;

	/* Policy selectors */
	struct wrl_config_client_selectors selectors;

	struct wrl_rate rate;
};

struct wrl_config {
	struct list_head interfaces;
	struct list_head clients;
};

void wrl_config_init(struct wrl_config *config);

/* Interface config */
struct wrl_config_interface *wrl_config_interface_get(struct wrl_config *config, struct wrl_config_interface_selectors *selectors, int *create);
void wrl_config_interface_purge(struct wrl_config *config);

/* Client config */
struct wrl_config_client *wrl_config_client_get(struct wrl_config *config, struct wrl_config_client_selectors *selectors, int *create);
void wrl_config_client_purge(struct wrl_config *config);

/* State update methods */
int wrl_config_interface_update(struct wrl_config *config, struct wrl_interface *interface);
int wrl_config_client_update(struct wrl_config *config, struct wrl_interface *interface, struct wrl_client *client);