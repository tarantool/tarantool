#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Local version resembling ISO C' tm structure.
 * Includes original epoch value, and nanoseconds.
 */
struct tnt_tm {
	/** Seconds. [0-60] (1 leap second) */
	int tm_sec;
	/** Minutes. [0-59] */
	int tm_min;
	/** Hours. [0-23] */
	int tm_hour;
	/** Day. [1-31] */
	int tm_mday;
	/** Month. [0-11] */
	int tm_mon;
	/** Year - 1900. */
	int tm_year;
	/** Day of week. [0-6] */
	int tm_wday;
	/** Days in year.[0-365] */
	int tm_yday;
	/** DST. [-1/0/1] */
	int tm_isdst;

	/** Seconds east of UTC. */
	long int tm_gmtoff;
	/** Timezone abbreviation. */
	const char *tm_zone;
	/** Seconds since Epoch */
	int64_t tm_epoch;
	/** nanoseconds */
	int tm_nsec;
};

/**
 * tnt_strftime is Tarantool version of a POSIX' strftime()
 * which has been extended with %f (fractions of second)
 * flag support. In all other aspect it's behaving exactly
 * like standard strftime.
 * @sa strftime()
 */
size_t
tnt_strftime(char *s, size_t maxsize, const char *format,
	     const struct tnt_tm *tm);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
