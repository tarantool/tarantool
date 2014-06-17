#ifndef INCLUDES_TARANTOOL_VCLOCK_H
#define INCLUDES_TARANTOOL_VCLOCK_H
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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum { VCLOCK_MAX = 16 };

struct vclock {
	int64_t lsn[VCLOCK_MAX];
};

#define vclock_foreach(vclock, var) \
	for (struct { uint32_t id; int64_t lsn;} (var) = {0, 0}; \
	     (var).id < VCLOCK_MAX; (var).id++) \
		if (((var).lsn = (vclock)->lsn[(var).id]) >= 0)

static inline void
vclock_create(struct vclock *vclock)
{
	memset(vclock, 0xff, sizeof(*vclock));
}

static inline void
vclock_destroy(struct vclock *vclock)
{
	(void) vclock;
}

static inline bool
vclock_has(const struct vclock *vclock, uint32_t server_id)
{
	return server_id < VCLOCK_MAX && vclock->lsn[server_id] >= 0;
}

static inline int64_t
vclock_get(const struct vclock *vclock, uint32_t server_id)
{
	return vclock_has(vclock, server_id) ? vclock->lsn[server_id] : -1;
}

void
vclock_set(struct vclock *vclock, uint32_t server_id, int64_t lsn);

static inline int64_t
vclock_inc(struct vclock *vclock, uint32_t server_id)
{
	assert(vclock_has(vclock, server_id));
	return ++vclock->lsn[server_id];
}

static inline void
vclock_copy(struct vclock *dst, const struct vclock *src)
{
	*dst = *src;
}

static inline uint32_t
vclock_size(const struct vclock *vclock)
{
	int32_t size = 0;
	vclock_foreach(vclock, pair)
		++size;
	return size;
}

static inline int64_t
vclock_signature(const struct vclock *vclock)
{
	int64_t sum = 0;
	vclock_foreach(vclock, server)
		sum += server.lsn;
	return sum;
}

int64_t
vclock_cas(struct vclock *vclock, uint32_t server_id, int64_t lsn);

#if defined(__cplusplus)
} /* extern "C" */

#include "exception.h"

static inline void
vclock_add_server(struct vclock *vclock, uint32_t server_id)
{
	if (server_id >= VCLOCK_MAX)
		tnt_raise(ClientError, ER_REPLICA_MAX, server_id);
	assert(! vclock_has(vclock, server_id));
	vclock->lsn[server_id] = 0;
}

static inline void
vclock_del_server(struct vclock *vclock, uint32_t server_id)
{
	assert(vclock_has(vclock, server_id));
	vclock->lsn[server_id] = -1;
}

#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_VCLOCK_H */
