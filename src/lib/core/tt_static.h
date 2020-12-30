#ifndef TARANTOOL_CORE_STATIC_H_INCLUDED
#define TARANTOOL_CORE_STATIC_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "small/static.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#ifndef MIN
#define UNDEF_MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/** Can't be enum - used by preprocessor for static assertions. */
#define TT_STATIC_BUF_LEN 2048

/**
 * Return a thread-local statically allocated temporary buffer of
 * size @a TT_STATIC_BUF_LEN.
 */
static inline char *
tt_static_buf(void)
{
	return (char *) static_aligned_alloc(TT_STATIC_BUF_LEN,
					     sizeof(intptr_t));
}

/**
 * Return a null-terminated string for @a str of length @a len.
 */
static inline const char *
tt_cstr(const char *str, size_t len)
{
	len = MIN(len, SMALL_STATIC_SIZE - 1);
	char *buf = (char *) static_alloc(len + 1);
	memcpy(buf, str, len);
	buf[len] = '\0';
	return buf;
}

/**
 * Wrapper around vsnprintf() that prints the result to
 * the static buffer.
 */
static inline const char *
tt_vsnprintf(size_t size, const char *format, va_list ap)
{
	char *buf = (char *) static_reserve(size);
	assert(buf != NULL);
	int rc = vsnprintf(buf, size, format, ap);
	if (rc >= 0) {
		/* +1 for terminating zero. */
		rc = MIN((int) size, rc + 1);
		char *tmp = (char *) static_alloc(rc);
		assert(tmp == buf);
		(void) tmp;
	}
	return buf;
}

/** @copydoc tt_vsnprintf() */
static inline const char *
tt_sprintf(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	const char *result = tt_vsnprintf(TT_STATIC_BUF_LEN, format, ap);
	va_end(ap);
	return result;
}

/**
 * The same as tt_sprintf() but allows to specify more precise
 * string limits.
 */
static inline const char *
tt_snprintf(size_t size, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	const char *result = tt_vsnprintf(MIN(size, (size_t) SMALL_STATIC_SIZE),
					  format, ap);
	va_end(ap);
	return result;
}

#ifdef UNDEF_MIN
#undef MIN
#endif

#endif /* TARANTOOL_CORE_STATIC_H_INCLUDED */
