/*
 * Copyright 2020, Tarantool AUTHORS, please see AUTHORS file.
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
#include "uuid/mp_uuid.h"
#include "mp_error.h"

static int
msgpack_fprint_ext(FILE *file, const char **data, int depth)
{
	const char **orig = data;
	int8_t type;
	uint32_t len = mp_decode_extl(data, &type);
	switch(type) {
	case MP_DECIMAL:
		return mp_fprint_decimal(file, data, len);
	case MP_UUID:
		return mp_fprint_uuid(file, data, len);
	case MP_ERROR:
		return mp_fprint_error(file, data, depth);
	default:
		return mp_fprint_ext_default(file, orig, depth);
	}
}

static int
msgpack_snprint_ext(char *buf, int size, const char **data, int depth)
{
	const char **orig = data;
	int8_t type;
	uint32_t len = mp_decode_extl(data, &type);
	switch(type) {
	case MP_DECIMAL:
		return mp_snprint_decimal(buf, size, data, len);
	case MP_UUID:
		return mp_snprint_uuid(buf, size, data, len);
	case MP_ERROR:
		return mp_snprint_error(buf, size, data, depth);
	default:
		return mp_snprint_ext_default(buf, size, orig, depth);
	}
}

void
msgpack_init(void)
{
	mp_fprint_ext = msgpack_fprint_ext;
	mp_snprint_ext = msgpack_snprint_ext;
}
