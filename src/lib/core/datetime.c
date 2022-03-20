/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>

#define DT_PARSE_ISO_TNT
#include "c-dt/dt.h"
#include "datetime.h"
#include "trivia/util.h"
#include "tzcode/tzcode.h"

#include "fiber.h"

/** floored modulo and divide */
#define MOD(a, b) (unlikely((a) < 0) ? (((b) + ((a) % (b))) % (b)) : \
		  ((a) % (b)))
#define DIV(a, b) (unlikely((a) < 0) ? (((a) - (b) + 1) / (b)) : ((a) / (b)))

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

/**
 * Create datetime structure using given tnt_tm fieldsю
 */
static bool
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
	char *ret = tnt_strptime(buf, fmt, &t);
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

void
datetime_ev_now(struct datetime *now)
{
	double timestamp = fiber_time();
	assert(timestamp > INT32_MIN && timestamp < INT32_MAX);
	long sec = timestamp;
	now->epoch = sec;
	now->nsec = (timestamp - sec) * 1000000000;

	struct tm tm;
	localtime_r(&sec, &tm);
	now->tzoffset = tm.tm_gmtoff / 60;
	now->tzindex = 0;
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

static inline int64_t
dt_seconds(const struct datetime *date)
{
	return (int64_t)date->epoch + date->tzoffset * 60 +
	       SECS_EPOCH_1970_OFFSET;
}

int64_t
datetime_year(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t rd_number = DIV(rd_seconds, SECS_PER_DAY);
	assert(rd_number <= INT_MAX && rd_number >= INT_MIN);
	dt_t dt = dt_from_rdn((int)rd_number);
	int year;
	int day;
	dt_to_yd(dt, &year, &day);
	return year;
}

int64_t
datetime_quarter(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t rd_number = DIV(rd_seconds, SECS_PER_DAY);
	assert(rd_number <= INT_MAX && rd_number >= INT_MIN);
	dt_t dt = dt_from_rdn((int)rd_number);
	int year;
	int quarter;
	int day;
	dt_to_yqd(dt, &year, &quarter, &day);
	return quarter;
}

int64_t
datetime_month(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t rd_number = DIV(rd_seconds, SECS_PER_DAY);
	assert(rd_number <= INT_MAX && rd_number >= INT_MIN);
	dt_t dt = dt_from_rdn((int)rd_number);
	int year;
	int month;
	int day;
	dt_to_ymd(dt, &year, &month, &day);
	return month;
}

int64_t
datetime_week(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t rd_number = DIV(rd_seconds, SECS_PER_DAY);
	assert(rd_number <= INT_MAX && rd_number >= INT_MIN);
	dt_t dt = dt_from_rdn((int)rd_number);
	int year;
	int week;
	int day;
	dt_to_ywd(dt, &year, &week, &day);
	return week;
}

int64_t
datetime_day(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t rd_number = DIV(rd_seconds, SECS_PER_DAY);
	assert(rd_number <= INT_MAX && rd_number >= INT_MIN);
	dt_t dt = dt_from_rdn((int)rd_number);
	int year;
	int month;
	int day;
	dt_to_ymd(dt, &year, &month, &day);
	return day;
}

int64_t
datetime_dow(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t rd_number = DIV(rd_seconds, SECS_PER_DAY);
	assert(rd_number <= INT_MAX && rd_number >= INT_MIN);
	dt_t dt = dt_from_rdn((int)rd_number);
	return (int64_t)dt_dow(dt);
}

int64_t
datetime_doy(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t rd_number = DIV(rd_seconds, SECS_PER_DAY);
	assert(rd_number <= INT_MAX && rd_number >= INT_MIN);
	dt_t dt = dt_from_rdn((int)rd_number);
	int year;
	int day;
	dt_to_yd(dt, &year, &day);
	return day;
}

int64_t
datetime_hour(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t hour = (MOD(rd_seconds, SECS_PER_DAY) / 3600) % 24;
	return hour;
}

int64_t
datetime_min(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t minute = (MOD(rd_seconds, SECS_PER_DAY) / 60) % 60;
	return minute;
}

int64_t
datetime_sec(const struct datetime *date)
{
	int64_t rd_seconds = dt_seconds(date);
	int64_t second = MOD(rd_seconds, 60);
	return second;
}

int64_t
datetime_tzoffset(const struct datetime *date)
{
	return date->tzoffset;
}

int64_t
datetime_epoch(const struct datetime *date)
{
	return date->epoch;
}

int64_t
datetime_nsec(const struct datetime *date)
{
	return date->nsec;
}

/**
 * Interval support functions: stringization and operations
 */
bool
datetime_totable(const struct datetime *date, struct interval *out)
{
	int64_t secs = local_secs(date);
	int64_t dt = local_dt(secs);

	out->year = dt_year(dt);
	out->month = dt_month(dt);
	out->week = 0;
	out->day = dt_dom(dt);
	out->hour = (secs / 3600) % 24;
	out->min = (secs / 60) % 60;
	out->sec = secs % 60;
	out->nsec = date->nsec;
	out->adjust = DT_LIMIT;

	return true;
}

/**
 * Interval support functions: stringization and operations
 */

#define NANOS_PER_SEC 1000000000LL

static inline bool
zero_or_same_sign(int64_t sec, int64_t nsec)
{
	return sec == 0 || (sec < 0) == (nsec < 0);
}

/**
 * If |nsec| is larger than allowed range 1e9 then modify passed
 * sec accordingly.
 */
static void
denormalize_interval_nsec(int64_t *psec, int *pnsec)
{
	assert(psec != NULL);
	assert(pnsec != NULL);
	int64_t sec = *psec;
	int nsec = *pnsec;
	/*
	 * There is nothing to change:
	 * - if there is a small nsec with 0 in sec;
	 * - or if both sec and nsec have the same sign, and nsec is
	 *   small enough.
	 */
	if (nsec == 0)
		return;

	if (zero_or_same_sign(sec, nsec) && abs(nsec) < NANOS_PER_SEC)
		return;

	sec += DIV(nsec, NANOS_PER_SEC);
	nsec = MOD(nsec, NANOS_PER_SEC);

	/*
	 * We crafted a positive nsec value, so convert it to negative, if secs
	 * are also negative.
	 */
	if (sec < 0) {
		sec++;
		nsec -= NANOS_PER_SEC;
	}

	*psec = sec;
	*pnsec = nsec;
}

#define SPACE() \
	do { \
		if (sz > 0) { \
			SNPRINT(sz, snprintf, buf, len, ", "); \
		} \
	} while (0)

size_t
interval_to_string(const struct interval *ival, char *buf, ssize_t len)
{
	static const char *const long_signed_fmt[] = {
		"%" PRId64,	/* false */
		"%+" PRId64,	/* true */
	};
	static const char *const signed_fmt[] = {
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
	if (ival->week != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, signed_fmt[need_sign],
			ival->week);
		SNPRINT(sz, snprintf, buf, len, " weeks");
		need_sign = false;
	}
	int64_t days = (int64_t)ival->day;
	if (days != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, long_signed_fmt[need_sign],
			days);
		SNPRINT(sz, snprintf, buf, len, " days");
		need_sign = false;
	}
	int64_t hours = (int64_t)ival->hour;
	if (ival->hour != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, long_signed_fmt[need_sign],
			hours);
		SNPRINT(sz, snprintf, buf, len, " hours");
		need_sign = false;
	}
	int64_t minutes = (int64_t)ival->min;
	if (minutes != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, long_signed_fmt[need_sign],
			minutes);
		SNPRINT(sz, snprintf, buf, len, " minutes");
		need_sign = false;
	}
	int64_t secs = (int64_t)ival->sec;
	int nsec = ival->nsec;
	if (secs == 0 && nsec == 0) {
		if (sz != 0)
			return sz;
		SNPRINT(sz, snprintf, buf, len, zero_secs);
		return sizeof(zero_secs) - 1;
	}
	SPACE();
	denormalize_interval_nsec(&secs, &nsec);
	bool is_neg = secs < 0 || (secs == 0 && nsec < 0);

	SNPRINT(sz, snprintf, buf, len, is_neg ? "-" : (need_sign ? "+" : ""));
	secs = labs(secs);
	if (nsec != 0) {
		SNPRINT(sz, snprintf, buf, len, "%" PRId64 ".%09d seconds",
			secs, abs(nsec));
	} else {
		SNPRINT(sz, snprintf, buf, len, "%" PRId64 " seconds", secs);
	}
	return sz;
}

