#ifndef INCLUDES_VCLOCK_H
#define INCLUDES_VCLOCK_H
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
#include <assert.h>

struct vclock {
	int64_t *lsn;
	uint32_t capacity;
};

#define vclock_foreach(vclock, var) \
	for (struct { uint32_t node_id; int64_t lsn;} (var) = {0, 0}; \
	     (var).node_id < (vclock)->capacity; (var).node_id++) \
		if (((var).lsn = (vclock)->lsn[(var).node_id]) < 0) continue; else

static inline void
vclock_create(struct vclock *vclock)
{
	memset(vclock, 0, sizeof(*vclock));
}

static inline void
vclock_destroy(struct vclock *vclock)
{
	free(vclock->lsn);
	vclock->capacity = 0;
}

static inline int64_t
vclock_get(const struct vclock *vclock, uint32_t node_id)
{
	if (node_id >= vclock->capacity)
		return -1;

	return vclock->lsn[node_id];
}

void
vclock_realloc(struct vclock *vclock, uint32_t node_id);

static inline void
vclock_set(struct vclock *vclock, uint32_t node_id, int64_t lsn)
{
	if (node_id >= vclock->capacity)
		vclock_realloc(vclock, node_id);

	assert(vclock->lsn[node_id] < lsn);
	vclock->lsn[node_id] = lsn;
}

static inline void
vclock_add_server(struct vclock *vclock, uint32_t server_id)
{
	vclock_set(vclock, server_id, 0);
}


static inline void
vclock_del(struct vclock *vclock, uint32_t node_id)
{
	if (node_id >= vclock->capacity)
		return;

	vclock->lsn[node_id] = -1;
}

static inline int
vclock_copy(struct vclock *vclock, const struct vclock *src)
{
	for (uint32_t n = 0; n < src->capacity; n++) {
		if (src->lsn[n] < 0)
			continue;

		vclock_set(vclock, n, src->lsn[n]);
	}
	return 0;
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
	vclock_foreach(vclock, pair) {
		sum += pair.lsn;
	}

	return sum;
}

#endif /* INCLUDES_VCLOCK_H */
