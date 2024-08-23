#pragma once

#include <libubus.h>
#include <libubox/uloop.h>

#include "config.h"
#include "list.h"

struct wrl_data {
	struct {
	    struct ubus_context ctx;
	} ubus;

	struct wrl_config config;

	struct uloop_timeout recurring;

	struct list_head interfaces;
};
