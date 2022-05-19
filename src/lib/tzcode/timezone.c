/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include "datetime.h"
#include "timezone.h"
#include "tzcode.h"
#include "trivia/util.h"

/**
 * Array of zone descriptors, placed in their natural id order, used for
 * translation from zone id to unique zone name and their attributes.
 */
static struct date_time_zone zones_raw[] = {
#define ZONE_ABBREV(id, offset, name, flags) {name, id, flags, offset},
#define ZONE_UNIQUE(id, name) {name, id, TZ_OLSON, 0},
#define ZONE_ALIAS(id, alias, name) {alias, id, TZ_OLSON|TZ_ALIAS, 0},
#include "timezones.h"
#undef ZONE_ALIAS
#undef ZONE_UNIQUE
#undef ZONE_ABBREV
};

#define ZONES_MAX 1024
static_assert(ZONES_MAX >= lengthof(zones_raw), "please increase ZONES_MAX");

/**
 * Sorted array of zone descriptors, whether it's abbreviation, full
 * zone name or [backward-compatible] link name.
 */
static struct date_time_zone zones_sorted[lengthof(zones_raw)];
static struct date_time_zone zones_unsorted[ZONES_MAX];

static int
compare_zones(const void *a, const void *b)
{
	return strcasecmp(((const struct date_time_zone *) a)->name,
			  ((const struct date_time_zone*) b)->name);
}

static void __attribute__((constructor))
sort_array(void)
{
	size_t i;
	/* 1st save zones in id order for stringization */
	for (i = 0; i < lengthof(zones_raw); i++) {
		size_t id = zones_raw[i].id;
		assert(id < lengthof(zones_unsorted));
		/* save to unsorted array if it's not an alias */
		if ((zones_raw[i].flags & TZ_ALIAS) == 0)
			zones_unsorted[id] = zones_raw[i];
	}
	/* 2nd copy raw to be sorted and prepare them for bsearch */
	assert(sizeof(zones_sorted) == sizeof(zones_raw));
	memcpy(zones_sorted, zones_raw, sizeof(zones_raw));
	qsort(zones_sorted, lengthof(zones_sorted), sizeof(zones_sorted[0]),
	      compare_zones);
}

int16_t
timezone_offset(const struct date_time_zone *zone)
{
	return zone->offset;
}

int16_t
timezone_index(const struct date_time_zone *zone)
{
	return zone->id;
}

uint16_t
timezone_flags(const struct date_time_zone *zone)
{
	return zone->flags;
}

bool
timezone_isdst(const struct date_time_zone *zone)
{
	return !!(zone->flags & TZ_DST);
}

const char*
timezone_name(int64_t index)
{
	assert((size_t)index < lengthof(zones_unsorted));
	return zones_unsorted[index].name;
}

/** lookaside values we reuse across parser calls */
static timezone_t prev_tz;
static char prev_zonename[DT_TO_STRING_BUFSIZE];

static timezone_t
timezone_alloc(const char * zonename)
{
	assert(zonename != NULL);
	if (prev_zonename[0] != '\0' && strcmp(prev_zonename, zonename) == 0)
		return prev_tz;
	if (prev_tz != NULL)
		tzfree(prev_tz);
	prev_tz = tzalloc(zonename);
	assert(strlen(zonename) < lengthof(prev_zonename));
	strcpy(prev_zonename, zonename);
	return prev_tz;
}

static void __attribute__((destructor))
timezone_free(void)
{
	if (prev_tz != NULL) {
		tzfree(prev_tz);
		prev_tz = NULL;
		prev_zonename[0] = '\0';
	}
}

/**
 * We want to accept only names in a form:
 * - Z, AT, MSK, i.e. [A-Z]{1,6}
 * - Etc/GMT, Europe/Moscow, America/St_Kitts, i.e. [A-Za-z][A-Za-z/_-]*
 * NB! Eventually should be reimplemented with proper regexp, but now
 * it accepts slightly wider class of input.
 */
static size_t
char_span_alpha(const char *src, size_t len)
{
	size_t n;

	if (len == 0 || !isalpha(src[0]))
		return 0;
	for (n = 0; n < len; n++) {
		char ch = src[n];
		if (!isalpha(ch) && ch != '/' && ch != '_' && ch != '-')
			break;

	}
	return n;
}

static inline ssize_t
timezone_raw_lookup(const char *str, size_t len,
		    const struct date_time_zone **zone)
{
	len = char_span_alpha(str, len);
	if (len == 0)
		return 0;

	struct date_time_zone wrap = {.name = str};
	struct date_time_zone *found = (struct date_time_zone *)
		bsearch(&wrap, zones_sorted, lengthof(zones_sorted),
			sizeof(zones_sorted[0]), compare_zones);

	if (found != NULL) {
		/* lua assumes that single bit is set, not both */
		assert((found->flags & TZ_ERROR_MASK) != TZ_ERROR_MASK);
		if (found->flags & TZ_ERROR_MASK)
			return -(found->flags & TZ_ERROR_MASK);
		*zone = found;
		return len;
	}
	return -1;
}

ssize_t
timezone_tm_lookup(const char *str, size_t len,
		   const struct date_time_zone **zone,
		   struct tnt_tm *tm)
{
	ssize_t rc = timezone_raw_lookup(str, len, zone);
	if (rc <= 0)
		return rc;

	const struct date_time_zone *found = *zone;
	if ((found->flags & TZ_OLSON) == 0) {
		tm->tm_gmtoff = found->offset * 60;
		tm->tm_tzindex = found->id;
		tm->tm_isdst = !!(found->flags & TZ_DST);
		return rc;
	}
	timezone_t tz = timezone_alloc(str);
	if (tz == NULL)
		return 0;
	struct datetime date = {.epoch = 0};
	if (tm_to_datetime(tm, &date) == false)
		return 0;
	time_t epoch = (int64_t)date.epoch;
	struct tnt_tm * result = tnt_localtime_rz(tz, &epoch, tm);
	if (result == NULL)
		return 0;
	return rc;
}

ssize_t
timezone_epoch_lookup(const char *str, size_t len, time_t base,
		      const struct date_time_zone **zone,
		      long *gmtoff)
{
	ssize_t rc = timezone_raw_lookup(str, len, zone);
	if (rc <= 0)
		return rc;

	const struct date_time_zone *found = *zone;
	if ((found->flags & TZ_OLSON) == 0) {
		assert((TZ_ERROR_MASK & found->flags) == 0);
		*gmtoff = found->offset * 60;
		return rc;
	}
	timezone_t tz = NULL;
	tz = timezone_alloc(str);
	if (tz == NULL)
		return 0;
	struct tnt_tm tm = {.tm_epoch = 0};
	struct tnt_tm * result = tnt_localtime_rz(tz, &base, &tm);
	if (result == NULL)
		return 0;
	*gmtoff = result->tm_gmtoff;
	return rc;
}

bool
timezone_tzindex_lookup(int16_t tzindex, struct tnt_tm *tm)
{
	assert(tm != NULL);
	if (tzindex == 0)
		return false;

	timezone_t tz = timezone_alloc(timezone_name(tzindex));
	if (tz == NULL)
		return false;
	time_t epoch = (int64_t)tm->tm_epoch;
	struct tnt_tm *result = tnt_localtime_rz(tz, &epoch, tm);
	if (result == NULL)
		return false;
	return true;
}
