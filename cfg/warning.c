#include "warning.h"

#include <stdio.h>

#include <stdarg.h>

FILE *cfg_out;
char *cfg_log;
size_t cfg_logsize;

/** This is a callback function used by the generated
 * configuration file parser (tarantool_{box, feeder, ...}_cfg.c)
 * to complain when something wrong happens.
 */
void
out_warning(ConfettyError v, const char *format, ...)
{
	va_list ap;

	(void)v; /* make gcc happy */
	va_start(ap, format);
	vfprintf(cfg_out, format, ap);
	fprintf(cfg_out, ".\n");
	fflush(cfg_out);
	va_end(ap);
}
