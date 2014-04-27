#ifndef TARANTOOL_UUID_H_INCLUDED
#define TARANTOOL_UUID_H_INCLUDED
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
#include <uuid/uuid.h>
#include <string.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum { UUID_STR_LEN = 36 };

typedef struct tt_uuid {
	uuid_t id;
} tt_uuid;

extern tt_uuid uuid_nil;

static inline void
tt_uuid_create(tt_uuid *uu)
{
	uuid_generate(uu->id);
}

static inline int
tt_uuid_from_string(const char *in, tt_uuid *uu)
{
	return uuid_parse((char *) in, uu->id);
}

static inline void
tt_uuid_to_string(const tt_uuid *uu, char *out)
{
	uuid_unparse(uu->id, out);
}

static inline void
tt_uuid_set(tt_uuid *uu, const void *data)
{
	memcpy(uu->id, data, sizeof(uu->id));
}

char *
tt_uuid_str(const struct tt_uuid *uu);

static inline bool
tt_uuid_cmp(const tt_uuid *lhs, const tt_uuid *rhs)
{
	return uuid_compare(lhs->id, rhs->id);
}

static inline bool
tt_uuid_is_nil(const tt_uuid *uu)
{
	return uuid_is_null(uu->id);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_UUID_H_INCLUDED */
