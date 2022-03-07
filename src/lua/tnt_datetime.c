/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <datetime.h>
#include <mp_datetime.h>

size_t
tnt_datetime_strftime(const struct datetime *date, char *buf, size_t len,
		      const char *fmt)
{
	return datetime_strftime(date, buf, len, fmt);
}

size_t
tnt_datetime_strptime(struct datetime *date, const char *buf, const char *fmt)
{
	return datetime_strptime(date, buf, fmt);
}

void
tnt_datetime_now(struct datetime *now)
{
	return datetime_now(now);
}

size_t
tnt_datetime_to_string(const struct datetime *date, char *buf, ssize_t len)
{
	return datetime_to_string(date, buf, len);
}

size_t
tnt_datetime_parse_full(struct datetime *date, const char *str, size_t len,
			int32_t offset)
{
	return datetime_parse_full(date, str, len, offset);
}

struct datetime *
tnt_datetime_unpack(const char **data, uint32_t len, struct datetime *date)
{
	return datetime_unpack(data, len, date);
}

size_t
tnt_interval_to_string(const struct datetime_interval *ival, char *buf,
		       ssize_t len)
{
	return interval_to_string(ival, buf, len);
}
