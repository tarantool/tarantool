#include "warning.h"
#include <stdarg.h>

#include <tarantool.h>
#include <util.h>

/** This is a callback function used by the generated
 * configuration file parser (tarantool_{silverbox, feeder,
 * ...}_cfg.c) to complain when something wrong happens.
 */

void
out_warning(ConfettyError v, char *format, ...)
{
	va_list ap;

	(void)v; /* make gcc happy */

	va_start(ap, format);
	tbuf_printf(cfg_out, "\r\n - ");
	tbuf_vprintf(cfg_out, format, ap);
	va_end(ap);
}
