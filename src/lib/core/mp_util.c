/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "diag.h"
#include "mp_util.h"
#include "msgpuck.h"
#include "small/region.h"

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
