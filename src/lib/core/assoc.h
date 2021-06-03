/*
 * Copyright 2010-2016 Tarantool AUTHORS: please see AUTHORS file.
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
#include <stdint.h>
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#if !MH_SOURCE
#define MH_UNDEF
#endif

#include <PMurHash.h>

/*
 * Set: (i32)
 */
#define mh_name _i32
#define mh_key_t uint32_t
#define mh_node_t uint32_t
#define mh_arg_t void *
#define mh_hash(a, arg) (*(a))
#define mh_hash_key(a, arg) (a)
#define mh_cmp(a, b, arg) (*(a) != *(b))
#define mh_cmp_key(a, b, arg) ((a) != *(b))
#include "salad/mhash.h"

/*
 * Map: (i32) => (void *)
 */
#define mh_name _i32ptr
#define mh_key_t uint32_t
struct mh_i32ptr_node_t {
	mh_key_t key;
	void *val;
};

#define mh_node_t struct mh_i32ptr_node_t
#define mh_arg_t void *
#define mh_hash(a, arg) (a->key)
#define mh_hash_key(a, arg) (a)
#define mh_cmp(a, b, arg) ((a->key) != (b->key))
#define mh_cmp_key(a, b, arg) ((a) != (b->key))
#include "salad/mhash.h"

/*
 * Map: (i64) => (void *)
 */
#define mh_name _i64ptr
#define mh_key_t uint64_t
struct mh_i64ptr_node_t {
	mh_key_t key;
	void *val;
};

#define mh_node_t struct mh_i64ptr_node_t
#define mh_arg_t void *
#define mh_hash(a, arg) (a->key)
#define mh_hash_key(a, arg) (a)
#define mh_cmp(a, b, arg) ((a->key) != (b->key))
#define mh_cmp_key(a, b, arg) ((a) != (b->key))
#include "salad/mhash.h"

/*
 * Map: (char * with length) => (void *)
 */
enum {
	MH_STRN_HASH_SEED = 13U
};

static inline uint32_t
mh_strn_hash(const char *str, size_t len)
{
	uint32_t h = MH_STRN_HASH_SEED;
	uint32_t carry = 0;
	PMurHash32_Process(&h, &carry, str, len);
	return PMurHash32_Result(h, carry, len);
}

#define mh_name _strnptr
struct mh_strnptr_key_t {
	const char *str;
	size_t len;
	uint32_t hash;
};
#define mh_key_t struct mh_strnptr_key_t *

struct mh_strnptr_node_t {
	const char *str;
	size_t len;
	uint32_t hash;
	void *val;
};
#define mh_node_t struct mh_strnptr_node_t

#define mh_arg_t void *
#define mh_hash(a, arg) ((a)->hash)
#define mh_hash_key(a, arg) ((a)->hash)
#define mh_cmp(a, b, arg) ((a)->len != (b)->len || \
			    strncmp((a)->str, (b)->str, (a)->len))
#define mh_cmp_key(a, b, arg) mh_cmp(a, b, arg)
#include "salad/mhash.h"

static inline mh_int_t
mh_strnptr_find_inp(struct mh_strnptr_t *h, const char *str, size_t len)
{
	uint32_t hash = mh_strn_hash(str, len);
	struct mh_strnptr_key_t key = {str, len, hash};
	return mh_strnptr_find(h, &key, NULL);
};


#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
