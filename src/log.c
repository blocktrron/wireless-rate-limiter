#include <syslog.h>

#include "log.h"

enum channeld_debug_level log_level = MSG_INFO;
int write_to_syslog = 0;

void log_level_set(enum channeld_debug_level level)
{
	log_level = level;
}

void log_syslog(int enable) {
	if (enable)
		openlog("wirelessratelimit", LOG_PID, LOG_DAEMON);
	else
		closelog();
	write_to_syslog = enable;
}

static int log_level_to_syslog(int level)
{
	switch (level) {
	case MSG_FATAL:
	case MSG_ERROR:
		return LOG_ERR;
	case MSG_WARN:
		return LOG_WARNING;
	case MSG_INFO:
		return LOG_INFO;
	case MSG_VERBOSE:
	case MSG_DEBUG:
		return LOG_DEBUG;
	default:
		return LOG_INFO;
	}
}

void debug_msg(int level, const char *func, int line, const char *format, ...)
{
	va_list ap;

	if (level > log_level)
		return;

	if (write_to_syslog) {
		va_start(ap, format);
		vsyslog(log_level_to_syslog(level), format, ap);
		va_end(ap);
	} else {
		if (level == MSG_DEBUG)
			fprintf(stderr, "[%s:%d] ", func, line);
		va_start(ap, format);
		vfprintf(stderr, format, ap);
		va_end(ap);
	}
}
