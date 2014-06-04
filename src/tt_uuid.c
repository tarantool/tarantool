/*
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
#include "tt_uuid.h"
#include "msgpuck/msgpuck.h"

/* Zeroed by the linker. */
const tt_uuid uuid_nil;

static __thread char buf[UUID_STR_LEN + 1];

char *
tt_uuid_str(const tt_uuid *uu)
{
	tt_uuid_to_string(uu, buf);
	return buf;
}

int
tt_uuid_from_msgpack(const char **data, tt_uuid *out)
{
	const char *d = *data;
	if (mp_typeof(*d) != MP_STR)
		return -1;
	if (mp_decode_strl(&d) != UUID_STR_LEN)
		return -2;
	/* Need zero-terminated string for tt_uuid_from_string */
	memcpy(buf, d, UUID_STR_LEN);
	buf[UUID_STR_LEN] = 0;
	if (tt_uuid_from_string(buf, out) != 0)
		return -3;
	*data = d + UUID_STR_LEN;
	return 0;
}

char *
tt_uuid_to_msgpack(char *data, const tt_uuid *uu)
{
	data = mp_encode_strl(data, UUID_STR_LEN);
	tt_uuid_to_string(uu, data);
	data += UUID_STR_LEN;
	return data;
}
