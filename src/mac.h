#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static inline uint8_t *
wrl_mac_from_string(const char *str, uint8_t *mac)
{
	if (strlen(str) != 17)
		return NULL;

	for (int i = 0; i < 6; i++)
	{
		char *end;
		mac[i] = strtol(str, &end, 16);
		if (end == str || (i < 5 && *end != ':'))
			return NULL;
		str = end + 1;
	}

	return mac;
}

static inline char *
wrl_mac_to_string(const uint8_t *mac, char *str)
{
	static char str_buf[18];

	if (!str)
		str = str_buf;

	sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return str;
}

static inline int
wrl_mac_is_zero(const uint8_t *mac)
{
	for (int i = 0; i < 6; i++)
	{
		if (mac[i] != 0)
			return 0;
	}

	return 1;
}
