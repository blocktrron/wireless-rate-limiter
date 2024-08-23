#pragma once

#include <stdlib.h>
#include <stdint.h>

#include "list.h"
#include "rate.h"

struct wrl_client {
	uint32_t id;
	uint8_t address[6];

	struct wrl_rate rate;

	uint8_t connected;
};