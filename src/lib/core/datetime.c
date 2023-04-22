/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <math.h>
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>

#define DT_PARSE_ISO_TNT
#include "decimal.h"
#include "msgpuck.h"
#include "c-dt/dt.h"
#include "datetime.h"
#include "trivia/util.h"
#include "tzcode/tzcode.h"
#include "tzcode/timezone.h"
#include "mp_extension_types.h"

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

/**
 * Resolve tzindex encoded timezone from @sa date using Olson facilities.
 * @param[in] epoch decode input epoch time (in seconds).
 * @param[in] tzindex use timezone index for decode.
 * @param[out] gmtoff return resolved timezone offset (in seconds).
 * @param[out] isdst return resolved daylight saving time status for the zone.
 */
static inline bool
epoch_timezone_lookup(int64_t epoch, int16_t tzindex, long *gmtoff, int *isdst)
{
	if (tzindex == 0)
		return false;

	struct tnt_tm tm = {.tm_epoch = epoch};
	if (!timezone_tzindex_lookup(tzindex, &tm))
		return false;

	*gmtoff = tm.tm_gmtoff;
	*isdst = tm.tm_isdst;

	return true;
}

bool
datetime_isdst(const struct datetime *date)
{
	int isdst = 0;
	long gmtoff = 0;

	epoch_timezone_lookup(date->epoch, date->tzindex, &gmtoff, &isdst);
	return isdst != 0;
}

