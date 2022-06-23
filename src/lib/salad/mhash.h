/*
 * *No header guard*: the header is allowed to be included twice
 * with different sets of defines.
 */
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
/* The MIT License

   Copyright (c) 2008, by Attractive Chaos <attractivechaos@aol.co.uk>

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

#ifndef MH_INCREMENTAL_RESIZE
#define MH_INCREMENTAL_RESIZE 1
#endif

#ifndef MH_FAST_MOD
#define MH_FAST_MOD 1
#endif /* MH_FAST_MOD */

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include <trivia/util.h>

#define mh_cat(a, b) mh##a##_##b
#define mh_ecat(a, b) mh_cat(a, b)
#define _mh(x) mh_ecat(mh_name, x)

#define mh_unlikely(x)  __builtin_expect((x),0)

/* Modular multiplicative inverse computation. */
#define mh_mmi(p) (UINT64_C(0xFFFFFFFFFFFFFFFF) / (p) + 1)

#if MH_FAST_MOD && __SIZEOF_INT128__
#define mh_mul_u64_u32(u64, u32) \
	({ (uint32_t)(((__uint128_t)(u64) * (u32)) >> 64); })
#define mh_mod(n, mmi, d) mh_mul_u64_u32((n) * (mmi), (d))
#define mh_mod_const(n, d) mh_mul_u64_u32((n) * mh_mmi(d), (d))
#define mh_mod_prime(n, p) \
	mh_mod((n), __ac_primes_mmi_list[(p)], __ac_prime_list[(p)])
#define mh_mod_prime_minus_one(n, p) \
	mh_mod((n), __ac_primes_minus_one_mmi_list[(p)], \
	       __ac_primes_minus_one_list[(p)])
#else /* MH_FAST_MOD && __SIZEOF_INT128__ */
#define mh_mod(n, mmi, d) ({ (n) % (d); })
#define mh_mod_const(n, d) mh_mod((n), 0, (d))
#define mh_mod_prime(n, p) mh_mod((n), 0, __ac_prime_list[(p)])
#define mh_mod_prime_minus_one(n, p) \
	mh_mod((n), 0, __ac_primes_minus_one_list[(p)])
#endif /* MH_FAST_MOD */

#ifndef MH_TYPEDEFS
#define MH_TYPEDEFS 1
typedef uint32_t mh_int_t;
#endif /* MH_TYPEDEFS */

#ifndef MH_HEADER
#define MH_HEADER

#ifndef __ac_HASH_PRIME_SIZE
#define __ac_HASH_PRIME_SIZE 31
#define PRIMES_LIST(_) \
	_(3ul),		 _(11ul),	  _(23ul),		_(53ul),\
	_(97ul),	 _(193ul),	  _(389ul),		_(769ul),\
	_(1543ul),	 _(3079ul),	  _(6151ul),		_(12289ul),\
	_(24593ul),	 _(49157ul),	  _(98317ul),		_(196613ul),\
	_(393241ul),	 _(786433ul),	  _(1572869ul),		_(3145739ul),\
	_(6291469ul),	 _(12582917ul),	  _(25165843ul),	_(50331653ul),\
	_(100663319ul),	 _(201326611ul),  _(402653189ul),	_(805306457ul),\
	_(1610612741ul), _(3221225473ul), _(4294967291ul)
#define PRIME(p) (p)
static const mh_int_t __ac_prime_list[__ac_HASH_PRIME_SIZE] = {
	PRIMES_LIST(PRIME)
};

#define PRIME_MINUS_ONE(p) ((p) - 1)
static const mh_int_t __ac_primes_minus_one_list[__ac_HASH_PRIME_SIZE] = {
	PRIMES_LIST(PRIME_MINUS_ONE)
};

#undef PRIME_MINUS_ONE
#undef PRIME
#if MH_FAST_MOD
static const uint64_t __ac_primes_mmi_list[__ac_HASH_PRIME_SIZE] = {
	PRIMES_LIST(mh_mmi)
};

#define PRIME_MINUS_ONE_MMI(p) mh_mmi((p) - 1)
static const uint64_t __ac_primes_minus_one_mmi_list[__ac_HASH_PRIME_SIZE] = {
	PRIMES_LIST(PRIME_MINUS_ONE_MMI)
};

#undef PRIME_MINUS_ONE_MMI
#endif /* MH_FAST_MOD */
#undef PRIMES_LIST
#undef PRIMES_MINUS_ONE_LIST
#endif /* __ac_HASH_PRIME_SIZE */

