#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <limits.h>
#include <stdint.h>
#include <sys/types.h>
#include "c-dt/dt.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tnt_tm;

/**
 * We count dates since so called "Rata Die" date
 * January 1, 0001, Monday (as Day 1).
 * But datetime structure keeps seconds since
 * Unix "Epoch" date:
 * Unix, January 1, 1970, Thursday
 *
 * The difference between Epoch (1970-01-01)
 * and Rata Die (0001-01-01) is 719163 days.
 */

#ifndef SECS_PER_DAY
#define SECS_PER_DAY          86400
#define DT_EPOCH_1970_OFFSET  719163
#endif

/** Required size of datetime_to_string string buffer */
#define DT_TO_STRING_BUFSIZE 48

/**
 * Required buffer size to store the string representation of any possible
 * interval.
 */
#define DT_IVAL_TO_STRING_BUFSIZE 256

/**
 * c-dt library uses int as type for dt value, which
 * represents the number of days since Rata Die date.
 * This implies limits to the number of seconds we
 * could safely store in our structures and then safely
 * pass to c-dt functions.
 *
 * So supported ranges will be
 * - for seconds [-185604722870400 .. 185480451417600]
 * - for dates   [-5879610-06-22T00:00Z .. 5879611-07-11T00:00Z]
 */
#define MAX_DT_DAY_VALUE (int64_t)INT_MAX
#define MIN_DT_DAY_VALUE (int64_t)INT_MIN
#define SECS_EPOCH_1970_OFFSET 	((int64_t)DT_EPOCH_1970_OFFSET * SECS_PER_DAY)
#define MAX_EPOCH_SECS_VALUE    \
	(MAX_DT_DAY_VALUE * SECS_PER_DAY - SECS_EPOCH_1970_OFFSET)
#define MIN_EPOCH_SECS_VALUE    \
	(MIN_DT_DAY_VALUE * SECS_PER_DAY - SECS_EPOCH_1970_OFFSET)

/**
 * datetime structure keeps number of seconds and
 * nanoseconds since Unix Epoch.
 * Time is normalized by UTC, so time-zone offset
 * is informative only.
 */
struct datetime {
	/** Seconds since Epoch. */
	double epoch;
	/** Nanoseconds, if any. */
	int32_t nsec;
	/** Offset in minutes from UTC. */
	int16_t tzoffset;
	/** Olson timezone id */
	int16_t tzindex;
};

/**
 * To be able to perform arithmetics on time intervals and receive
 * deterministic results, we keep each component (i.e. years, months, weeks,
 * days, etc) separately from seconds.
 * We add/subtract interval components separately, and rebase upon resultant
 * date only at the moment when we apply them to the datetime object, at this
 * time all leap seconds/leap year logic taken into consideration.
 * Datetime supports range of values -5879610..5879611, thus interval should be
 * able to support positive and negative increments covering full distance from
 * minimum to maximum i.e. 11759221. Such years, months, and weeks values might
 * be handled by 32-bit integer, but should be extended to 64-bit once we need
 * to handle corresponding days, hours and seconds values.
 */
struct interval {
	/** Duration in seconds. */
	double sec;
	/** Number of minutes, if specified. */
	double min;
	/** Number of hours, if specified. */
	double hour;
	/** Number of days, if specified. */
	double day;
	/** Number of weeks, if specified. */
	int32_t week;
	/** Number of months, if specified. */
	int32_t month;
	/** Number of years, if specified. */
	int32_t year;
	/** Fraction part of duration in seconds. */
	int32_t nsec;
	/** Adjustment mode for day in month operations, @sa dt_adjust_t */
	dt_adjust_t adjust;
};

/*
 * Compare arguments of a datetime type
 * @param lhs left datetime argument
 * @param rhs right datetime argument
 * @retval < 0 if lhs less than rhs
 * @retval = 0 if lhs and rhs equal
 * @retval > 0 if lhs greater than rhs
 */
int
datetime_compare(const struct datetime *lhs, const struct datetime *rhs);

/**
 * Convert datetime to string using default format
 * @param date source datetime value
 * @param buf output character buffer
 * @param len size of output buffer
 * @retval length of a resultant text
 */
size_t
datetime_to_string(const struct datetime *date, char *buf, ssize_t len);

/**
 * Convert datetime to string using default format provided
 * Wrapper around standard strftime() function
 * @param date source datetime value
 * @param buf output buffer
 * @param len size of output buffer
 * @param fmt format
 * @retval length of a resultant text
 */
size_t
datetime_strftime(const struct datetime *date, char *buf, size_t len,
		      const char *fmt);

void
datetime_now(struct datetime *now);

/** Return current date and time. Cheaper version of datetime_now(). */
void
datetime_ev_now(struct datetime *now);

/**
 * Convert @sa datetime to @sa dt_tm
 */
void
datetime_to_tm(const struct datetime *date, struct tnt_tm *tm);

/**
 * Parse datetime text in ISO-8601 given format, and construct output
 * datetime value
 * @param date output datetime value
 * @param str input text in relaxed ISO-8601 format (0-terminated)
 * @param len length of str buffer
 * @param offset timezone offset, if you need to interpret
 *        date/time value as local. Pass 0 if you need to interpret it as UTC,
 *        or apply offset from string.
 * @retval Upon successful completion returns length of accepted
 *         prefix substring. It's ok if there is some unaccepted trailer.
 *         Returns 0 only if text is not recognizable as date/time string.
 *         Returns negative value is there is unaccepted timezone.
 * @sa datetime_strptime()
 */
