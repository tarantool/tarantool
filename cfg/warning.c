#include "warning.h"

#include <tbuf.h>

#include <stdarg.h>

struct tbuf *cfg_out = NULL;

/** This is a callback function used by the generated
 * configuration file parser (tarantool_{box, feeder, ...}_cfg.c)
 * to complain when something wrong happens.
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
