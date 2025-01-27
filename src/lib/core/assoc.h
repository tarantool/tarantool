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

static inline uint32_t
mh_ptr_hash(const void *ptr)
{
	uintptr_t u = (uintptr_t)ptr;
	return u ^ (u >> 32);
}

/**
 * Set: (void *)
 */
#define mh_name _ptr
#define mh_key_t void *
#define mh_node_t void *
#define mh_arg_t void *
#define mh_hash(a, arg) (mh_ptr_hash(*(a)))
#define mh_hash_key(a, arg) (mh_ptr_hash(a))
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
 * Map: (void *) => (void *)
 */
#define mh_name _ptrptr
#define mh_key_t void *
struct mh_ptrptr_node_t {
	mh_key_t key;
	void *val;
};

#define mh_node_t struct mh_ptrptr_node_t
#define mh_arg_t void *
#define mh_hash(a, arg) ((uintptr_t)a->key >> 5)
#define mh_hash_key(a, arg) ((uintptr_t)a >> 5)
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
mh_strn_hash(const char *str, uint32_t len)
{
	uint32_t h = MH_STRN_HASH_SEED;
	uint32_t carry = 0;
	PMurHash32_Process(&h, &carry, str, len);
	return PMurHash32_Result(h, carry, len);
}

#define mh_name _strnptr
struct mh_strnptr_key_t {
	const char *str;
	uint32_t len;
	uint32_t hash;
};
#define mh_key_t struct mh_strnptr_key_t *

struct mh_strnptr_node_t {
	const char *str;
	uint32_t len;
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
mh_strnptr_find_str(struct mh_strnptr_t *h, const char *str, uint32_t len)
{
	uint32_t hash = mh_strn_hash(str, len);
	struct mh_strnptr_key_t key = {str, len, hash};
	return mh_strnptr_find(h, &key, NULL);
}

/*
 * Map: (char * with length) => (uint32_t)
 */
#define mh_name _strnu32
/**
 * Key of `mh_strnu32_node_t` hash table.
 */
struct mh_strnu32_key_t {
	/* Key string. */
	const char *str;
	/* Key length. */
	size_t len;
	/* Key hash calculated using `mh_strn_hash`. */
	uint32_t hash;
};

#define mh_key_t struct mh_strnu32_key_t *

/**
 * Node of `mh_strnu32_node_t` hash table.
 */
struct mh_strnu32_node_t {
	/* Key string. */
	const char *str;
	/* Key length. */
	size_t len;
	/* Key hash calculated using `mh_strn_hash`. */
	uint32_t hash;
	/* Mapped value. */
	uint32_t val;
};

#define mh_node_t struct mh_strnu32_node_t

#define mh_arg_t void *
#define mh_hash(a, arg) ((a)->hash)
#define mh_hash_key(a, arg) mh_hash(a, arg)
#define mh_cmp(a, b, arg) ((a)->len != (b)->len || \
			   memcmp((a)->str, (b)->str, (a)->len))
#define mh_cmp_key(a, b, arg) mh_cmp(a, b, arg)
#include "salad/mhash.h"

/**
 * Helper for looking up strings in `mh_strnu32_t` hash table.
 */
static inline mh_int_t
mh_strnu32_find_str(struct mh_strnu32_t *h, const char *str, uint32_t len)
{
	uint32_t hash = mh_strn_hash(str, len);
	struct mh_strnu32_key_t key = {str, len, hash};
	return mh_strnu32_find(h, &key, NULL);
}

/*
 * Map: (const char *, uint32_t, const char *, uint32_t) => (void *)
 */

#define mh_name _strnstrnptr
/**
 * Key of `mh_strnstrnptr_node_t` hash table.
 */
struct mh_strnstrnptr_key_t {
	/* First string. */
	const char *s1;
	/* First string length. */
	uint32_t s1_len;
	/* Second string. */
	const char *s2;
	/* Second string length. */
	uint32_t s2_len;
	/* Key hash calculated using `mh_strnstrnptr_hash`. */
	uint32_t hash;
};

/**
 * Node of `mh_strnstrnptr_node_t` hash table.
 */
struct mh_strnstrnptr_node_t {
	/* First string. */
	const char *s1;
	/* First string length. */
	uint32_t s1_len;
	/* Second string. */
	const char *s2;
	/* Second string length. */
	uint32_t s2_len;
	/* Key hash calculated using `mh_strnstrnptr_hash`. */
	uint32_t hash;
	/* Mapped value. */
	void *val;
};

#define mh_key_t struct mh_strnstrnptr_key_t *
#define mh_node_t struct mh_strnstrnptr_node_t

#define mh_arg_t void *
#define mh_hash(a, arg) ((a)->hash)
#define mh_hash_key(a, arg) ((a)->hash)
#define mh_cmp(a, b, arg) ((a)->s1_len != (b)->s1_len || \
			   (a)->s2_len != (b)->s2_len || \
			   memcmp((a)->s1, (b)->s1, (a)->s1_len) || \
			   memcmp((a)->s2, (b)->s2, (a)->s2_len))
#define mh_cmp_key(a, b, arg) mh_cmp(a, b, arg)
#include "salad/mhash.h"

static inline uint32_t
mh_strnstrnptr_hash(const char *s1, uint32_t s1_len,
		    const char *s2, uint32_t s2_len)
{
	uint32_t h = MH_STRN_HASH_SEED;
	uint32_t carry = 0;
	PMurHash32_Process(&h, &carry, s1, s1_len);
	PMurHash32_Process(&h, &carry, s2, s2_len);
	return PMurHash32_Result(h, carry, s1_len + s2_len);
};

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
