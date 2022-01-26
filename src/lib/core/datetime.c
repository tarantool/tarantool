/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <assert.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#define DT_PARSE_ISO_TNT
#include "c-dt/dt.h"
#include "datetime.h"
#include "trivia/util.h"
#include "tzcode/tzcode.h"

/** floored modulo and divide */
#define MOD(a, b) unlikely(a < 0) ? (b + (a % b)) : (a % b)
#define DIV(a, b) unlikely(a < 0) ? ((a - b + 1) / b) : (a / b)

/**
 * Given the seconds from Epoch (1970-01-01) we calculate date
 * since Rata Die (0001-01-01).
 * DT_EPOCH_1970_OFFSET is the distance in days from Rata Die to Epoch.
 */
static int
local_dt(int64_t secs)
{
	return dt_from_rdn((int)(DIV(secs, SECS_PER_DAY)) +
			   DT_EPOCH_1970_OFFSET);
}

static int64_t
local_secs(const struct datetime *date)
{
	return (int64_t)date->epoch + date->tzoffset * 60;
}

void
datetime_to_tm(const struct datetime *date, struct tnt_tm *tm)
{
	struct tm t;
	memset(&t, 0, sizeof(t));
	tm->tm_epoch = local_secs(date);
	dt_to_struct_tm(local_dt(tm->tm_epoch), &t);
	tm->tm_year = t.tm_year;
	tm->tm_mon = t.tm_mon;
	tm->tm_mday = t.tm_mday;
	tm->tm_wday = t.tm_wday;
	tm->tm_yday = t.tm_yday;

	tm->tm_gmtoff = date->tzoffset * 60;
	tm->tm_nsec = date->nsec;

	int seconds_of_day = MOD(tm->tm_epoch, SECS_PER_DAY);
	tm->tm_hour = (seconds_of_day / 3600) % 24;
	tm->tm_min = (seconds_of_day / 60) % 60;
	tm->tm_sec = seconds_of_day % 60;
}

size_t
datetime_strftime(const struct datetime *date, char *buf, size_t len,
		  const char *fmt)
{
	assert(date != NULL);
	struct tnt_tm tm;
	datetime_to_tm(date, &tm);
	return tnt_strftime(buf, len, fmt, &tm);
}

bool
tm_to_datetime(struct tnt_tm *tm, struct datetime *date)
{
	assert(tm != NULL);
	assert(date != NULL);
	int year = tm->tm_year;
	int mon = tm->tm_mon;
	int mday = tm->tm_mday;
	int yday = tm->tm_yday;
	int wday = tm->tm_wday;
	dt_t dt = 0;

	if ((year | mon | mday) == 0) {
		if (yday != 0) {
			dt = yday - 1 + DT_EPOCH_1970_OFFSET;
		} else if (wday != 0) {
			/* 1970-01-01 was Thursday */
			dt = ((wday - 4) % 7) + DT_EPOCH_1970_OFFSET;
		}
	} else {
		if (mday == 0)
			mday = 1;
		assert(mday >= 1 && mday <= 31);
		assert(mon >= 0 && mon <= 11);
		if (dt_from_ymd_checked(year + 1900, mon + 1, mday, &dt) == false)
			return false;
	}
	int64_t local_secs =
		(int64_t)dt * SECS_PER_DAY - SECS_EPOCH_1970_OFFSET;
	local_secs += tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
	date->epoch = local_secs - tm->tm_gmtoff;
	date->nsec = tm->tm_nsec;
	date->tzindex = 0;
	date->tzoffset = tm->tm_gmtoff / 60;
	return true;
}

size_t
datetime_strptime(struct datetime *date, const char *buf, const char *fmt)
{
	assert(date != NULL);
	assert(fmt != NULL);
	assert(buf != NULL);
	struct tnt_tm t = { .tm_epoch = 0 };
	char * ret = tnt_strptime(buf, fmt, &t);
	if (ret == NULL)
		return 0;
	if (tm_to_datetime(&t, date) == false)
		return 0;
	return ret - buf;
}

