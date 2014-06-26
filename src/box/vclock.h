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
#include <limits.h>
#include <assert.h>

#define RB_COMPACT 1
#include <third_party/rb.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum { VCLOCK_MAX = 16 };

/** Cluster vector clock */
struct vclock {
	int64_t lsn[VCLOCK_MAX];
	rb_node(struct vclock) link;
};

/* Server id, coordinate */
struct vclock_c {
	uint32_t id;
	int64_t lsn;
};

#define vclock_foreach(vclock, var) \
	for (struct vclock_c (var) = {0, 0}; \
	     (var).id < VCLOCK_MAX; (var).id++) \
		if (((var).lsn = (vclock)->lsn[(var).id]) >= 0)

static inline void
vclock_create(struct vclock *vclock)
{
	memset(vclock, 0xff, sizeof(*vclock));
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
	int64_t signt = 0;
	vclock_foreach(vclock, server)
		signt += server.lsn;
	return signt;
}

int64_t
vclock_follow(struct vclock *vclock, uint32_t server_id, int64_t lsn);

/**
 * \brief Format vclock to YAML-compatible string representation:
 * { server_id: lsn, server_id:lsn })
 * \param vclock vclock
 * \return fomatted string. This pointer should be passed to free(3) to
 * release the allocated storage when it is no longer needed.
 */
char *
vclock_to_string(const struct vclock *vclock);

/**
 * \brief Fill vclock from string representation.
 * \param vclock vclock
 * \param str string to parse
 * \retval 0 on sucess
 * \retval error offset on error (indexed from 1)
 * \sa vclock_to_string()
 */
size_t
vclock_from_string(struct vclock *vclock, const char *str);

enum { VCLOCK_ORDER_UNDEFINED = INT_MAX };

/**
 * \brief Compare vclocks
 * \param a vclock
 * \param b vclock
 * \retval 1 if \a vclock is ordered after \a other
 * \retval -1 if \a vclock is ordered before than \a other
 * \retval 0 if vclocks are equal
 * \retval VCLOCK_ORDER_UNDEFINED if vclocks are concurrent
 */
static inline int
vclock_compare(const struct vclock *a, const struct vclock *b)
{
	bool le = true, ge = true;
	for (uint32_t server_id = 0; server_id < VCLOCK_MAX; server_id++) {
		int64_t lsn_a = vclock_get(a, server_id);
		int64_t lsn_b = vclock_get(b, server_id);
		le = le && lsn_a <= lsn_b;
		ge = ge && lsn_a >= lsn_b;
		if (!ge && !le)
			return VCLOCK_ORDER_UNDEFINED;
	}
	if (ge && !le)
		return 1;
	if (le && !ge)
		return -1;
	return 0;
}

/**
 * @brief vclockset - a set of vclocks
 */
typedef rb_tree(struct vclock) vclockset_t;
rb_proto(, vclockset_, vclockset_t, struct vclock);

/**
 * @brief Inclusive search
 * @param set
 * @param key
 * @return a vclock that <= than \a key
 */
static inline struct vclock *
vclockset_isearch(vclockset_t *set, struct vclock *key)
{
	struct vclock *res = vclockset_psearch(set, key);
	while (res != NULL) {
		if (vclock_compare(res, key) <= 0)
			return res;
		res = vclockset_prev(set, res);
	}
	return NULL;
}

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
