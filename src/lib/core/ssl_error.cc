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

static void
ssl_error_create(SSLError *err, const char *format, va_list ap)
{
	unsigned long code = ERR_peek_error();
	ERR_clear_error();
	error_vformat_msg(err, format, ap);
	if (code != 0) {
		const char *msg;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
		/**
		 * Since OpenSSL 3.0, ERR_reason_error_string() returns NULL
		 * for system errors so we have to format the message manually.
		 */
		if (ERR_SYSTEM_ERROR(code))
			msg = tt_strerror(ERR_GET_REASON(code));
		else
#endif
			msg = ERR_reason_error_string(code);
		assert(msg != NULL);
		error_append_msg(err, ": %s", msg);
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