long
datetime_gmtoff(const struct datetime *date)
{
	int isdst = 0;
	long gmtoff = date->tzoffset * 60;

	epoch_timezone_lookup(date->epoch, date->tzindex, &gmtoff, &isdst);
	return gmtoff;
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
	tm->tm_tzindex = date->tzindex;
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
	date->tzindex = tm->tm_tzindex;
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
	now->nsec = (timestamp - sec) * NANOS_PER_SEC;

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
	int tzindex = date->tzindex;
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
	if (tzindex != 0) {
		const char *tz_name = timezone_name(tzindex);
		assert(tz_name != NULL);
		SNPRINT(sz, snprintf, buf, len,
			tz_name[1] == '\0' ? "%s" : " %s",
			tz_name);
	} else if (offset == 0) {
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

static inline int64_t
dt_epoch(dt_t dt)
{
	return ((int64_t)dt_rdn(dt) - DT_EPOCH_1970_OFFSET) * SECS_PER_DAY;
}

/** Common timezone suffix parser */
static inline ssize_t
parse_tz_suffix(const char *str, size_t len, time_t base,
		int16_t *tzindex, int32_t *offset)
{
	/* 1st attempt: decode as MSK */
	const struct date_time_zone *zone;
	long gmtoff = 0;
	ssize_t l = timezone_epoch_lookup(str, len, base, &zone, &gmtoff);
	if (l < 0)
		return l;
	if (l > 0) {
		assert(zone != NULL);
		*offset = gmtoff / 60;
		*tzindex = timezone_index(zone);
		assert(l <= (ssize_t)len);
		return l;
	}

	/* 2nd attempt: decode as +03:00 */
	*tzindex = 0;
	l = dt_parse_iso_zone_lenient(str, len, offset);
	assert(l <= (ssize_t)len);

	return l;
}

ssize_t
datetime_parse_full(struct datetime *date, const char *str, size_t len,
		    const char *tzsuffix, int32_t offset)
{
	size_t n;
	dt_t dt;
	const char *svp = str;
	char c;
	int sec_of_day = 0, nanosecond = 0;
	int16_t tzindex = 0;

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

	/* now we have parsed enough of date literal, and we are
	 * ready to consume timezone suffix, if overridden
	 */
	time_t base = dt_epoch(dt) + sec_of_day - offset * 60;
	ssize_t l;
	if (tzsuffix != NULL) {
		l = parse_tz_suffix(tzsuffix, strlen(tzsuffix), base,
				    &tzindex, &offset);
		if (l < 0)
			return l;
		goto exit;
	}

	if (*str == ' ') {
		str++;
		len--;
	}
	if (len <= 0)
		goto exit;

	l = parse_tz_suffix(str, len, base, &tzindex, &offset);
	if (l < 0)
		return l;
	str += l;

exit:
	date->epoch = dt_epoch(dt) + sec_of_day - offset * 60;
	date->nsec = nanosecond;
	date->tzoffset = offset;
	date->tzindex = tzindex;

	return str - svp;
}

ssize_t
datetime_parse_tz(const char *str, size_t len, time_t base, int16_t *tzoffset,
		  int16_t *tzindex)
{
	int32_t offset = 0;
	ssize_t l = parse_tz_suffix(str, len, base, tzindex, &offset);
	if (l <= 0)
		return l;
	assert(offset <= INT16_MAX);
	*tzoffset = offset;
	return l;
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

	size_t sz = 0;
	if (ival->year != 0) {
		SNPRINT(sz, snprintf, buf, len, "%+d years", ival->year);
	}
	if (ival->month != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, signed_fmt[sz == 0],
			ival->month);
		SNPRINT(sz, snprintf, buf, len, " months");
	}
	if (ival->week != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, signed_fmt[sz == 0],
			ival->week);
		SNPRINT(sz, snprintf, buf, len, " weeks");
	}
	int64_t days = (int64_t)ival->day;
	if (days != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, long_signed_fmt[sz == 0],
			days);
		SNPRINT(sz, snprintf, buf, len, " days");
	}
	int64_t hours = (int64_t)ival->hour;
	if (ival->hour != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, long_signed_fmt[sz == 0],
			hours);
		SNPRINT(sz, snprintf, buf, len, " hours");
	}
	int64_t minutes = (int64_t)ival->min;
	if (minutes != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, long_signed_fmt[sz == 0],
			minutes);
		SNPRINT(sz, snprintf, buf, len, " minutes");
	}

	int64_t secs = (int64_t)ival->sec;
	if (secs != 0 || sz == 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, long_signed_fmt[sz == 0],
			secs);
		SNPRINT(sz, snprintf, buf, len, " seconds");
	}
	int32_t nsec = ival->nsec;
	if (nsec != 0) {
		SPACE();
		SNPRINT(sz, snprintf, buf, len, signed_fmt[sz == 0],
			nsec);
		SNPRINT(sz, snprintf, buf, len, " nanoseconds");
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
		if (nsec < 0) {
			secs -= 1;
			nsec += NANOS_PER_SEC;
		}
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
#define MIN_DATE_YEAR -5879610LL
#define MIN_DATE_MONTH 6
#define MIN_DATE_DAY 22

/** maximum supported date - 5879611-07-11 */
#define MAX_DATE_YEAR 5879611LL
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

#define MAX_YEAR_RANGE (MAX_DATE_YEAR - MIN_DATE_YEAR)
#define MAX_MONTH_RANGE (MAX_YEAR_RANGE * 12)
#define MAX_WEEK_RANGE (MAX_YEAR_RANGE * AVERAGE_WEEK_YEAR)
#define MAX_DAY_RANGE (MAX_YEAR_RANGE * AVERAGE_DAYS_YEAR)
#define MAX_HOUR_RANGE (MAX_DAY_RANGE * 24)
#define MAX_MIN_RANGE (MAX_HOUR_RANGE * 60)
#define MAX_SEC_RANGE (MAX_DAY_RANGE * SECS_PER_DAY)
#define MAX_NSEC_RANGE ((int64_t)INT_MAX)

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
	int offset = self->tzoffset;
	int tzindex = self->tzindex;

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

	if (tzindex != 0) {
		int isdst = 0;
		long gmtoff = offset * 60;
		epoch_timezone_lookup(secs, tzindex, &gmtoff, &isdst);
		offset = gmtoff / 60;
	}
	self->epoch = utc_secs(secs, offset);
	self->nsec = nsec;
	self->tzoffset = offset;
	return 0;
}