#ifndef mh_bytemap
#define mh_bytemap 0
#endif

struct _mh(t) {
	mh_node_t *p;
#if !mh_bytemap
	uint32_t *b;
#else
	uint8_t *b;
#endif
	mh_int_t n_dirty;
	mh_int_t size;
	mh_int_t upper_bound;
	mh_int_t prime;

	mh_int_t resize_cnt;
	mh_int_t resize_position;
	mh_int_t batch;
	struct _mh(t) *shadow;
};

#if !mh_bytemap
#define mh_exist(h, i)		({ h->b[i >> 4] & (1 << (i % 16)); })
#define mh_dirty(h, i)		({ h->b[i >> 4] & (1u << (i % 16 + 16)); })
#define mh_gethk(hash)		(1)
#define mh_mayeq(h, i, hk)	mh_exist(h, i)

#define mh_setfree(h, i)	({ h->b[i >> 4] &= ~(1 << (i % 16)); })
#define mh_setexist(h, i, hk)	({ h->b[i >> 4] |= (1 << (i % 16)); })
#define mh_setdirty(h, i)	({ h->b[i >> 4] |= (1u << (i % 16 + 16)); })
#else
#define mh_exist(h, i)		({ h->b[i] & 0x7f; })
#define mh_dirty(h, i)		({ h->b[i] & 0x80; })
#define mh_gethk(hash)		({ mh_mod_const(hash, 127) + 1; })
#define mh_mayeq(h, i, hk)	({ mh_exist(h, i) == hk; })

#define mh_setfree(h, i)	({ h->b[i] &= 0x80; })
#define mh_setexist(h, i, hk)	({ h->b[i] |= hk; })
#define mh_setdirty(h, i)	({ h->b[i] |= 0x80; })
#endif

#define mh_node(h, i)		((const mh_node_t *) &((h)->p[(i)]))
#define mh_size(h)		({ (h)->size;		})
#define mh_capacity(h)		({ __ac_prime_list[(h)->prime]; })
#define mh_begin(h)		({ 0;			})
#define mh_end(h)		({ __ac_prime_list[(h)->prime]; })

#define mh_first(h) ({						\
	mh_int_t i;						\
	for (i = 0; i < mh_end(h); i++) {			\
		if (mh_exist(h, i))				\
			break;					\
	}							\
	i;							\
})

#define mh_next(h, i) ({					\
	mh_int_t n = i;						\
	if (n < mh_end(h)) {					\
		for (n = i + 1; n < mh_end(h); n++) {		\
			if (mh_exist(h, n))			\
				break;				\
		}						\
	}							\
	n;							\
})

#define mh_foreach(h, i) \
	for (i = mh_first(h); i < mh_end(h); i = mh_next(h, i))

#define MH_DENSITY 0.7

struct _mh(t) * _mh(new)();
void _mh(clear)(struct _mh(t) *h);
void _mh(delete)(struct _mh(t) *h);
void _mh(resize)(struct _mh(t) *h, mh_arg_t arg);
void _mh(start_resize)(struct _mh(t) *h, mh_int_t buckets, mh_int_t batch,
		       mh_arg_t arg);
void _mh(reserve)(struct _mh(t) *h, mh_int_t size, mh_arg_t arg);
void NOINLINE _mh(del_resize)(struct _mh(t) *h, mh_int_t x,
					       mh_arg_t arg);
size_t _mh(memsize)(struct _mh(t) *h);
void _mh(dump)(struct _mh(t) *h);

#define put_slot(h, node, exist, arg) \
	_mh(put_slot)(h, node, exist, arg)

static inline mh_node_t *
_mh(node)(struct _mh(t) *h, mh_int_t x)
{
	return (mh_node_t *) &(h->p[x]);
}


static inline mh_int_t
_mh(next_slot)(mh_int_t slot, mh_int_t inc, mh_int_t size)
{
	slot += inc;
	return slot >= size ? slot - size : slot;
}

#if defined(mh_hash_key) && defined(mh_cmp_key)
/**
 * If it is necessary to search by something different
 * than a hash node, define mh_hash_key and mh_eq_key
 * and use mh_find().
 */