ssize_t
datetime_parse_full(struct datetime *date, const char *str, size_t len,
		    int32_t offset);
/**
 * Parse buffer given format, and construct datetime value
 * @param date output datetime value
 * @param buf input text buffer (0-terminated)
 * @param fmt format to use for parsing
 * @retval Upon successful completion returns length of accepted
 *         prefix substring, 0 otherwise.
 * @sa strptime()
 */
size_t
datetime_strptime(struct datetime *date, const char *buf, const char *fmt);

/**
 * Decompose datetime into components
 * @param[in] date origonal datetime value to decompose
 * @param[out] out resultant time components holder
 */
bool
datetime_totable(const struct datetime *date, struct interval *out);

/**
 * Convert datetime interval to string using default format
 * @param ival source interval value
 * @param buf output buffer
 * @param len size of output buffer
 * @retval length of resultant text
 */
size_t
interval_to_string(const struct interval *ival, char *buf, ssize_t len);

/**
 * Add/subtract datetime value by passed interval using direction as a hint
 * @param[in,out] self left source operand and target for binary operation
 * @param[in] direction addition (0) or subtraction (-1)
 * @param[in] ival interval objects (right operand)
 * @retval 0 if there is no overflow, negative value if there is underflow
 *         or positive if there is overflow.
 */
int
datetime_increment_by(struct datetime *self, int direction,
		      const struct interval *ival);

/**
 * Error code multipliers for interval operations
 */
enum check_attr_multiplier {
	CHECK_YEARS = 1,
	CHECK_MONTHS = 2,
	CHECK_WEEKS = 3,
	CHECK_DAYS = 4,
	CHECK_HOURS = 5,
	CHECK_MINUTES = 6,
	CHECK_SECONDS = 7,
	CHECK_NANOSECS = 8,
};

/**
 * Subtract datetime values and return interval between epoch seconds
 * @param[out] res resultant interval
 * @param[in] lhs left datetime operand
 * @param[in] rhs right datetime operand
 * @retval 0 if there is no overflow, negative value if there is underflow
 *         in particular attribute, or positive if there is overflow.
 *         Return value is multiplied by @sa check_attr_multiplier to
 *         indicate which particular attributed is overflowed/underflowed.
 */
int
datetime_datetime_sub(struct interval *res, const struct datetime *lhs,
		      const struct datetime *rhs);

/**
 * Subtract interval values, modify left operand for returning value
 * @param[in,out] lhs left interval operand, return resultant value
 * @param[in] rhs right interval operand
 * @retval 0 if there is no overflow, negative value if there is underflow
 *         in particular attribute, or positive if there is overflow.
 *         Return value is multiplied by @sa check_attr_multiplier to
 *         indicate which particular attributed is overflowed/underflowed.
 */
int
interval_interval_sub(struct interval *lhs, const struct interval *rhs);

/**
 * Add interval values, modify left operand for returning value
 * @param[in,out] lhs left interval operand, return resultant value
 * @param[in] rhs right interval operand
 * @retval 0 if there is no overflow, negative value if there is underflow
 *         in particular attribute, or positive if there is overflow.
 *         Return value is multiplied by @sa check_attr_multiplier to
 *         indicate which particular attributed is overflowed/underflowed.
 */
int
interval_interval_add(struct interval *lhs, const struct interval *rhs);

/** Return the year from a date. */
int64_t
datetime_year(const struct datetime *date);

/** Return the month of year from a date. */
int64_t
datetime_month(const struct datetime *date);

/** Return the quarter of year from a date. */
int64_t
datetime_quarter(const struct datetime *date);

/** Return the week of year from a date. */
int64_t
datetime_week(const struct datetime *date);

/** Return the day of month from a date. */
int64_t
datetime_day(const struct datetime *date);

/** Return the day of week from a date. */
int64_t
datetime_dow(const struct datetime *date);

/** Return the day of year from a date. */
int64_t
datetime_doy(const struct datetime *date);

/** Return the hour of day from a date. */
int64_t
datetime_hour(const struct datetime *date);

/** Return the minute of hour from a date. */
int64_t
datetime_min(const struct datetime *date);

/** Return the second of minute from a date. */
int64_t
datetime_sec(const struct datetime *date);

/** Return the timezone offset from a date. */
int64_t
datetime_tzoffset(const struct datetime *date);

/** Return the epoch from a date. */
int64_t
datetime_epoch(const struct datetime *date);

/** Return the nanosecond of second from a date. */
int64_t
datetime_nsec(const struct datetime *date);

/** Return the millennium from a date. */
static inline int64_t
datetime_millennium(const struct datetime *date)
{
	int64_t year = datetime_year(date);
	if (year > 0)
		return (year - 1) / 1000 + 1;
	return year / 1000 - 1;
}

/** Return the century from a date. */
static inline int64_t
datetime_century(const struct datetime *date)
{
	int64_t year = datetime_year(date);
	if (year > 0)
		return (year - 1) / 100 + 1;
	return year / 100 - 1;
}

/** Return the decade from a date. */
static inline int64_t
datetime_decade(const struct datetime *date)
{
	int64_t year = datetime_year(date);
	if (year > 0)
		return year / 10;
	return year / 10 - 1;
}

/** Return the millisecond of second from a date. */
static inline int64_t
datetime_msec(const struct datetime *date)
{
	return datetime_nsec(date) / 1000000;
}

/** Return the microsecond of second from a date. */
static inline int64_t
datetime_usec(const struct datetime *date)
{
	return datetime_nsec(date) / 1000;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