/**
 * Check attributes of interval record after prior operation
 */
static int
interval_check_args(const struct interval *ival)
{
	int rc = verify_range(ival->year, -MAX_YEAR_RANGE, MAX_YEAR_RANGE);
	if (rc != 0)
		return rc * CHECK_YEARS;
	rc = verify_range(ival->month, -MAX_MONTH_RANGE, MAX_MONTH_RANGE);
	if (rc != 0)
		return rc * CHECK_MONTHS;
	rc = verify_range(ival->week, -MAX_WEEK_RANGE, MAX_WEEK_RANGE);
	if (rc != 0)
		return rc * CHECK_WEEKS;
	rc = verify_range(ival->day, -MAX_DAY_RANGE, MAX_DAY_RANGE);
	if (rc != 0)
		return rc * CHECK_DAYS;
	rc = verify_range(ival->hour, -MAX_HOUR_RANGE, MAX_HOUR_RANGE);
	if (rc != 0)
		return rc * CHECK_HOURS;
	rc = verify_range(ival->min, -MAX_MIN_RANGE, MAX_MIN_RANGE);
	if (rc != 0)
		return rc * CHECK_MINUTES;
	rc = verify_range(ival->sec, -MAX_SEC_RANGE, MAX_SEC_RANGE);
	if (rc != 0)
		return rc * CHECK_SECONDS;
	return verify_range(ival->nsec, -MAX_NSEC_RANGE, MAX_NSEC_RANGE) *
		CHECK_NANOSECS;
}

int
datetime_datetime_sub(struct interval *res, const struct datetime *lhs,
		      const struct datetime *rhs)
{
	assert(res != NULL);
	assert(lhs != NULL);
	assert(rhs != NULL);
	struct interval inv_rhs;
	datetime_totable(lhs, res);
	datetime_totable(rhs, &inv_rhs);
	res->min -= lhs->tzoffset - rhs->tzoffset;
	return interval_interval_sub(res, &inv_rhs);
}

int
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
	return interval_check_args(lhs);
}

int
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
	return interval_check_args(lhs);
}

/** This structure contains information about the given date and time fields. */
struct dt_fields {
	/* Specified year. */
	double year;
	/* Specified month. */
	double month;
	/* Specified day. */
	double day;
	/* Specified hour. */
	double hour;
	/* Specified minute. */
	double min;
	/* Specified second. */
	double sec;
	/* Specified millisecond. */
	double msec;
	/* Specified microsecond. */
	double usec;
	/* Specified nanosecond. */
	double nsec;
	/* Specified timestamp. */
	double timestamp;
	/* Specified timezone offset. */
	double tzoffset;
	/* Number of given fields among msec, usec and nsec. */
	int count_usec;
	/* True, if any of year, month, day, hour, min or sec is specified. */
	bool is_ymdhms;
	/* True, if timestamp is specified. */
	bool is_ts;
};

/** Parse msgpack value and convert it to double, if possible. */
static int
get_double_from_mp(const char **data, double *value)
{
	switch (mp_typeof(**data)) {
	case MP_INT:
		*value = mp_decode_int(data);
		break;
	case MP_UINT:
		*value = mp_decode_uint(data);
		break;
	case MP_DOUBLE:
		*value = mp_decode_double(data);
		break;
	case MP_EXT: {
		int8_t type;
		uint32_t len = mp_decode_extl(data, &type);
		if (type != MP_DECIMAL)
			return -1;
		decimal_t dec;
		if (decimal_unpack(data, len, &dec) == NULL)
			return -1;
		*value = atof(decimal_str(&dec));
		break;
	}
	default:
		return -1;
	}
	return 0;
}