static inline mh_int_t
_mh(find)(struct _mh(t) *h, mh_key_t key, mh_arg_t arg)
{
	(void) arg;

	mh_int_t k = mh_hash_key(key, arg);
	uint8_t hk = mh_gethk(k); (void)hk;
	mh_int_t i = mh_mod_prime(k, h->prime);
	mh_int_t inc = 1 + mh_mod_prime_minus_one(k, h->prime);
	for (;;) {
		if ((mh_mayeq(h, i, hk) &&
		    !mh_cmp_key(key, mh_node(h, i), arg)))
			return i;

		if (!mh_dirty(h, i))
			return mh_end(h);

		i = _mh(next_slot)(i, inc, mh_capacity(h));
	}
}
#endif

static inline mh_int_t
_mh(get)(struct _mh(t) *h, const mh_node_t *node,
	 mh_arg_t arg)
{
	(void) arg;

	mh_int_t k = mh_hash(node, arg);
	uint8_t hk = mh_gethk(k); (void)hk;
	mh_int_t i = mh_mod_prime(k, h->prime);
	mh_int_t inc = 1 + mh_mod_prime_minus_one(k, h->prime);
	for (;;) {
		if ((mh_mayeq(h, i, hk) && !mh_cmp(node, mh_node(h, i), arg)))
			return i;

		if (!mh_dirty(h, i))
			return mh_end(h);

		i = _mh(next_slot)(i, inc, mh_capacity(h));
	}
}

static inline mh_int_t
_mh(random)(struct _mh(t) *h, mh_int_t rnd)
{
	mh_int_t res = mh_next(h, mh_mod_prime(rnd, h->prime));
	if (res != mh_end(h))
		return res;
	return mh_first(h);
}

static inline mh_int_t
_mh(put_slot)(struct _mh(t) *h, const mh_node_t *node, int *exist,
	      mh_arg_t arg)
{
	(void) arg;

	mh_int_t k = mh_hash(node, arg); /* hash key */
	uint8_t hk = mh_gethk(k); (void)hk;
	mh_int_t i = mh_mod_prime(k, h->prime); /* offset in the hash table. */
	/* overflow chain increment. */
	mh_int_t inc = 1 + mh_mod_prime_minus_one(k, h->prime);

	*exist = 1;
	/* Skip through all collisions. */
	while (mh_exist(h, i)) {
		if (mh_mayeq(h, i, hk) && !mh_cmp(node, mh_node(h, i), arg))
			return i;               /* Found a duplicate. */
		/*
		 * Mark this link as part of a collision chain. The
		 * chain always ends with a non-marked link.
		 * Note: the collision chain for this key may share
		 * links with collision chains of other keys.
		 */
		mh_setdirty(h, i);
		i = _mh(next_slot)(i, inc, mh_capacity(h));
	}
	/*
	 * Found an unused, but possibly dirty slot. Use it.
	 * However, if this is a dirty slot, first check that
	 * there are no duplicates down the collision chain. The
	 * current link can also be from a collision chain of some
	 * other key, but this is can't be established, so check
	 * anyway.
	 */
	mh_int_t save_i = i;
	while (mh_dirty(h, i)) {
		i = _mh(next_slot)(i, inc, mh_capacity(h));

		if (mh_mayeq(h, i, hk) && !mh_cmp(mh_node(h, i), node, arg))
			return i;               /* Found a duplicate. */
	}
	/* Reached the end of the collision chain: no duplicates. */
	*exist = 0;
	h->size++;
	if (!mh_dirty(h, save_i))
		h->n_dirty++;
	mh_setexist(h, save_i, hk);
	return save_i;
}

/**
 * Find a node in the hash and replace it with a new value.
 * Save the old node in ret pointer, if it is provided.
 * If the old node didn't exist, just insert the new node.
 *
 * @retval != mh_end()   pos of the new node, ret is either NULL
 *                       or copy of the old node
 */
static inline mh_int_t
_mh(put)(struct _mh(t) *h, const mh_node_t *node, mh_node_t **ret,
	 mh_arg_t arg)
{
	mh_int_t x = mh_end(h);
	int exist;

	assert(h->size < mh_capacity(h));

#if MH_INCREMENTAL_RESIZE
	if (mh_unlikely(h->resize_position > 0))
		_mh(resize)(h, arg);
	else if (mh_unlikely(h->n_dirty >= h->upper_bound)) {
		_mh(start_resize)(h, mh_capacity(h) + 1, 0, arg);
	}
	if (h->resize_position)
		_mh(put)(h->shadow, node, NULL, arg);
#else
	if (mh_unlikely(h->n_dirty >= h->upper_bound)) {
		_mh(start_resize)(h, mh_capacity(h) + 1, h->size, arg);
	}
#endif
	x = put_slot(h, node, &exist, arg);

	if (ret) {
		if (exist)
			memcpy(*ret, &(h->p[x]), sizeof(mh_node_t));
		else
			*ret = NULL;
	}
	memcpy(&(h->p[x]), node, sizeof(mh_node_t));
	return x;
}

