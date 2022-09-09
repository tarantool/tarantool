/*
 * Copyright 2020-2021, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "msgpack.h"
#include "msgpuck/msgpuck.h"

#include "mp_extension_types.h"
#include "mp_decimal.h"
#include "mp_error.h"
#include "mp_uuid.h"
#include "mp_datetime.h"
#include "mp_interval.h"
#include "mp_compression.h"

static int
msgpack_fprint_ext(FILE *file, const char **data, int depth)
{
	const char *orig = *data;
	int8_t type;
	uint32_t len = mp_decode_extl(data, &type);
	switch(type) {
	case MP_DECIMAL:
		return mp_fprint_decimal(file, data, len);
	case MP_UUID:
		return mp_fprint_uuid(file, data, len);
	case MP_DATETIME:
		return mp_fprint_datetime(file, data, len);
	case MP_ERROR:
		return mp_fprint_error(file, data, depth);
	case MP_COMPRESSION:
		return mp_fprint_compression(file, data, len);
	case MP_INTERVAL:
		return mp_fprint_interval(file, data);
	default:
		*data = orig;
		return mp_fprint_ext_default(file, data, depth);
	}
}

static int
msgpack_snprint_ext(char *buf, int size, const char **data, int depth)
{
	const char *orig = *data;
	int8_t type;
	uint32_t len = mp_decode_extl(data, &type);
	switch(type) {
	case MP_DECIMAL:
		return mp_snprint_decimal(buf, size, data, len);
	case MP_UUID:
		return mp_snprint_uuid(buf, size, data, len);
	case MP_DATETIME:
		return mp_snprint_datetime(buf, size, data, len);
	case MP_ERROR:
		return mp_snprint_error(buf, size, data, depth);
	case MP_COMPRESSION:
		return mp_snprint_compression(buf, size, data, len);
	case MP_INTERVAL:
		return mp_snprint_interval(buf, size, data);
	default:
		*data = orig;
		return mp_snprint_ext_default(buf, size, data, depth);
	}
}

/** Our handler to validate MP_EXT contents. */
static int
msgpack_check_ext_data(int8_t type, const char *data, uint32_t len)
{
	switch (type) {
	case MP_DECIMAL:
		return mp_validate_decimal(data, len);
	case MP_UUID:
		return mp_validate_uuid(data, len);
	case MP_DATETIME:
		return mp_validate_datetime(data, len);
	case MP_ERROR:
		return mp_validate_error(data, len);
	case MP_INTERVAL:
		return mp_validate_interval(data, len);
	case MP_COMPRESSION:
	default:
		return mp_check_ext_data_default(type, data, len);
	}
}

void
msgpack_init(void)
{
	mp_fprint_ext = msgpack_fprint_ext;
	mp_snprint_ext = msgpack_snprint_ext;
	mp_check_ext_data = msgpack_check_ext_data;
}
