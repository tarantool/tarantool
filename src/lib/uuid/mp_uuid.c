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

#include "mp_uuid.h"
#include "msgpuck.h"
#include "mp_extension_types.h"

inline uint32_t
mp_sizeof_uuid(void)
{
	return mp_sizeof_ext(UUID_PACKED_LEN);
}

char *
uuid_pack(char *data, const struct tt_uuid *uuid)
{
	data = mp_store_u32(data, uuid->time_low);
	data = mp_store_u16(data, uuid->time_mid);
	data = mp_store_u16(data, uuid->time_hi_and_version);
	data = mp_store_u8(data, uuid->clock_seq_hi_and_reserved);
	data = mp_store_u8(data, uuid->clock_seq_low);
	for (int i = 0; i < 6; i++)
		data = mp_store_u8(data, uuid->node[i]);
	return data;
}

struct tt_uuid *
uuid_unpack(const char **data, uint32_t len, struct tt_uuid *uuid)
{
	const char *const svp = *data;
	if (len != UUID_PACKED_LEN)
		return NULL;
	uuid->time_low = mp_load_u32(data);
	uuid->time_mid = mp_load_u16(data);
	uuid->time_hi_and_version = mp_load_u16(data);
	uuid->clock_seq_hi_and_reserved = mp_load_u8(data);
	uuid->clock_seq_low = mp_load_u8(data);
	for (int i = 0; i < 6; i++)
		uuid->node[i] = mp_load_u8(data);

	if (tt_uuid_validate(uuid) != 0) {
		*data = svp;
		return NULL;
	}
	return uuid;
}

struct tt_uuid *
mp_decode_uuid(const char **data, struct tt_uuid *uuid)
{
	if (mp_typeof(**data) != MP_EXT)
		return NULL;
	int8_t type;
	const char *const svp = *data;

	uint32_t len = mp_decode_extl(data, &type);
	if (type != MP_UUID || uuid_unpack(data, len, uuid) == NULL) {
		*data = svp;
		return NULL;
	}
	return uuid;
}

char *
mp_encode_uuid(char *data, const struct tt_uuid *uuid)
{
	data = mp_encode_extl(data, MP_UUID, UUID_PACKED_LEN);
	return uuid_pack(data, uuid);
}

int
mp_snprint_uuid(char *buf, int size, const char **data, uint32_t len)
{
	struct tt_uuid uuid;
	if (uuid_unpack(data, len, &uuid) == NULL)
		return -1;
	return snprintf(buf, size, "%s", tt_uuid_str(&uuid));
}

int
mp_fprint_uuid(FILE *file, const char **data, uint32_t len)
{
	struct tt_uuid uuid;
	if (uuid_unpack(data, len, &uuid) == NULL)
		return -1;
	return fprintf(file, "%s", tt_uuid_str(&uuid));
}