static inline void
_mh(del)(struct _mh(t) *h, mh_int_t x,
	 mh_arg_t arg)
{
	if (x != mh_end(h) && mh_exist(h, x)) {
		mh_setfree(h, x);
		h->size--;
		if (!mh_dirty(h, x))
			h->n_dirty--;
#if MH_INCREMENTAL_RESIZE
		if (mh_unlikely(h->resize_position))
			_mh(del_resize)(h, x, arg);
#endif
	}
}
#endif

static inline void
_mh(remove)(struct _mh(t) *h, const mh_node_t *node,
	 mh_arg_t arg)
{
	mh_int_t k = _mh(get)(h, node, arg);
	if (k != mh_end(h))
		_mh(del)(h, k, arg);
}

#ifdef MH_SOURCE

NOINLINE void
_mh(del_resize)(struct _mh(t) *h, mh_int_t x,
		mh_arg_t arg)
{
	struct _mh(t) *s = h->shadow;
	mh_int_t y = _mh(get)(s, (const mh_node_t *) &(h->p[x]),
			      arg);
	_mh(del)(s, y, arg);
	_mh(resize)(h, arg);
}

struct _mh(t) *
_mh(new)()
{
	struct _mh(t) *h = (struct _mh(t) *)xcalloc(1, sizeof(*h));
	h->shadow = (struct _mh(t) *)xcalloc(1, sizeof(*h));
	h->prime = 0;
	h->p = (mh_node_t *)xcalloc(mh_capacity(h), sizeof(mh_node_t));
#if !mh_bytemap
	h->b = (uint32_t *)xcalloc(mh_capacity(h) / 16 + 1, sizeof(uint32_t));
#else
	h->b = (uint8_t *)xcalloc(mh_capacity(h), sizeof(uint8_t));
#endif
	h->upper_bound = mh_capacity(h) * MH_DENSITY;
	return h;
}

void
_mh(clear)(struct _mh(t) *h)
{
	mh_int_t n_buckets = __ac_prime_list[h->prime];
	mh_node_t *p = (mh_node_t *)xcalloc(n_buckets, sizeof(mh_node_t));
#if !mh_bytemap
	uint32_t *b = (uint32_t *)xcalloc(n_buckets / 16 + 1, sizeof(uint32_t));
#else
	uint8_t *b = (uint8_t *)xcalloc(n_buckets, sizeof(uint8_t));
#endif
	if (h->shadow->p) {
		free(h->shadow->p);
		free(h->shadow->b);
		memset(h->shadow, 0, sizeof(*h->shadow));
	}
	free(h->p);
	free(h->b);
	h->prime = 0;
	h->p = p;
	h->b = b;
	h->size = 0;
	h->upper_bound = mh_capacity(h) * MH_DENSITY;
}

void
_mh(delete)(struct _mh(t) *h)
{
	if (h->shadow->p) {
		free(h->shadow->p);
		free(h->shadow->b);
		memset(h->shadow, 0, sizeof(*h->shadow));
	}
	free(h->shadow);
	free(h->b);
	free(h->p);
	free(h);
}

/** Calculate hash size. */
size_t
_mh(memsize)(struct _mh(t) *h)
{
    size_t sz = 2 * sizeof(struct _mh(t));

    sz += mh_capacity(h) * sizeof(mh_node_t);
#if !mh_bytemap
    sz += (mh_capacity(h) / 16 + 1) * sizeof(uint32_t);
#else
    sz += mh_capacity(h);
#endif
    if (h->resize_position) {
	    h = h->shadow;
	    sz += mh_capacity(h) * sizeof(mh_node_t);
#if !mh_bytemap
	    sz += (mh_capacity(h) / 16 + 1) * sizeof(uint32_t);
#else
	    sz += mh_capacity(h);
#endif
    }
    return sz;
}

