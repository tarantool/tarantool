/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <assert.h>
#include <limits.h>

#include "msgpuck.h"
#include "bit/bit.h"
#include "mp_datetime.h"
#include "mp_extension_types.h"

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
	      "Tarantool currently only supports little-endian hardware.");

/**
 * Datetime MessagePack serialization schema is MP_EXT extension, which
 * creates container of 8 or 16 bytes long payload.
 *
 * +---------+--------+===============+-------------------------------+
 * |0xd7/0xd8|type (4)| seconds (8b)  | nsec; tzoffset; tzindex; (8b) |
 * +---------+--------+===============+-------------------------------+
 *
 * MessagePack data encoded using fixext8 (0xd7) or fixext16 (0xd8), and may
 * contain:
 *
 * - [required] seconds parts as full, unencoded, signed 64-bit integer, stored
 *   in little-endian order;
 *
 * - [optional] all the other fields (nsec, tzoffset, tzindex) if any of them
 *    were having not 0 value. They are packed naturally in little-endian order;
 *
 */

#define SZ_TAIL sizeof(struct datetime) - sizeof(((struct datetime *)0)->epoch)

static inline uint32_t
mp_sizeof_datetime_raw(const struct datetime *date)
{
	uint32_t sz = sizeof(int64_t);
	if (mp_unlikely(date->tzoffset != 0 || date->tzindex != 0 ||
			date->nsec != 0))
		sz += SZ_TAIL;
	return sz;
}

uint32_t
mp_sizeof_datetime(const struct datetime *date)
{
	return mp_sizeof_ext(mp_sizeof_datetime_raw(date));
}

struct datetime *
datetime_unpack(const char **data, uint32_t len, struct datetime *date)
{
	if (len <= 0)
		return NULL;

	const char *const svp = *data;
	memset(date, 0, sizeof(*date));

	int64_t i_epoch = load_u64(*data);
	date->epoch = i_epoch;
	*data += sizeof(i_epoch);
	len -= sizeof(i_epoch);

	if (len == 0)
		return date;

	if (len != SZ_TAIL) {
		*data = svp;
		return NULL;
	}
	memcpy(&date->nsec, *data, SZ_TAIL);
	*data += SZ_TAIL;

	return date;
}

struct datetime *
mp_decode_datetime(const char **data, struct datetime *date)
{
	if (mp_typeof(**data) != MP_EXT)
		return NULL;

	const char *svp = *data;
	int8_t type;
	uint32_t len = mp_decode_extl(data, &type);

	if (type != MP_DATETIME ||
	    datetime_unpack(data, len, date) == NULL) {
		*data = svp;
		return NULL;
	}
	return date;
}

char *
datetime_pack(char *data, const struct datetime *date)
{
	int64_t i_epoch = date->epoch;
	store_u64(data, i_epoch);
	data += sizeof(i_epoch);
	if (mp_unlikely(date->tzoffset != 0 || date->tzindex != 0 ||
			date->nsec != 0)) {
		memcpy(data, &date->nsec, SZ_TAIL);
		data += SZ_TAIL;
	}
	return data;
}

char *
mp_encode_datetime(char *data, const struct datetime *date)
{
	uint32_t len = mp_sizeof_datetime_raw(date);

	data = mp_encode_extl(data, MP_DATETIME, len);

	return datetime_pack(data, date);
}

int
mp_snprint_datetime(char *buf, int size, const char **data, uint32_t len)
{
	struct datetime date = {
		.epoch = 0,
		.nsec = 0,
		.tzoffset = 0,
		.tzindex = 0,
	};

	if (datetime_unpack(data, len, &date) == NULL)
		return -1;

	return datetime_to_string(&date, buf, size);
}

int
mp_fprint_datetime(FILE *file, const char **data, uint32_t len)
{
	struct datetime date = {
		.epoch = 0,
		.nsec = 0,
		.tzoffset = 0,
		.tzindex = 0,
	};

	if (datetime_unpack(data, len, &date) == NULL)
		return -1;

	char buf[DT_TO_STRING_BUFSIZE];
	datetime_to_string(&date, buf, sizeof(buf));

	return fprintf(file, "%s", buf);
}

