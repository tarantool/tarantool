/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <stdio.h>

#include "mp_extension_types.h"
#include "mp_interval.h"
#include "datetime.h"
#include "msgpuck.h"
#include "tt_static.h"

static_assert(DT_EXCESS == 0, "DT_EXCESS is not 0");

enum interval_fields {
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

/** Determine size of one field of packed INTERVAL value. */
static inline uint32_t
value_size(int64_t value)
{
	if (value > 0)
		return 1 + mp_sizeof_uint(value);
	if (value < 0)
		return 1 + mp_sizeof_int(value);
	return 0;
}

/** Length of packed interval. */
static uint32_t
interval_len(const struct interval *itv)
{
	assert(itv->sec >= (double)INT64_MIN && itv->sec < (double)INT64_MAX);
	assert(itv->min >= (double)INT64_MIN && itv->min < (double)INT64_MAX);
	assert(itv->hour >= (double)INT64_MIN && itv->hour < (double)INT64_MAX);
	assert(itv->day >= (double)INT64_MIN && itv->day < (double)INT64_MAX);
	uint32_t size = 1 + value_size(itv->nsec) + value_size(itv->sec) +
			value_size(itv->min) + value_size(itv->hour) +
			value_size(itv->day) + value_size(itv->week) +
			value_size(itv->month) + value_size(itv->year) +
			value_size(itv->adjust);
	assert(size <= UINT8_MAX);
	return size;
}

uint32_t
mp_sizeof_interval(const struct interval *itv)
{
	return mp_sizeof_ext(interval_len(itv));
}

/** Pack one field of INTERVAL value. */
static inline char *
value_pack(char *data, enum interval_fields field, int64_t value)
{
	if (value == 0)
		return data;
	data = mp_encode_uint(data, field);
	if (value > 0)
		data = mp_encode_uint(data, value);
	else
		data = mp_encode_int(data, value);
	return data;
}

/** Pack an interval value to a buffer. */
static char *
interval_pack(char *data, const struct interval *itv)
{
	assert(itv->sec >= (double)INT64_MIN && itv->sec < (double)INT64_MAX);
	assert(itv->min >= (double)INT64_MIN && itv->min < (double)INT64_MAX);
	assert(itv->hour >= (double)INT64_MIN && itv->hour < (double)INT64_MAX);
	assert(itv->day >= (double)INT64_MIN && itv->day < (double)INT64_MAX);
	uint32_t len = (itv->year != 0) + (itv->week != 0) + (itv->month != 0) +
		       ((int64_t)itv->day != 0) + ((int64_t)itv->hour != 0) +
		       ((int64_t)itv->min != 0) + ((int64_t)itv->sec != 0) +
		       (itv->nsec != 0) + (itv->adjust != DT_EXCESS);
	data = mp_store_u8(data, len);
	data = value_pack(data, FIELD_YEAR, itv->year);
	data = value_pack(data, FIELD_MONTH, itv->month);
	data = value_pack(data, FIELD_WEEK, itv->week);
	data = value_pack(data, FIELD_DAY, itv->day);
	data = value_pack(data, FIELD_HOUR, itv->hour);
	data = value_pack(data, FIELD_MINUTE, itv->min);
	data = value_pack(data, FIELD_SECOND, itv->sec);
	data = value_pack(data, FIELD_NANOSECOND, itv->nsec);
	data = value_pack(data, FIELD_ADJUST, itv->adjust);
	return data;
}

struct interval *
interval_unpack(const char **data, uint32_t len, struct interval *itv)
{
	/*
	 * MsgPack extensions have length greater or equal than 1 by
	 * specification.
	 */
	assert(len > 0);

	const char *end = *data + len;
	uint32_t count = mp_load_u8(data);
	len -= sizeof(uint8_t);
	if (count > 0 && len < 2)
		return NULL;

	memset(itv, 0, sizeof(*itv));
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t field = mp_load_u8(data);
		int32_t value;
		enum mp_type type = mp_typeof(**data);
		if (type == MP_UINT) {
			if (mp_check_uint(*data, end) > 0)
				return NULL;
		} else if (type == MP_INT) {
			if (mp_check_int(*data, end) > 0)
				return NULL;
		} else {
			return NULL;
		}
		if (mp_read_int32(data, &value) != 0)
			return NULL;
		switch (field) {
		case FIELD_YEAR:
			itv->year = value;
			break;
		case FIELD_MONTH:
			itv->month = value;
			break;
		case FIELD_WEEK:
			itv->week = value;
			break;
		case FIELD_DAY:
			itv->day = value;
			break;
		case FIELD_HOUR:
			itv->hour = value;
			break;
		case FIELD_MINUTE:
			itv->min = value;
			break;
		case FIELD_SECOND:
			itv->sec = value;
			break;
		case FIELD_NANOSECOND:
			itv->nsec = value;
			break;
		case FIELD_ADJUST:
			if (value > (int32_t)DT_SNAP)
				return NULL;
			itv->adjust = (dt_adjust_t)value;
			break;
		default:
			return NULL;
		}
	}
	if (*data != end)
		return NULL;
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
	uint32_t len = mp_decode_extl(data, &type);
	if (type != MP_INTERVAL || interval_unpack(data, len, itv) == NULL) {
		*data = svp;
		return NULL;
	}
	return itv;
}

int
mp_snprint_interval(char *buf, int size, const char **data, uint32_t len)
{
	struct interval itv;
	if (interval_unpack(data, len, &itv) == NULL)
		return -1;
	return interval_to_string(&itv, buf, size);
}

int
mp_fprint_interval(FILE *file, const char **data, uint32_t len)
{
	struct interval itv;
	if (interval_unpack(data, len, &itv) == NULL)
		return -1;
	char *buf = tt_static_buf();
	interval_to_string(&itv, buf, TT_STATIC_BUF_LEN);
	return fprintf(file, "%s", buf);
}

int
mp_validate_interval(const char *data, uint32_t len)
{
	struct interval itv;
	struct interval *rc = interval_unpack(&data, len, &itv);
	return rc == NULL;
}
