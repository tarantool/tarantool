/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>
#include <string.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	/**
	 * The length of the buffer sufficient to contain the contents of any
	 * string representation of value of struct interval.
	 */
	INTERVAL_STR_MAX_LEN = 256,
};

/**
 * Type of adjust of date interval.
 *
 * In some cases, when the original date is increased or decreased by years or
 * months, the result date may not have the same number as the original date.
 * For example, when 1 year is added to February 29 or 1 month is subtracted
 * from December 31. This option tells what the result will be in such cases.
 */
enum adjust {
	/** See the description of DT_EXCESS in c-dt. */
	INTERVAL_ADJUST_EXCESS,
	/** See the description of DT_LIMIT in c-dt. */
	INTERVAL_ADJUST_LIMIT,
	/** See the description of DT_SNAP in c-dt. */
	INTERVAL_ADJUST_SNAP,
};

/** A structure that describes a date and time interval. */
struct interval {
	/** Number of years. */
	int32_t year;
	/** Number of months. */
	int32_t month;
	/** Number of weeks. */
	int32_t week;
	/** Number of days. */
	int32_t day;
	/** Number of hours. */
	int32_t hour;
	/** Number of minutes. */
	int32_t min;
	/** Number of seconds. */
	int32_t sec;
	/** Number of nanoseconds. */
	int32_t nsec;
	/** Type of adjust. */
	enum adjust adjust;
};

/** Initialize the interval with the default value. */
static inline void
interval_create(struct interval *itv)
{
	memset(itv, 0, sizeof(*itv));
}

/** Write the interval as a string into the passed buffer. */
void
interval_to_string(const struct interval *itv, char *out, size_t size);

/**
 * Write the interval to a string.
 *
 * Returns a statically allocated buffer containing the interval representation.
 */
char *
interval_str(const struct interval *itv);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
