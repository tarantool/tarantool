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

#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include <trivia/util.h>

#define mh_cat(a, b) mh##a##_##b
#define mh_ecat(a, b) mh_cat(a, b)
#define _mh(x) mh_ecat(mh_name, x)

#define mh_unlikely(x)  __builtin_expect((x),0)

#ifndef MH_TYPEDEFS
#define MH_TYPEDEFS 1
typedef uint32_t mh_int_t;
#endif /* MH_TYPEDEFS */

#ifndef MH_HEADER
#define MH_HEADER

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
	mh_int_t n_buckets;
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
#define mh_gethk(hash)		({ (hash) % 127 + 1; })
#define mh_mayeq(h, i, hk)	({ mh_exist(h, i) == hk; })

#define mh_setfree(h, i)	({ h->b[i] &= 0x80; })
#define mh_setexist(h, i, hk)	({ h->b[i] |= hk; })
#define mh_setdirty(h, i)	({ h->b[i] |= 0x80; })
#endif

#define mh_node(h, i)		((const mh_node_t *) &((h)->p[(i)]))
#define mh_size(h)		({ (h)->size;		})
#define mh_capacity(h)		({ (h)->n_buckets;	})
#define mh_begin(h)		({ 0;			})
#define mh_end(h)		({ (h)->n_buckets;	})

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
int _mh(start_resize)(struct _mh(t) *h, mh_int_t buckets, mh_int_t batch,
		      mh_arg_t arg);
int _mh(reserve)(struct _mh(t) *h, mh_int_t size,
		  mh_arg_t arg);
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
	mh_int_t i = k % h->n_buckets;
	mh_int_t inc = 1 + k % (h->n_buckets - 1);
	for (;;) {
		if ((mh_mayeq(h, i, hk) &&
		    !mh_cmp_key(key, mh_node(h, i), arg)))
			return i;

		if (!mh_dirty(h, i))
			return h->n_buckets;

		i = _mh(next_slot)(i, inc, h->n_buckets);
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
	mh_int_t i = k % h->n_buckets;
	mh_int_t inc = 1 + k % (h->n_buckets - 1);
	for (;;) {
		if ((mh_mayeq(h, i, hk) && !mh_cmp(node, mh_node(h, i), arg)))
			return i;

		if (!mh_dirty(h, i))
			return h->n_buckets;

		i = _mh(next_slot)(i, inc, h->n_buckets);
	}
}

