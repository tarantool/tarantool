/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <stdio.h>

#include "mp_extension_types.h"
#include "mp_interval.h"
#include "interval.h"
#include "msgpuck.h"

enum {
	FIELD_YEAR = 0,
	FIELD_MONTH,
	FIELD_WEEK,
	FIELD_DAY,
	FIELD_HOUR,
	FIELD_MINUTE,
	FIELD_SECOND,
	FIELD_NANOSECOND,
	FIELD_ADJUST,
};

/** Length of packed interval. */
static uint32_t
interval_len(const struct interval *itv)
{
	uint32_t size = 1;
	if (itv->year > 0)
		size += 1 + mp_sizeof_uint(itv->year);
	else if (itv->year < 0)
		size += 1 + mp_sizeof_int(itv->year);
	if (itv->month > 0)
		size += 1 + mp_sizeof_uint(itv->month);
	else if (itv->month < 0)
		size += 1 + mp_sizeof_int(itv->month);
	if (itv->week > 0)
		size += 1 + mp_sizeof_uint(itv->week);
	else if (itv->week < 0)
		size += 1 + mp_sizeof_int(itv->week);
	if (itv->day > 0)
		size += 1 + mp_sizeof_uint(itv->day);
	else if (itv->day < 0)
		size += 1 + mp_sizeof_int(itv->day);
	if (itv->hour > 0)
		size += 1 + mp_sizeof_uint(itv->hour);
	else if (itv->hour < 0)
		size += 1 + mp_sizeof_int(itv->hour);
	if (itv->min > 0)
		size += 1 + mp_sizeof_uint(itv->min);
	else if (itv->min < 0)
		size += 1 + mp_sizeof_int(itv->min);
	if (itv->sec > 0)
		size += 1 + mp_sizeof_uint(itv->sec);
	else if (itv->sec < 0)
		size += 1 + mp_sizeof_int(itv->sec);
	if (itv->nsec > 0)
		size += 1 + mp_sizeof_uint(itv->nsec);
	else if (itv->nsec < 0)
		size += 1 + mp_sizeof_int(itv->nsec);
	if (itv->adjust != INTERVAL_ADJUST_EXCESS)
		size += 1 + mp_sizeof_uint((uint32_t)itv->adjust);
	assert(size <= UINT8_MAX);
	return size;
}

uint32_t
mp_sizeof_interval(const struct interval *itv)
{
	return mp_sizeof_ext(interval_len(itv));
}

/**
 * Pack an interval value to a buffer.
 *
 * @param data A buffer.
 * @param itv An interval to encode.
 *
 * @return data + mp_sizeof_interval(itv).
 */