void
_mh(resize)(struct _mh(t) *h, mh_arg_t arg)
{
	struct _mh(t) *s = h->shadow;
	int exist;
#if MH_INCREMENTAL_RESIZE
	mh_int_t  batch = h->batch;
#endif
	for (mh_int_t i = h->resize_position; i < mh_end(h); i++) {
#if MH_INCREMENTAL_RESIZE
		if (batch-- == 0) {
			h->resize_position = i;
			return;
		}
#endif
		if (!mh_exist(h, i))
			continue;
		mh_int_t n = put_slot(s, mh_node(h, i), &exist, arg);
		s->p[n] = h->p[i];
	}
	free(h->p);
	free(h->b);
	if (s->size != h->size)
		abort();
	memcpy(h, s, sizeof(*h));
	h->resize_cnt++;
	memset(s, 0, sizeof(*s));
}

void
_mh(start_resize)(struct _mh(t) *h, mh_int_t buckets, mh_int_t batch,
		  mh_arg_t arg)
{
	if (h->resize_position) {
		/* resize has already been started */
		return;
	}
	if (buckets < mh_capacity(h)) {
		/* hash size is already greater than requested */
		return;
	}
	mh_int_t new_prime = h->prime;
	while (new_prime < __ac_HASH_PRIME_SIZE - 1) {
		if (__ac_prime_list[new_prime] >= buckets)
			break;
		new_prime += 1;
	}
	mh_int_t new_batch = batch > 0 ? batch : mh_capacity(h) / (256 * 1024);
	if (new_batch < 256) {
		/*
		 * Minimal batch must be greater or equal to
		 * 1 / (1 - f), where f is upper bound percent
		 * = MH_DENSITY
		 */
		new_batch = 256;
	}

	mh_int_t n_buckets = __ac_prime_list[new_prime];
	mh_node_t *p = (mh_node_t *)xmalloc(n_buckets * sizeof(mh_node_t));
#if !mh_bytemap
	uint32_t *b = (uint32_t *)xcalloc(n_buckets / 16 + 1, sizeof(uint32_t));
#else
	uint8_t *b = (uint8_t *)xcalloc(n_buckets, sizeof(uint8_t));
#endif
	h->batch = new_batch;
	struct _mh(t) *s = h->shadow;
	memcpy(s, h, sizeof(*h));
	s->resize_position = 0;
	s->prime = new_prime;
	s->upper_bound = mh_capacity(s) * MH_DENSITY;
	s->n_dirty = 0;
	s->size = 0;
	s->p = p;
	s->b = b;
	_mh(resize)(h, arg);
}

void
_mh(reserve)(struct _mh(t) *h, mh_int_t size,
	     mh_arg_t arg)
{
	_mh(start_resize)(h, size/MH_DENSITY, h->size, arg);
}

#ifndef mh_stat
#define mh_stat(buf, h) ({						\
		tbuf_printf(buf, "  capacity: %" PRIu32 CRLF		\
			    "  n_dirty: %" PRIu32 CRLF			\
			    "  size: %" PRIu32 CRLF			\
			    "  resize_cnt: %" PRIu32 CRLF		\
			    "  resize_position: %" PRIu32 CRLF,		\
			    mh_capacity(h),				\
			    h->n_dirty,					\
			    h->size,					\
			    h->resize_cnt,				\
			    h->resize_position);			\
			})
#endif

#ifdef MH_DEBUG
void
_mh(dump)(struct _mh(t) *h)
{
	printf("slots:\n");
	int k = 0;
	for (int i = 0; i < mh_end(h); i++) {
		if (mh_dirty(h, i) || mh_exist(h, i)) {
			printf("   [%i] ", i);
			if (mh_exist(h, i)) {
				/* TODO(roman): fix this printf */
				printf("   -> %p", h->p[i]);
				k++;
			}
			if (mh_dirty(h, i))
				printf(" dirty");
			printf("\n");
		}
	}
	printf("end(%i)\n", k);
}
#endif

#endif

#if defined(MH_SOURCE) || defined(MH_UNDEF)
#undef MH_HEADER
#undef mh_int_t
#undef mh_node_t
#undef mh_arg_t
#undef mh_key_t
#undef mh_name
#undef mh_hash
#undef mh_hash_key
#undef mh_cmp
#undef mh_cmp_key
#undef mh_node
#undef mh_dirty
#undef mh_place
#undef mh_setdirty
#undef mh_setexist
#undef mh_setvalue
#undef mh_unlikely
#undef slot
#undef slot_and_dirty
#undef MH_DENSITY
#undef mh_bytemap
#endif

#undef mh_cat
#undef mh_ecat
#undef _mh