/**
 * Normalize seconds and nanoseconds:
 * - make sure that nanoseconds part is positive
 * - make sure it's not exceeding maximum allowed value
 */
static void
normalize_nsec(int64_t *psecs, int *pnsec)
{
	assert(psecs != NULL);
	assert(pnsec != NULL);
	int64_t secs = *psecs;
	int nsec = *pnsec;

	if (nsec < 0 || nsec >= NANOS_PER_SEC) {
		secs += nsec / NANOS_PER_SEC;
		nsec %= NANOS_PER_SEC;
	}
	*psecs = secs;
	*pnsec = nsec;
}

static inline int64_t
utc_secs(int64_t epoch, int tzoffset)
{
	return epoch - tzoffset * 60;
}

/** minimum supported date - -5879610-06-22 */
#define MIN_DATE_YEAR -5879610
#define MIN_DATE_MONTH 6
#define MIN_DATE_DAY 22

/** maximum supported date - 5879611-07-11 */
#define MAX_DATE_YEAR 5879611
#define MAX_DATE_MONTH 7
#define MAX_DATE_DAY 11
/**
 * In the Julian calendar, the average year length is
 * 365 1/4 days = 365.25 days. This gives an error of
 * about 1 day in 128 years.
 */
#define AVERAGE_DAYS_YEAR 365.25
#define AVERAGE_DAYS_MONTH (AVERAGE_DAYS_YEAR / 12)
#define AVERAGE_WEEK_YEAR  (AVERAGE_DAYS_YEAR / 7)

