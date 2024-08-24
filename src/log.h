#pragma once

#include <stdarg.h>
#include <stdio.h>

enum channeld_debug_level {
	MSG_FATAL = 0,
	MSG_ERROR,
	MSG_WARN,
	MSG_INFO,
	MSG_VERBOSE,
	MSG_DEBUG,
};

#define MSG(_nr, _format, ...) debug_msg(MSG_##_nr, __func__, __LINE__, _format, ##__VA_ARGS__)

void log_syslog(int enable);
void log_level_set(enum channeld_debug_level level);
void debug_msg(int level, const char *func, int line, const char *format, ...);
