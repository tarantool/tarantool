/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "mp_util.h"
#include "msgpuck.h"
#include "small/region.h"
#include "trivia/util.h"
#include "tt_static.h"

size_t
mp_len(const char *data)
{
	const char *end = data;
	mp_next(&end);
	return end - data;
}

char *
mp_dup(const char *data)
{
	size_t len = mp_len(data);
	char *copy = xmalloc(len);
	memcpy(copy, data, len);
	return copy;
}

const char *
mp_str(const char *data)
{
	char *buf = tt_static_buf();
	if (mp_snprint(buf, TT_STATIC_BUF_LEN, data) < 0)
		return "<failed to format message pack>";
	return buf;
}

const char *
mp_vformat_on_region(struct region *region, size_t *size, const char *format,
		     va_list src)
{
	va_list ap;
	va_copy(ap, src);
	size_t buf_size = mp_vformat(NULL, 0, format, ap);
	char *buf = xregion_alloc(region, buf_size);
	va_end(ap);
	va_copy(ap, src);
	*size = mp_vformat(buf, buf_size, format, ap);
	va_end(ap);
	assert(*size == buf_size);
	return buf;
}

const char *
mp_format_on_region(struct region *region, size_t *size, const char *format,
		    ...)
{
	va_list ap;
	va_start(ap, format);
	const char *res = mp_vformat_on_region(region, size, format, ap);
	va_end(ap);
	return res;
}
