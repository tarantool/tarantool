/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <datetime.h>
#include <mp_datetime.h>
#include <mp_interval.h>

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

ssize_t
tnt_datetime_parse_full(struct datetime *date, const char *str, size_t len,
			const char *tzsuffix, int32_t offset)
{
	return datetime_parse_full(date, str, len, tzsuffix, offset);
}

ssize_t
tnt_datetime_parse_tz(const char *str, size_t len, time_t base_date,
		      int16_t *tzoffset, int16_t *tzindex)
{
	return datetime_parse_tz(str, len, base_date, tzoffset, tzindex);
}

struct datetime *
tnt_datetime_unpack(const char **data, uint32_t len, struct datetime *date)
{
	return datetime_unpack(data, len, date);
}

bool
tnt_datetime_totable(const struct datetime *date, struct interval *out)
{
	return datetime_totable(date, out);
}

size_t
tnt_interval_to_string(const struct interval *ival, char *buf, ssize_t len)
{
	return interval_to_string(ival, buf, len);
}

int
tnt_datetime_increment_by(struct datetime *self, int direction,
			  const struct interval *ival)
{
	return datetime_increment_by(self, direction, ival);
}

int
tnt_datetime_datetime_sub(struct interval *res, const struct datetime *lhs,
			  const struct datetime *rhs)
{
	return datetime_datetime_sub(res, lhs, rhs);
}

int
tnt_interval_interval_sub(struct interval *lhs, const struct interval *rhs)
{
	return interval_interval_sub(lhs, rhs);
}

int
tnt_interval_interval_add(struct interval *lhs, const struct interval *rhs)
{
	return interval_interval_add(lhs, rhs);
}

struct interval *
tnt_interval_unpack(const char **data, uint32_t len, struct interval *itv)
{
	return interval_unpack(data, len, itv);
}

bool
tnt_datetime_isdst(const struct datetime *date)
{
	return datetime_isdst(date);
}
