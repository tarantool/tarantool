#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

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
 * deterministic results, we have to keep months and years separately
 * from seconds.
 * Weeks, days, hours and minutes, all could be _precisely_ converted
 * to seconds, but it's not the case for months (which might be 28, 29,
 * 30, or 31 days long), or years (which could be leap year or not).
 * Approach used here - to add/subtract months or years intervals only
 * at the moment when we have particular date we operate on.
 * Determinism of results is achieved due to the order we apply
 * operations (from larger to smaller quantities):
 * - years, then months, then weeks, days, hours, minutes,
 *   seconds, and nanoseconds.
*/
struct datetime_interval {
	double sec;
	int nsec;
	int month;
	int year;
	int /*dt_adjust_t*/ adjust;
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
 * @sa datetime_strptime()
 */
size_t
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

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