static char *
interval_pack(char *data, const struct interval *itv)
{
	uint32_t len = (uint32_t)(itv->year != 0) + (uint32_t)(itv->week != 0) +
		       (uint32_t)(itv->month != 0) + (uint32_t)(itv->day != 0) +
		       (uint32_t)(itv->hour != 0) + (uint32_t)(itv->min != 0) +
		       (uint32_t)(itv->sec != 0) + (uint32_t)(itv->nsec != 0) +
		       (uint32_t)(itv->adjust != INTERVAL_ADJUST_EXCESS);
	data = mp_store_u8(data, len);
	if (itv->year > 0) {
		data = mp_encode_uint(data, FIELD_YEAR);
		data = mp_encode_uint(data, itv->year);
	} else if (itv->year < 0) {
		data = mp_encode_uint(data, FIELD_YEAR);
		data = mp_encode_int(data, itv->year);
	}
	if (itv->month > 0) {
		data = mp_encode_uint(data, FIELD_MONTH);
		data = mp_encode_uint(data, itv->month);
	} else if (itv->month < 0) {
		data = mp_encode_uint(data, FIELD_MONTH);
		data = mp_encode_int(data, itv->month);
	}
	if (itv->week > 0) {
		data = mp_encode_uint(data, FIELD_WEEK);
		data = mp_encode_uint(data, itv->week);
	} else if (itv->week < 0) {
		data = mp_encode_uint(data, FIELD_WEEK);
		data = mp_encode_int(data, itv->week);
	}
	if (itv->day > 0) {
		data = mp_encode_uint(data, FIELD_DAY);
		data = mp_encode_uint(data, itv->day);
	} else if (itv->day < 0) {
		data = mp_encode_uint(data, FIELD_DAY);
		data = mp_encode_int(data, itv->day);
	}
	if (itv->hour > 0) {
		data = mp_encode_uint(data, FIELD_HOUR);
		data = mp_encode_uint(data, itv->hour);
	} else if (itv->hour < 0) {
		data = mp_encode_uint(data, FIELD_HOUR);
		data = mp_encode_int(data, itv->hour);
	}
	if (itv->min > 0) {
		data = mp_encode_uint(data, FIELD_MINUTE);
		data = mp_encode_uint(data, itv->min);
	} else if (itv->min < 0) {
		data = mp_encode_uint(data, FIELD_MINUTE);
		data = mp_encode_int(data, itv->min);
	}
	if (itv->sec > 0) {
		data = mp_encode_uint(data, FIELD_SECOND);
		data = mp_encode_uint(data, itv->sec);
	} else if (itv->sec < 0) {
		data = mp_encode_uint(data, FIELD_SECOND);
		data = mp_encode_int(data, itv->sec);
	}
	if (itv->nsec > 0) {
		data = mp_encode_uint(data, FIELD_NANOSECOND);
		data = mp_encode_uint(data, itv->nsec);
	} else if (itv->nsec < 0) {
		data = mp_encode_uint(data, FIELD_NANOSECOND);
		data = mp_encode_int(data, itv->nsec);
	}
	if (itv->adjust != INTERVAL_ADJUST_EXCESS) {
		data = mp_encode_uint(data, FIELD_ADJUST);
		data = mp_encode_uint(data, (uint32_t)itv->adjust);
	}
	return data;
}

struct interval *
interval_unpack(const char **data, struct interval *itv)
{
	struct interval tmp = {};
	uint32_t len = mp_load_u8(data);
	for (uint32_t i = 0; i < len; ++i) {
		uint32_t field = mp_load_u8(data);
		int32_t value;
		if (mp_read_int32(data, &value) != 0)
			return NULL;
		switch (field) {
		case FIELD_YEAR:
			tmp.year = value;
			break;
		case FIELD_MONTH:
			tmp.month = value;
			break;
		case FIELD_WEEK:
			tmp.week = value;
			break;
		case FIELD_DAY:
			tmp.day = value;
			break;
		case FIELD_HOUR:
			tmp.hour = value;
			break;
		case FIELD_MINUTE:
			tmp.min = value;
			break;
		case FIELD_SECOND:
			tmp.sec = value;
			break;
		case FIELD_NANOSECOND:
			tmp.nsec = value;
			break;
		case FIELD_ADJUST:
			if (value > (int32_t)INTERVAL_ADJUST_SNAP)
				return NULL;
			tmp.adjust = (enum adjust)value;
			break;
		default:
			return NULL;
		}
	}
	*itv = tmp;
	return itv;
}

char *
mp_encode_interval(char *data, const struct interval *itv)
{
	data = mp_encode_extl(data, MP_INTERVAL, interval_len(itv));
	return interval_pack(data, itv);
}

struct interval *
mp_decode_interval(const char **data, struct interval *itv)
{
	if (mp_typeof(**data) != MP_EXT)
		return NULL;
	int8_t type;
	const char *svp = *data;
	mp_decode_extl(data, &type);
	if (type != MP_INTERVAL || interval_unpack(data, itv) == NULL) {
		*data = svp;
		return NULL;
	}
	return itv;
}

int
mp_snprint_interval(char *buf, int size, const char **data)
{
	struct interval itv;
	if (interval_unpack(data, &itv) == NULL)
		return -1;
	return snprintf(buf, size, "%s", interval_str(&itv));
}

int
mp_fprint_interval(FILE *file, const char **data)
{
	struct interval itv;
	if (interval_unpack(data, &itv) == NULL)
		return -1;
	return fprintf(file, "%s", interval_str(&itv));
}