void
datetime_now(struct datetime *now)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	now->epoch = tv.tv_sec;
	now->nsec = tv.tv_usec * 1000;

	struct tm tm;
	localtime_r(&tv.tv_sec, &tm);
	now->tzoffset = tm.tm_gmtoff / 60;
}

/**
 * NB! buf may be NULL, and we should handle it gracefully, returning
 * calculated length of output string
 */
size_t
datetime_to_string(const struct datetime *date, char *buf, ssize_t len)
{
	int offset = date->tzoffset;
	int64_t rd_seconds = (int64_t)date->epoch + offset * 60 +
			     SECS_EPOCH_1970_OFFSET;
	int64_t rd_number = DIV(rd_seconds, SECS_PER_DAY);
	assert(rd_number <= INT_MAX);
	assert(rd_number >= INT_MIN);
	dt_t dt = dt_from_rdn((int)rd_number);

	int year, month, day, second, nanosec, sign;
	dt_to_ymd(dt, &year, &month, &day);

	rd_seconds = MOD(rd_seconds, SECS_PER_DAY);
	int hour = (rd_seconds / 3600) % 24;
	int minute = (rd_seconds / 60) % 60;
	second = rd_seconds % 60;
	nanosec = date->nsec;

	size_t sz = 0;
	SNPRINT(sz, snprintf, buf, len, "%04d-%02d-%02dT%02d:%02d:%02d",
		year, month, day, hour, minute, second);
	if (nanosec != 0) {
		if (nanosec % 1000000 == 0) {
			SNPRINT(sz, snprintf, buf, len, ".%03d",
				nanosec / 1000000);

		} else if (nanosec % 1000 == 0) {
			SNPRINT(sz, snprintf, buf, len, ".%06d",
				nanosec / 1000);
		} else {
			SNPRINT(sz, snprintf, buf, len, ".%09d", nanosec);
		}
	}
	if (offset == 0) {
		SNPRINT(sz, snprintf, buf, len, "Z");
	} else {
		if (offset < 0) {
			sign = '-';
			offset = -offset;
		} else {
			sign = '+';
		}
		SNPRINT(sz, snprintf, buf, len, "%c%02d%02d", sign,
			offset / 60, offset % 60);
	}
	return sz;
}

size_t
datetime_parse_full(struct datetime *date, const char *str, size_t len,
		    int32_t offset)
{
	size_t n;
	dt_t dt;
	const char *svp = str;
	char c;
	int sec_of_day = 0, nanosecond = 0;

	n = dt_parse_iso_date(str, len, &dt);
	if (n == 0)
		return 0;

	str += n;
	len -= n;
	if (len <= 0)
		goto exit;

	c = *str++;
	if (c != 'T' && c != 't' && c != ' ')
		return 0;
	len--;
	if (len <= 0)
		goto exit;

	n = dt_parse_iso_time(str, len, &sec_of_day, &nanosecond);
	if (n == 0)
		return 0;

	str += n;
	len -= n;
	if (len <= 0)
		goto exit;

	if (*str == ' ') {
		str++;
		len--;
	}
	if (len <= 0)
		goto exit;

	n = dt_parse_iso_zone_lenient(str, len, &offset);
	if (n == 0)
		return 0;
	str += n;

exit:
	date->epoch =
		((int64_t)dt_rdn(dt) - DT_EPOCH_1970_OFFSET) * SECS_PER_DAY +
		sec_of_day - offset * 60;
	date->nsec = nanosecond;
	date->tzoffset = offset;
	date->tzindex = 0;

	return str - svp;
}

int
datetime_compare(const struct datetime *lhs, const struct datetime *rhs)
{
	int result = COMPARE_RESULT(lhs->epoch, rhs->epoch);
	if (result != 0)
		return result;

	return COMPARE_RESULT(lhs->nsec, rhs->nsec);
}
