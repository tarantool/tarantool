#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "c-dt/dt.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tnt_tm;

enum {
	TZ_UTC = 0x01,
	TZ_RFC = 0x02,
	TZ_MILITARY = 0x04,
	TZ_AMBIGUOUS = 0x08,
	TZ_NYI = 0x10,
	TZ_OLSON = 0x20,
	TZ_ALIAS = 0x40,
	TZ_DST = 0x80,

	TZ_ERROR_MASK = TZ_AMBIGUOUS | TZ_NYI,
};

/**
 * Time zone attributes
 */
struct date_time_zone {
	/** Zone name */
	const char *name;
	/** Id assigned to this zone */
	int16_t	id;
	/** Flags (rfc, military, etc) */
	uint16_t flags;
	/** Timezone offset (in minutes) */
	int16_t offset;
};

/**
 * Parse given str string (no longer than given len) and fill zone
 * with attributes of this symbol
 * @param[in] s input string to parse
 * @param[in] len length of input string
 * @param[in] base use base date for timezone parameters resolution
 * @param[out] zone return found zone structure
 * @param[out] gmtoff return gmtoff for the found zone
 * @retval positive value - length of accepted string,
 *         negative value - string looks legit, but is unknown or
 *         unsupported at the moment and should generate exception,
 *         0 - means string is bogus, and should be ignored.
 */
ssize_t
timezone_epoch_lookup(const char *s, size_t len, time_t base,
		      const struct date_time_zone **zone,
		      long *gmtoff);

/**
 * Parse timezone string like @sa timezone_epoch_lookup, but use
 * @struct tnt_tm for passing base date and returning resolved
 * tm_gmtoff or tm_isdst.
 */
ssize_t
timezone_tm_lookup(const char *str, size_t len,
		   const struct date_time_zone **zone,
		   struct tnt_tm *tm);

/**
 * Parse timezone string corresponding to @sa tzindex like
 * @sa timezone_tm_lookup does.
 */
bool
timezone_tzindex_lookup(int16_t tzindex, struct tnt_tm *tm);

/** Return offset in minutes for given zone */
int16_t
timezone_offset(const struct date_time_zone *zone);
/** Return tzindex for given zone */
int16_t
timezone_index(const struct date_time_zone *zone);
/** Return attributes flags for given zone */
uint16_t
timezone_flags(const struct date_time_zone *zone);
/** Return DST flag for given zone */
bool
timezone_isdst(const struct date_time_zone *zone);
/** Translate tzindex to zone name */
const char*
timezone_name(int64_t index);

#ifdef __cplusplus
}
#endif
