/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "tt_uuid.h"

#include <msgpuck.h>
#include <random.h>
#include <trivia/config.h>
#include <trivia/util.h>

/* Zeroed by the linker. */
const struct tt_uuid uuid_nil;

#define CT_ASSERT(e) typedef char __ct_assert_##__LINE__[(e) ? 1 : -1]
CT_ASSERT(sizeof(struct tt_uuid) == UUID_LEN);

#if defined(HAVE_UUIDGEN)
#include <sys/uuid.h>

CT_ASSERT(sizeof(struct tt_uuid) == sizeof(struct uuid));

void
tt_uuid_create(struct tt_uuid *uu)
{
	uuidgen((struct uuid *) uu, 1); /* syscall */
}
#else

void
tt_uuid_create(struct tt_uuid *uu)
{
	random_bytes((char *) uu, sizeof(*uu));

	uu->clock_seq_hi_and_reserved &= 0x3f;
	uu->clock_seq_hi_and_reserved |= 0x80; /* variant 1 = RFC4122 */
	uu->time_hi_and_version &= 0x0FFF;
	uu->time_hi_and_version |= (4 << 12);  /* version 4 = random */
}
#endif

extern inline int
tt_uuid_from_string(const char *in, struct tt_uuid *uu);

extern inline int
tt_uuid_compare(const struct tt_uuid *a, const struct tt_uuid *b);

extern inline void
tt_uuid_to_string(const struct tt_uuid *uu, char *out);

extern inline void
tt_uuid_bswap(struct tt_uuid *uu);

extern inline bool
tt_uuid_is_nil(const struct tt_uuid *uu);

extern inline bool
tt_uuid_is_equal(const struct tt_uuid *lhs, const struct tt_uuid *rhs);

char *
tt_uuid_str(const struct tt_uuid *uu)
{
	assert(TT_STATIC_BUF_LEN > UUID_STR_LEN);
	char *buf = tt_static_buf();
	tt_uuid_to_string(uu, buf);
	return buf;
}

int
tt_uuid_from_strl(const char *in, size_t len, struct tt_uuid *uu)
{
	char buf[UUID_STR_LEN + 1];
	snprintf(buf, sizeof(buf), "%.*s", (int) len, in);
	return tt_uuid_from_string(buf, uu);
}