/** Parse msgpack value and convert it to int32, if possible. */
static int
get_int32_from_mp(const char **data, int32_t *value)
{
	switch (mp_typeof(**data)) {
	case MP_INT: {
		int64_t val = mp_decode_int(data);
		if (val < INT32_MIN)
			return -1;
		*value = val;
		break;
	}
	case MP_UINT: {
		uint64_t val = mp_decode_uint(data);
		if (val > INT32_MAX)
			return -1;
		*value = val;
		break;
	}
	case MP_DOUBLE: {
		double val = mp_decode_double(data);
		if (val > (double)INT32_MAX || val < (double)INT32_MIN)
			return -1;
		if (val != floor(val))
			return -1;
		*value = val;
		break;
	}
	case MP_EXT: {
		int8_t type;
		uint32_t len = mp_decode_extl(data, &type);
		if (type != MP_DECIMAL)
			return -1;
		decimal_t dec;
		if (decimal_unpack(data, len, &dec) == NULL)
			return -1;
		if (!decimal_is_int(&dec))
			return -1;
		int64_t val;
		if (decimal_to_int64(&dec, &val) == NULL)
			return -1;
		if (val < INT32_MIN || val > INT32_MAX)
			return -1;
		*value = val;
		break;
	}
	default:
		return -1;
	}
	return 0;
}

/** Define field of DATETIME value from field of given MAP value.*/
static int
map_field_to_dt_field(struct dt_fields *fields, const char **data)
{
	if (mp_typeof(**data) != MP_STR) {
		mp_next(data);
		mp_next(data);
		return 0;
	}
	uint32_t size;
	const char *str = mp_decode_str(data, &size);
	double *value;
	if (strncmp(str, "year", size) == 0) {
		value = &fields->year;
		fields->is_ymdhms = true;
	} else if (strncmp(str, "month", size) == 0) {
		value = &fields->month;
		fields->is_ymdhms = true;
	} else if (strncmp(str, "day", size) == 0) {
		value = &fields->day;
		fields->is_ymdhms = true;
	} else if (strncmp(str, "hour", size) == 0) {
		value = &fields->hour;
		fields->is_ymdhms = true;
	} else if (strncmp(str, "min", size) == 0) {
		value = &fields->min;
		fields->is_ymdhms = true;
	} else if (strncmp(str, "sec", size) == 0) {
		value = &fields->sec;
		fields->is_ymdhms = true;
	} else if (strncmp(str, "msec", size) == 0) {
		value = &fields->msec;
		++fields->count_usec;
	} else if (strncmp(str, "usec", size) == 0) {
		value = &fields->usec;
		++fields->count_usec;
	} else if (strncmp(str, "nsec", size) == 0) {
		value = &fields->nsec;
		++fields->count_usec;
	} else if (strncmp(str, "timestamp", size) == 0) {
		value = &fields->timestamp;
		fields->is_ts = true;
	} else if (strncmp(str, "tzoffset", size) == 0) {
		value = &fields->tzoffset;
	} else {
		mp_next(data);
		return 0;
	}
	return get_double_from_mp(data, value);
}

/** Create a DATETIME value using fields of the DATETIME. */
static int
datetime_from_fields(struct datetime *dt, const struct dt_fields *fields)
{
	if (fields->count_usec > 1)
		return -1;
	double nsec = fields->msec * 1000000 + fields->usec * 1000 +
		      fields->nsec;
	if (nsec < 0 || nsec >= MAX_NANOS_PER_SEC)
		return -1;
	if (fields->tzoffset < -720 || fields->tzoffset > 840)
		return -1;
	if (fields->timestamp < (double)INT32_MIN * SECS_PER_DAY ||
	    fields->timestamp > (double)INT32_MAX * SECS_PER_DAY)
		return -1;
	if (fields->is_ts) {
		if (fields->is_ymdhms)
			return -1;
		double timestamp = floor(fields->timestamp);
		double frac = fields->timestamp - timestamp;
		if (frac != 0) {
			if (fields->count_usec > 0)
				return -1;
			nsec = frac * NANOS_PER_SEC;
		}
		dt->epoch = timestamp;
		dt->nsec = nsec;
		dt->tzoffset = fields->tzoffset;
		dt->tzindex = 0;
		return 0;
	}
	if (fields->year < MIN_DATE_YEAR || fields->year > MAX_DATE_YEAR)
		return -1;
	if (fields->month < 1 || fields->month > 12)
		return -1;
	if (fields->day < 1 ||
	    fields->day > dt_days_in_month(fields->year, fields->month))
		return -1;
	if (fields->hour < 0 || fields->hour > 23)
		return -1;
	if (fields->min < 0 || fields->min > 59)
		return -1;
	if (fields->sec < 0 || fields->sec > 60)
		return -1;
	double days = dt_from_ymd(fields->year, fields->month, fields->day) -
		      DT_EPOCH_1970_OFFSET;
	dt->epoch = days * SECS_PER_DAY + fields->hour * 3600 +
		    fields->min * 60 + fields->sec;
	dt->nsec = nsec;
	dt->tzoffset = fields->tzoffset;
	dt->tzindex = 0;
	return 0;
}

