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

/**
 * Interval support functions: stringization and operations
 */

/**
 * if |nsec| is larger than allowed range 1e9 then modify passed
 * sec accordingly
 */

#define NANOS_PER_SEC 1000000000

static inline int64_t
interval_seconds(const struct datetime_interval *ival)
{
	return (int64_t)ival->sec + ival->nsec / NANOS_PER_SEC;
}

static inline int
interval_days(const struct datetime_interval *ival)
{
	return interval_seconds(ival) / SECS_PER_DAY;
}

static inline int
interval_hours(const struct datetime_interval *ival)
{
	return interval_seconds(ival) / (60 * 60);
}

static inline int
interval_minutes(const struct datetime_interval *ival)
{
	return interval_seconds(ival) / 60;
}

static void
denormalize_interval_nsec(int64_t *psec, int *pnsec)
{
	assert(psec != NULL);
	assert(pnsec != NULL);
	int64_t sec = *psec;
	int nsec = *pnsec;
	/*
	 * nothing to change:
	 * - if there is small nsec with 0 in sec
	 * - or if both sec and nsec have the same sign, and nsec is
	 *   small enough
	 */
	if (nsec == 0)
		return;

	bool zero_or_same_sign = sec == 0 || (sec < 0) == (nsec < 0);
	if (zero_or_same_sign && labs(nsec) < NANOS_PER_SEC)
		return;

	sec += nsec / NANOS_PER_SEC;
	nsec = nsec % NANOS_PER_SEC;

	*psec = sec;
	*pnsec = nsec;
}

static size_t
seconds_str(char *buf, ssize_t len, bool need_sign, long sec, int nsec)
{
	sec %= 60;
	denormalize_interval_nsec(&sec, &nsec);
	size_t sz = 0;
	bool is_neg = sec < 0 || (sec == 0 && nsec < 0);

	SNPRINT(sz, snprintf, buf, len, is_neg ? "-" : (need_sign ? "+" : ""));
	sec = labs(sec);
	if (nsec != 0) {
		SNPRINT(sz, snprintf, buf, len, "%ld.%09ld seconds",
			sec, labs(nsec));
	} else {
		SNPRINT(sz, snprintf, buf, len, "%ld seconds", sec);
	}
	return sz;
}

#define SPACE() \
	if (sz > 0) { \
		SNPRINT(sz, snprintf, buf, len, ", "); \
	}

size_t
interval_to_string(const struct datetime_interval *ival, char *buf, ssize_t len)
{
	static char* signed_fmt[] = {
		"%d",	/* false */
		"%+d",	/* true */
	};
	static const char zero_secs[] = "0 seconds";

	bool need_sign = true;
	size_t sz = 0;
	if (ival->year != 0) {
		SNPRINT(sz, snprintf, buf, len, "%+d years", ival->year);
		need_sign = false;
	}
	if (ival->month != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, signed_fmt[need_sign],
			ival->month);
		SNPRINT(sz, snprintf, buf, len, " months");
		need_sign = false;
	}
	int64_t secs = (int64_t)ival->sec;
	int nsec = ival->nsec;
	if (secs == 0 && nsec == 0) {
		if (sz != 0) {
			return sz;
		} else {
			SNPRINT(sz, snprintf, buf, len, zero_secs);
			return sizeof(zero_secs) - 1;
		}
	}
	long abs_s = labs(secs);
	if (abs_s < 60) {
		SPACE();
		SNPRINT(sz, seconds_str, buf, len, need_sign, secs, nsec);
	} else if (abs_s < 60 * 60) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, signed_fmt[need_sign],
			interval_minutes(ival));
		SNPRINT(sz, snprintf, buf, len, " minutes, ");
		SNPRINT(sz, seconds_str, buf, len, false, secs, nsec);
	} else if (abs_s < SECS_PER_DAY) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, signed_fmt[need_sign],
			interval_hours(ival));
		SNPRINT(sz, snprintf, buf, len, " hours, %d minutes, ",
			interval_minutes(ival) % 60);
		SNPRINT(sz, seconds_str, buf, len, false, secs, nsec);
	} else {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, signed_fmt[need_sign],
			interval_days(ival));
		SNPRINT(sz, snprintf, buf, len, " days, %d hours, %d minutes, ",
			interval_hours(ival) % 24,
			interval_minutes(ival) % 60);
		SNPRINT(sz, seconds_str, buf, len, false, secs, nsec);
	}
	return sz;
}
