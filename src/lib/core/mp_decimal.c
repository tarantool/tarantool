/*
 * Copyright 2019, Tarantool AUTHORS, please see AUTHORS file.
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

#include "mp_decimal.h"
#include "mp_extension_types.h"
#include "msgpuck.h"
#include "decimal.h"

uint32_t
mp_sizeof_decimal(const decimal_t *dec)
{
	return mp_sizeof_ext(decimal_len(dec));
}

decimal_t *
mp_decode_decimal(const char **data, decimal_t *dec)
{
	if (mp_typeof(**data) != MP_EXT)
		return NULL;

	int8_t type;
	uint32_t len;
	const char *const svp = *data;

	len = mp_decode_extl(data, &type);

	if (type != MP_DECIMAL || len == 0) {
		*data = svp;
		return NULL;
	}
	decimal_t *res = decimal_unpack(data,  len, dec);
	if (!res)
		*data = svp;
	return res;
}

char *
mp_encode_decimal(char *data, const decimal_t *dec)
{
	uint32_t len = decimal_len(dec);
	data = mp_encode_extl(data, MP_DECIMAL, len);
	data = decimal_pack(data, dec);
	return data;
}

int
mp_snprint_decimal(char *buf, int size, const char **data, uint32_t len)
{
	decimal_t d;
	if (decimal_unpack(data, len, &d) == NULL)
		return -1;
	return snprintf(buf, size, "%s", decimal_str(&d));
}

int
mp_fprint_decimal(FILE *file, const char **data, uint32_t len)
{
	decimal_t d;
	if (decimal_unpack(data, len, &d) == NULL)
		return -1;
	return fprintf(file, "%s", decimal_str(&d));
}
