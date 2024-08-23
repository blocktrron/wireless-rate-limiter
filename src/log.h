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

static inline void debug_msg(int level, const char *func, int line, const char *format, ...)
{
	va_list ap;

	if (level > MSG_DEBUG)
		return;

	fprintf(stderr, "[%s:%d] ", func, line);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
}
