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
#include <trivia/config.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum { UUID_LEN = 16, UUID_STR_LEN = 36 };

#if defined(HAVE_LIBUUID_LINUX)

#include <uuid/uuid.h>

typedef struct tt_uuid {
	uuid_t id;
} tt_uuid;

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
tt_uuid_dec_be(const void *in, tt_uuid *uu)
{
	memcpy(uu->id, in, sizeof(uu->id));
}

static inline void
tt_uuid_enc_be(const tt_uuid *uu, void *out)
{
	memcpy(out, uu->id, sizeof(uu->id));
}

static inline bool
tt_uuid_is_nil(const tt_uuid *uu)
{
	return uuid_is_null(uu->id);
}

static inline bool
tt_uuid_cmp(const tt_uuid *lhs, const tt_uuid *rhs)
{
	return uuid_compare(lhs->id, rhs->id);
}

#elif defined(HAVE_LIBUUID_BSD)

#include <uuid.h>

typedef struct uuid tt_uuid;

static inline int
tt_uuid_create(tt_uuid *uu)
{
	uint32_t status;
	uuid_create(uu, &status);
	return status == uuid_s_ok;
}

static inline int
tt_uuid_from_string(const char *in, tt_uuid *uu)
{
	uint32_t status;
	uuid_from_string(in, uu, &status);
	return status == uuid_s_ok;
}

static inline void
tt_uuid_to_string(const tt_uuid *uu, char *out)
{
	uint32_t status;
	char *buf = NULL;
	uuid_to_string(uu, &buf, &status);
	assert(status == uuid_s_ok);
	strncpy(out, buf, UUID_STR_LEN);
	out[UUID_STR_LEN] = '\0';
	free(buf);
}

static inline bool
tt_uuid_cmp(const tt_uuid *lhs, const tt_uuid *rhs)
{
	uint32_t status;
	return uuid_compare(lhs, rhs, &status);
}

static inline bool
tt_uuid_is_nil(const tt_uuid *uu)
{
	uint32_t status;
	return uuid_is_nil(uu, &status);
}

static inline void
tt_uuid_dec_be(const void *in, tt_uuid *uu)
{
	uuid_dec_be(in, uu);

}

static inline void
tt_uuid_enc_be(const tt_uuid *uu, void *out)
{
	uuid_enc_be(out, uu);
}
#else
#error Unsupported libuuid
#endif /* HAVE_LIBUUID_XXX */

extern const tt_uuid uuid_nil;

char *
tt_uuid_str(const tt_uuid *uu);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_UUID_H_INCLUDED */
