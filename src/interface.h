#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <libubus.h>

#include "client.h"
#include "list.h"
#include "rate.h"

#define WRL_INTERFACE_NUM_CLIENTS 256

struct wrl_interface {
	struct list_head head;

	char name[32];
	struct wrl_client clients[WRL_INTERFACE_NUM_CLIENTS];
	struct wrl_rate rate;

	struct {
		uint32_t id;

		struct ubus_request req;
		uint8_t req_pending;
	} ubus;

	uint8_t missing;
};
