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
#define ZONE_ALIAS(id, alias, name)
#include "timezones.h"
#undef ZONE_ALIAS
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
#define ZONE_UNIQUE(id, name) { name, id, TZ_OLSON, 0 },
#define ZONE_ALIAS(id, alias, name) { alias, id, TZ_OLSON|TZ_ALIAS, 0 },
#include "timezones.h"
#undef ZONE_ALIAS
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

ssize_t
timezone_lookup(const char *str, size_t len, const struct date_time_zone **zone)
{
	len = char_span_alpha(str, len);
	if (len == 0)
		return 0;

	struct date_time_zone *sorted = sort_abbrevs_singleton();
	struct date_time_zone wrap = {.name = str};
	struct date_time_zone *found = (struct date_time_zone *)
		bsearch(&wrap, sorted, lengthof(zone_abbrevs),
			sizeof(struct date_time_zone), compare_abbrevs);

	if (found != NULL) {
		/* lua assumes that single bit is set, not both */
		assert((found->flags & (TZ_NYI | TZ_AMBIGUOUS)) !=
		       (TZ_NYI | TZ_AMBIGUOUS));
		if (found->flags & (TZ_NYI | TZ_AMBIGUOUS))
			return -found->flags;
		*zone = found;
		return len;
	}
	return -1;
}
