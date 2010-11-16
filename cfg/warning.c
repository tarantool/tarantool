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
	switch (v) {
	case CNF_NOTSET:
		vsay(S_FATAL, NULL, format, ap);
		panic("can't read config");

		break;

	default:
		vsay(S_WARN, NULL, format, ap);
	}
}
