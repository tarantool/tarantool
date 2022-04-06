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
#include "trivia/util.h"

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

static const char * zone_names[] = {
#define ZONE_ABBREV(id, offset, name, flags) [id] = name,
#define ZONE_UNIQUE(id, name) [id] = name,
#include "timezones.h"
#undef ZONE_UNIQUE
#undef ZONE_ABBREV
};

const char*
timezone_name(int64_t index)
{
	assert((size_t)index < lengthof(zone_names));
	return zone_names[index];
}

static struct date_time_zone zone_abbrevs[] = {
#define ZONE_ABBREV(id, offset, name, flags) { name, id, flags, offset },
#define ZONE_UNIQUE(id, name) { name, id, TZ_NYI, 0 },

#include "timezones.h"
#undef ZONE_UNIQUE
#undef ZONE_ABBREV
};

static int
compare_abbrevs(const void *a, const void *b)
{
	return strcasecmp(((const struct date_time_zone *) a)->name,
			  ((const struct date_time_zone*) b)->name);
}

static inline struct date_time_zone *
sort_abbrevs_singleton(void)
{
	static struct date_time_zone *sorted = NULL;
	if (sorted != NULL)
		return sorted;

	qsort(zone_abbrevs, lengthof(zone_abbrevs),
	      sizeof(struct date_time_zone), compare_abbrevs);
	sorted = zone_abbrevs;

	return sorted;
}

static size_t
char_span_alpha(const char *src, size_t len)
{
	size_t n;

	for (n = 0; n < len; n++)
		if (!isalpha(src[n]) && !ispunct(src[n]))
			break;

	return n;
}

size_t
timezone_lookup(const char *str, size_t len, const struct date_time_zone **zone)
{
	len = char_span_alpha(str, len);
	if (!len || len > 6)
		return 0;

	struct date_time_zone *sorted = sort_abbrevs_singleton();
	struct date_time_zone wrap = {.name = str};
	struct date_time_zone *found = (struct date_time_zone *)
		bsearch(&wrap, sorted, lengthof(zone_abbrevs),
			sizeof(struct date_time_zone), compare_abbrevs);

	if (found != NULL) {
		*zone = found;
		return len;
	}
	return 0;
}
