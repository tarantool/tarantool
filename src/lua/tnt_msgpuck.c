/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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

#include "msgpuck.h"
#include "tnt_msgpuck.h"

char *
tnt_mp_encode_float(char *data, float num)
{
	return mp_encode_float(data, num);
}

char *
tnt_mp_encode_double(char *data, double num)
{
	return mp_encode_double(data, num);
}

float
tnt_mp_decode_float(const char **data)
{
	return mp_decode_float(data);
}

double
tnt_mp_decode_double(const char **data)
{
	return mp_decode_double(data);
}

uint32_t
tnt_mp_decode_extl(const char **data, int8_t *type)
{
	return mp_decode_extl(data, type);
}

char *
tnt_mp_encode_decimal(char *data, const decimal_t *dec)
{
	return mp_encode_decimal(data, dec);
}

uint32_t
tnt_mp_sizeof_decimal(const decimal_t *dec)
{
	return mp_sizeof_decimal(dec);
}

char *
tnt_mp_encode_uuid(char *data, const struct tt_uuid *uuid)
{
	return mp_encode_uuid(data, uuid);
}

uint32_t
tnt_mp_sizeof_uuid(void)
{
	return mp_sizeof_uuid();
}

char *
tnt_mp_encode_error(char *data, const struct error *error)
{
	return mp_encode_error(data, error);
}

uint32_t
tnt_mp_sizeof_error(const struct error *error)
{
	return mp_sizeof_error(error);
}

char *
tnt_mp_encode_datetime(char *data, const struct datetime *date)
{
	return mp_encode_datetime(data, date);
}

uint32_t
tnt_mp_sizeof_datetime(const struct datetime *date)
{
	return mp_sizeof_datetime(date);
}
