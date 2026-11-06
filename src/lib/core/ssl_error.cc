/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ssl_error.h"

#include <assert.h>
#include <openssl/err.h>
#include <stdarg.h>
#include <stddef.h>

#include "diag.h"
#include "exception.h"
#include "reflection.h"
#include "trivia/config.h"
#include "trivia/util.h"
#include "tt_strerror.h"

const struct type_info type_SSLError = make_type("SSLError", NULL);

static int
ssl_error_append_entry(const char *str, size_t len, void *arg)
{
	SSLError *err = static_cast<SSLError *>(arg);
	error_append_msg(err, "%.*s", (int)len, str);
	return 1;
}

static void
ssl_error_create(SSLError *err, const char *format, va_list ap)
{
	error_vformat_msg(err, format, ap);
	if (ERR_peek_error() != 0) {
		error_append_msg(err, ":\n");
		ERR_print_errors_cb(ssl_error_append_entry, err);
		assert(ERR_peek_error() == 0);
	} else {
		error_append_msg(err, ": <no SSL error>");
	}
}

SSLError::SSLError(const char *file, unsigned line, const char *format, ...)
	: SSLError(file, line)
{
	va_list ap;
	va_start(ap, format);
	ssl_error_create(this, format, ap);
	va_end(ap);
}

struct error *
BuildSSLError(const char *file, unsigned line, const char *format, ...)
{
	void *ptr = xmalloc(sizeof(SSLError));
	SSLError *err = new (ptr) SSLError(file, line);
	va_list ap;
	va_start(ap, format);
	ssl_error_create(err, format, ap);
	va_end(ap);
	return err;
}