static inline int
verify_range(int64_t v, int64_t from, int64_t to)
{
	return (v < from) ? -1 : (v > to ? +1 : 0);
}

static inline int
verify_dt(int64_t dt)
{
	return verify_range(dt, INT_MIN, INT_MAX);
}

int
datetime_increment_by(struct datetime *self, int direction,
		      const struct interval *ival)
{
	int64_t secs = local_secs(self);
	int64_t dt = local_dt(secs);
	int nsec = self->nsec;
	int offset = self->tzindex;

	bool is_ymd_updated = false;
	int64_t years = ival->year;
	int64_t months = ival->month;
	int64_t weeks = ival->week;
	int64_t days = ival->day;
	int64_t hours = ival->hour;
	int64_t minutes = ival->min;
	int64_t seconds = ival->sec;
	int nanoseconds = ival->nsec;
	dt_adjust_t adjust = ival->adjust;
	int rc = 0;

	if (years != 0) {
		rc = verify_dt(dt + direction * years * AVERAGE_DAYS_YEAR);
		if (rc != 0)
			return rc;
		/* tnt_dt_add_years() not handle properly DT_SNAP or DT_LIMIT
		 * mode so use tnt_dt_add_months() as a work-around
		 */
		dt = dt_add_months(dt, direction * years * 12, adjust);
		is_ymd_updated = true;
	}
	if (months != 0) {
		rc = verify_dt(dt + direction * months * AVERAGE_DAYS_MONTH);
		if (rc != 0)
			return rc;

		dt = dt_add_months(dt, direction * months, adjust);
		is_ymd_updated = true;
	}
	if (weeks != 0) {
		rc = verify_dt(dt + direction * weeks * 7);
		if (rc != 0)
			return rc;

		dt += direction * weeks * 7;
		is_ymd_updated = true;
	}
	if (days != 0) {
		rc = verify_dt(dt + direction * days);
		if (rc != 0)
			return rc;

		dt += direction * days;
		is_ymd_updated = true;
	}

	if (is_ymd_updated) {
		secs = dt * SECS_PER_DAY - SECS_EPOCH_1970_OFFSET +
		       secs % SECS_PER_DAY;
	}

	if (hours != 0) {
		rc = verify_range(secs + direction * hours * 3600,
				  MIN_EPOCH_SECS_VALUE, MAX_EPOCH_SECS_VALUE);
		if (rc != 0)
			return rc;

		secs += direction * hours * 3600;
	}
	if (minutes != 0) {
		rc = verify_range(secs + direction * minutes * 60,
				  MIN_EPOCH_SECS_VALUE, MAX_EPOCH_SECS_VALUE);
		if (rc != 0)
			return rc;

		secs += direction * minutes * 60;
	}
	if (seconds != 0) {
		rc = verify_range(secs + direction * seconds,
				  MIN_EPOCH_SECS_VALUE, MAX_EPOCH_SECS_VALUE);
		if (rc != 0)
			return rc;

		secs += direction * seconds;
	}
	if (nanoseconds != 0)
		nsec += direction * nanoseconds;

	normalize_nsec(&secs, &nsec);
	rc = verify_dt((secs + SECS_EPOCH_1970_OFFSET) / SECS_PER_DAY);
	if (rc != 0)
		return rc;

	self->epoch = utc_secs(secs, offset);
	self->nsec = nsec;
	return 0;
}

bool
datetime_datetime_sub(struct interval *res, const struct datetime *lhs,
		      const struct datetime *rhs)
{
	assert(res != NULL);
	assert(lhs != NULL);
	assert(rhs != NULL);
	struct interval inv_rhs;
	datetime_totable(lhs, res);
	datetime_totable(rhs, &inv_rhs);
	return interval_interval_sub(res, &inv_rhs);
}

bool
interval_interval_sub(struct interval *lhs, const struct interval *rhs)
{
	assert(lhs != NULL);
	assert(rhs != NULL);
	lhs->year -= rhs->year;
	lhs->month -= rhs->month;
	lhs->week -= rhs->week;
	lhs->day -= rhs->day;
	lhs->hour -= rhs->hour;
	lhs->min -= rhs->min;
	lhs->sec -= rhs->sec;
	lhs->nsec -= rhs->nsec;
	return true;
}

bool
interval_interval_add(struct interval *lhs, const struct interval *rhs)
{
	assert(lhs != NULL);
	assert(rhs != NULL);
	lhs->year += rhs->year;
	lhs->month += rhs->month;
	lhs->day += rhs->day;
	lhs->week += rhs->week;
	lhs->hour += rhs->hour;
	lhs->min += rhs->min;
	lhs->sec += rhs->sec;
	lhs->nsec += rhs->nsec;
	return true;
}
