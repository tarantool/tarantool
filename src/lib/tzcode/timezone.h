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

enum {
	TZ_UTC = 0x01,
	TZ_RFC = 0x02,
	TZ_MILITARY = 0x04,
	TZ_AMBIGUOUS = 0x08,
	TZ_NYI = 0x10,
};

struct date_time_zone {
	const char *name;
	int16_t	id;
	uint16_t flags;
	int16_t offset;
};

size_t
timezone_lookup(const char *str, size_t, const struct date_time_zone **zone);
int16_t
timezone_offset(const struct date_time_zone *zone);
int16_t
timezone_index(const struct date_time_zone *zone);
uint16_t
timezone_flags(const struct date_time_zone *zone);
const char*
timezone_name(int64_t index);

#ifdef __cplusplus
}
#endif