int
datetime_from_map(struct datetime *dt, const char *data)
{
	assert(mp_typeof(*data) == MP_MAP);
	uint32_t len = mp_decode_map(&data);
	struct dt_fields fields;
	memset(&fields, 0, sizeof(fields));
	fields.year = 1970;
	fields.month = 1;
	fields.day = 1;
	for (uint32_t i = 0; i < len; ++i) {
		if (map_field_to_dt_field(&fields, &data) != 0)
			return -1;
	}
	return datetime_from_fields(dt, &fields);
}

/** Define field of INTERVAL value from field of given MAP value.*/
static int
map_field_to_itv_field(struct interval *itv, const char **data)
{
	if (mp_typeof(**data) != MP_STR) {
		mp_next(data);
		mp_next(data);
		return 0;
	}
	uint32_t size;
	const char *str = mp_decode_str(data, &size);
	double *dvalue = NULL;
	int32_t *ivalue = NULL;
	if (strncmp(str, "year", size) == 0) {
		ivalue = &itv->year;
	} else if (strncmp(str, "month", size) == 0) {
		ivalue = &itv->month;
	} else if (strncmp(str, "week", size) == 0) {
		ivalue = &itv->week;
	} else if (strncmp(str, "day", size) == 0) {
		dvalue = &itv->day;
	} else if (strncmp(str, "hour", size) == 0) {
		dvalue = &itv->hour;
	} else if (strncmp(str, "min", size) == 0) {
		dvalue = &itv->min;
	} else if (strncmp(str, "sec", size) == 0) {
		dvalue = &itv->sec;
	} else if (strncmp(str, "nsec", size) == 0) {
		ivalue = &itv->nsec;
	} else if (strncmp(str, "adjust", size) == 0) {
		if (mp_typeof(**data) != MP_STR)
			return -1;
		uint32_t vsize;
		const char *val = mp_decode_str(data, &vsize);
		if (strncasecmp(val, "none", vsize) == 0)
			itv->adjust = DT_LIMIT;
		else if (strncasecmp(val, "last", vsize) == 0)
			itv->adjust = DT_SNAP;
		else if (strncasecmp(val, "excess", vsize) == 0)
			itv->adjust = DT_EXCESS;
		else
			return -1;
		return 0;
	} else {
		mp_next(data);
		return 0;
	}
	if (dvalue != NULL) {
		double val;
		if (get_double_from_mp(data, &val) != 0)
			return -1;
		if (val != floor(val))
			return -1;
		*dvalue = val;
		return 0;
	}
	assert(ivalue != NULL);
	return get_int32_from_mp(data, ivalue);
}

int
interval_from_map(struct interval *itv, const char *data)
{
	assert(mp_typeof(*data) == MP_MAP);
	uint32_t len = mp_decode_map(&data);
	memset(itv, 0, sizeof(*itv));
	itv->adjust = DT_LIMIT;
	for (uint32_t i = 0; i < len; ++i) {
		if (map_field_to_itv_field(itv, &data) != 0)
			return -1;
	}
	return interval_check_args(itv) == 0 ? 0 : -1;
}
