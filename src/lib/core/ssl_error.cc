/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ssl_error.h"

#include <stdarg.h>
#include <stddef.h>

#include "diag.h"
#include "reflection.h"
#include "trivia/config.h"
#include "trivia/util.h"

#if defined(ENABLE_SSL)
# error unimplemented
#endif

const struct type_info type_SSLError = make_type("SSLError", NULL);

struct error *
BuildSSLError(const char *file, unsigned line, const char *format, ...)
{
	void *ptr = xmalloc(sizeof(SSLError));
	SSLError *err = new(ptr) SSLError(file, line);
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(err, format, ap);
	va_end(ap);
	return err;
}