static inline mh_int_t
_mh(random)(struct _mh(t) *h, mh_int_t rnd)
{
	mh_int_t res = mh_next(h, rnd % mh_end(h));
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
	mh_int_t i = k % h->n_buckets; /* offset in the hash table. */
	mh_int_t inc = 1 + k % (h->n_buckets - 1); /* overflow chain increment. */

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
		i = _mh(next_slot)(i, inc, h->n_buckets);
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
		i = _mh(next_slot)(i, inc, h->n_buckets);

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
 * @retval  mh_end()     out of memory, ret is unchanged.
 */
static inline mh_int_t
_mh(put)(struct _mh(t) *h, const mh_node_t *node, mh_node_t **ret,
	 mh_arg_t arg)
{
	mh_int_t x = mh_end(h);
	int exist;
	if (h->size == h->n_buckets)
		/* no one free elements in the hash table */
		goto put_done;

#if MH_INCREMENTAL_RESIZE
	if (mh_unlikely(h->resize_position > 0))
		_mh(resize)(h, arg);
	else if (mh_unlikely(h->n_dirty >= h->upper_bound)) {
		if (_mh(start_resize)(h, h->n_buckets + 1, 0, arg))
			goto put_done;
	}
	if (h->resize_position)
		_mh(put)(h->shadow, node, NULL, arg);
#else
	if (mh_unlikely(h->n_dirty >= h->upper_bound)) {
		if (_mh(start_resize)(h, h->n_buckets + 1, h->size,
				      arg))
			goto put_done;
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

put_done:
	return x;
}

static inline void
_mh(del)(struct _mh(t) *h, mh_int_t x,
	 mh_arg_t arg)
{
	if (x != h->n_buckets && mh_exist(h, x)) {
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

#ifndef __ac_HASH_PRIME_SIZE
#define __ac_HASH_PRIME_SIZE 31
static const mh_int_t __ac_prime_list[__ac_HASH_PRIME_SIZE] = {
	3ul,		11ul,		23ul,		53ul,
	97ul,		193ul,		389ul,		769ul,
	1543ul,		3079ul,		6151ul,		12289ul,
	24593ul,	49157ul,	98317ul,	196613ul,
	393241ul,	786433ul,	1572869ul,	3145739ul,
	6291469ul,	12582917ul,	25165843ul,	50331653ul,
	100663319ul,	201326611ul,	402653189ul,	805306457ul,
	1610612741ul,	3221225473ul,	4294967291ul
};
#endif /* __ac_HASH_PRIME_SIZE */

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
	struct _mh(t) *h = (struct _mh(t) *) calloc(1, sizeof(*h));
	h->shadow = (struct _mh(t) *) calloc(1, sizeof(*h));
	h->prime = 0;
	h->n_buckets = __ac_prime_list[h->prime];
	h->p = (mh_node_t *) calloc(h->n_buckets, sizeof(mh_node_t));
#if !mh_bytemap
	h->b = (uint32_t *) calloc(h->n_buckets / 16 + 1, sizeof(uint32_t));
#else
	h->b = (uint8_t *) calloc(h->n_buckets, sizeof(uint8_t));
#endif
	h->upper_bound = h->n_buckets * MH_DENSITY;
	return h;
}

void
_mh(clear)(struct _mh(t) *h)
{
	if (h->shadow->p) {
		free(h->shadow->p);
		free(h->shadow->b);
		memset(h->shadow, 0, sizeof(*h->shadow));
	}
	free(h->p);
	free(h->b);
	h->prime = 0;
	h->n_buckets = __ac_prime_list[h->prime];
	h->p = (mh_node_t *) calloc(h->n_buckets, sizeof(mh_node_t));
#if !mh_bytemap
	h->b = (uint32_t *) calloc(h->n_buckets / 16 + 1, sizeof(uint32_t));
#else
	h->b = (uint8_t *) calloc(h->n_buckets, sizeof(uint8_t));
#endif
	h->size = 0;
	h->upper_bound = h->n_buckets * MH_DENSITY;
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

    sz += h->n_buckets * sizeof(mh_node_t);
#if !mh_bytemap
    sz += (h->n_buckets / 16 + 1) * sizeof(uint32_t);
#else
    sz += h->n_buckets;
#endif
    if (h->resize_position) {
	    h = h->shadow;
	    sz += h->n_buckets * sizeof(mh_node_t);
#if !mh_bytemap
	    sz += (h->n_buckets / 16 + 1) * sizeof(uint32_t);
#else
	    sz += h->n_buckets;
#endif
    }
    return sz;
}

void
_mh(resize)(struct _mh(t) *h,
	    mh_arg_t arg)
{
	struct _mh(t) *s = h->shadow;
	int exist;
#if MH_INCREMENTAL_RESIZE
	mh_int_t  batch = h->batch;
#endif
	for (mh_int_t i = h->resize_position; i < h->n_buckets; i++) {
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

int
_mh(start_resize)(struct _mh(t) *h, mh_int_t buckets, mh_int_t batch,
		  mh_arg_t arg)
{
	if (h->resize_position) {
		/* resize has already been started */
		return 0;
	}
	if (buckets < h->n_buckets) {
		/* hash size is already greater than requested */
		return 0;
	}
	while (h->prime < __ac_HASH_PRIME_SIZE - 1) {
		if (__ac_prime_list[h->prime] >= buckets)
			break;
		h->prime += 1;
	}

	h->batch = batch > 0 ? batch : h->n_buckets / (256 * 1024);
	if (h->batch < 256) {
		/*
		 * Minimal batch must be greater or equal to
		 * 1 / (1 - f), where f is upper bound percent
		 * = MH_DENSITY
		 */
		h->batch = 256;
	}

	struct _mh(t) *s = h->shadow;
	memcpy(s, h, sizeof(*h));
	s->resize_position = 0;
	s->n_buckets = __ac_prime_list[h->prime];
	s->upper_bound = s->n_buckets * MH_DENSITY;
	s->n_dirty = 0;
	s->size = 0;
	s->p = (mh_node_t *) malloc(s->n_buckets * sizeof(mh_node_t));
	if (s->p == NULL)
		return -1;
#if !mh_bytemap
	s->b = (uint32_t *) calloc(s->n_buckets / 16 + 1, sizeof(uint32_t));
#else
	s->b = (uint8_t *) calloc(s->n_buckets, sizeof(uint8_t));
#endif
	if (s->b == NULL) {
		free(s->p);
		s->p = NULL;
		return -1;
	}
	_mh(resize)(h, arg);

	return 0;
}

int
_mh(reserve)(struct _mh(t) *h, mh_int_t size,
	     mh_arg_t arg)
{
	return _mh(start_resize)(h, size/MH_DENSITY, h->size, arg);
}

#ifndef mh_stat
#define mh_stat(buf, h) ({						\
		tbuf_printf(buf, "  n_buckets: %" PRIu32 CRLF		\
			    "  n_dirty: %" PRIu32 CRLF			\
			    "  size: %" PRIu32 CRLF			\
			    "  resize_cnt: %" PRIu32 CRLF		\
			    "  resize_position: %" PRIu32 CRLF,		\
			    h->n_buckets,				\
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
	for(int i = 0; i < h->n_buckets; i++) {
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
