#pragma once

#include <libubus.h>
#include <libubox/uloop.h>

#include "config.h"
#include "list.h"

enum wrl_purge_state {
	WRL_PURGE_DONE = 0,
	WRL_PURGE_PENDING = 1,
	WRL_PURGE_NONE = 2,
};

struct wrl_data {
	struct {
	    struct ubus_context ctx;
	} ubus;

	struct wrl_config config;
	enum wrl_purge_state full_purge;

	struct uloop_timeout recurring;

	struct list_head interfaces;
};
