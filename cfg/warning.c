#include <stdarg.h>

#include <tarantool.h>
#include <util.h>
#include <third_party/confetti/prscfg.h>

void
out_warning(ConfettyError v, char *format, ...)
{
	(void)v; /* make gcc happy */
	va_list ap;
	va_start(ap, format);
	vsay(S_WARN, NULL, format, ap);
}
