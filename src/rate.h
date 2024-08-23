#pragma once

#include <stdlib.h>
#include <stdint.h>

struct wrl_rate{
	uint32_t down;
	uint32_t up;

	uint8_t applied;
};
