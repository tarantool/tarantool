#include <stdarg.h>

#include <tarantool.h>
#include <util.h>
#include <third_party/confetti/prscfg.h>

void
out_warning(ConfettyError v, char *format, ...)
{
	va_list ap;

	(void)v; /* make gcc happy */

	va_start(ap, format);
	tbuf_printf(cfg_out, "\t");
	tbuf_vprintf(cfg_out, format, ap);
	tbuf_printf(cfg_out, "\n");
	va_end(ap);
}
