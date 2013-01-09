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

#define mh_cat(a, b) mh##a##_##b
#define mh_ecat(a, b) mh_cat(a, b)
#define _mh(x) mh_ecat(mh_name, x)

#define mh_unlikely(x)  __builtin_expect((x),0)

#ifndef MH_TYPEDEFS
#define MH_TYPEDEFS 1
typedef uint32_t mh_int_t;
#endif /* MH_TYPEDEFS */

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

#ifndef MH_HEADER
#define MH_HEADER

struct _mh(t) {
	mh_node_t *p;
	mh_int_t *b;
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

#define mh_exist(h, i)		({ h->b[i >> 4] & (1 << (i % 16)); })
#define mh_dirty(h, i)		({ h->b[i >> 4] & (1 << (i % 16 + 16)); })

#define mh_setfree(h, i)	({ h->b[i >> 4] &= ~(1 << (i % 16)); })
#define mh_setexist(h, i)	({ h->b[i >> 4] |= (1 << (i % 16)); })
#define mh_setdirty(h, i)	({ h->b[i >> 4] |= (1 << (i % 16 + 16)); })

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

struct _mh(t) * _mh(init)();
void _mh(clear)(struct _mh(t) *h);
void _mh(destroy)(struct _mh(t) *h);
void _mh(resize)(struct _mh(t) *h, mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg);
int _mh(start_resize)(struct _mh(t) *h, mh_int_t buckets, mh_int_t batch,
		      mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg);
void _mh(reserve)(struct _mh(t) *h, mh_int_t size,
		  mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg);
void __attribute__((noinline)) _mh(del_resize)(struct _mh(t) *h, mh_int_t x,
					       mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg);
void _mh(dump)(struct _mh(t) *h);

#define put_slot(h, node, hash_arg, eq_arg) \
	_mh(put_slot)(h, node, hash_arg, eq_arg)

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

static inline mh_int_t
_mh(get)(struct _mh(t) *h, const mh_node_t *node,
	 mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg)
{
	(void) hash_arg;
	(void) eq_arg;

	mh_int_t k = mh_hash(node, hash_arg);
	mh_int_t i = k % h->n_buckets;
	mh_int_t inc = 1 + k % (h->n_buckets - 1);
	for (;;) {
		if ((mh_exist(h, i) && mh_eq(node, mh_node(h, i), eq_arg)))
			return i;

		if (!mh_dirty(h, i))
			return h->n_buckets;

		i = _mh(next_slot)(i, inc, h->n_buckets);
	}
}

static inline mh_int_t
_mh(put_slot)(struct _mh(t) *h, const mh_node_t *node,
	      mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg)
{
	(void) hash_arg;
	(void) eq_arg;

	mh_int_t k = mh_hash(node, hash_arg); /* hash key */
	mh_int_t i = k % h->n_buckets; /* offset in the hash table. */
	mh_int_t inc = 1 + k % (h->n_buckets - 1); /* overflow chain increment. */

	/* Skip through all collisions. */
	while (mh_exist(h, i)) {
		if (mh_eq(node, mh_node(h, i), eq_arg))
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

		if (mh_exist(h, i) && mh_eq(mh_node(h, i), node, eq_arg))
			return i;               /* Found a duplicate. */
	}
	/* Reached the end of the collision chain: no duplicates. */
	return save_i;
}

static inline mh_int_t
_mh(put)(struct _mh(t) *h, const mh_node_t *node,
	 mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg, int *ret)
{
	mh_int_t x = mh_end(h);
	if (h->size == h->n_buckets)
		/* no one free elements in the hash table */
		goto put_done;

#if MH_INCREMENTAL_RESIZE
	if (mh_unlikely(h->resize_position > 0))
		_mh(resize)(h, hash_arg, eq_arg);
	else if (mh_unlikely(h->n_dirty >= h->upper_bound)) {
		if (_mh(start_resize)(h, h->n_buckets + 1, 0, hash_arg, eq_arg))
			goto put_done;
	}
	if (h->resize_position)
		_mh(put)(h->shadow, node, hash_arg, eq_arg, NULL);
#else
	if (mh_unlikely(h->n_dirty >= h->upper_bound)) {
		if (_mh(start_resize)(h, h->n_buckets + 1, h->size,
				      hash_arg, eq_arg))
			goto put_done;
	}
#endif

	x = put_slot(h, node, hash_arg, eq_arg);
	int exist = mh_exist(h, x);
	if (ret)
		*ret = !exist;

	if (!exist) {
		/* add new */
		mh_setexist(h, x);
		h->size++;
		if (!mh_dirty(h, x))
			h->n_dirty++;

		memcpy(&(h->p[x]), node, sizeof(mh_node_t));
	} else {
		/* replace old */
		memcpy(&(h->p[x]), node, sizeof(mh_node_t));
	}

put_done:
	return x;
}


/**
 * Find a node in the hash and replace it with a new value.
 * Save the old node in p_old pointer, if it is provided.
 * If the old node didn't exist, just insert the new node.
 */
static inline mh_int_t
_mh(replace)(struct _mh(t) *h, const mh_node_t *node, mh_node_t **p_old,
	 mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg)
{
	mh_int_t k = _mh(get)(h, node, hash_arg, eq_arg);
	if (k == mh_end(h)) {
		/* No such node yet: insert a new one. */
		if (p_old) {
			*p_old = NULL;
		}
		return _mh(put)(h, node, hash_arg, eq_arg, NULL);
	} else {
		/*
		 * Maintain uniqueness: replace the old node
		 * with a new value.
		 */
		if (p_old) {
			/* Save the old value. */
			memcpy(*p_old, &(h->p[k]), sizeof(mh_node_t));
		}
		memcpy(&(h->p[k]), node, sizeof(mh_node_t));
		return k;
	}
}

static inline void
_mh(del)(struct _mh(t) *h, mh_int_t x,
	 mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg)
{
	if (x != h->n_buckets && mh_exist(h, x)) {
		mh_setfree(h, x);
		h->size--;
		if (!mh_dirty(h, x))
			h->n_dirty--;
#if MH_INCREMENTAL_RESIZE
		if (mh_unlikely(h->resize_position))
			_mh(del_resize)(h, x, hash_arg, eq_arg);
#endif
	}
}
#endif

static inline void
_mh(remove)(struct _mh(t) *h, const mh_node_t *node,
	 mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg)
{
	mh_int_t k = _mh(get)(h, node, hash_arg, eq_arg);
	if (k != mh_end(h))
		_mh(del)(h, k, hash_arg, eq_arg);
}

#ifdef MH_SOURCE

void __attribute__((noinline))
_mh(del_resize)(struct _mh(t) *h, mh_int_t x,
		mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg)
{
	struct _mh(t) *s = h->shadow;
	uint32_t y = _mh(get)(s, (const mh_node_t *) &(h->p[x]),
			      hash_arg, eq_arg);
	_mh(del)(s, y, hash_arg, eq_arg);
	_mh(resize)(h, hash_arg, eq_arg);
}

struct _mh(t) *
_mh(init)()
{
	struct _mh(t) *h = calloc(1, sizeof(*h));
	h->shadow = calloc(1, sizeof(*h));
	h->prime = 0;
	h->n_buckets = __ac_prime_list[h->prime];
	h->p = calloc(h->n_buckets, sizeof(mh_node_t));
	h->b = calloc(h->n_buckets / 16 + 1, sizeof(unsigned));
	h->upper_bound = h->n_buckets * MH_DENSITY;
	return h;
}

void
_mh(clear)(struct _mh(t) *h)
{
	free(h->p);
	h->prime = 0;
	h->n_buckets = __ac_prime_list[h->prime];
	h->p = calloc(h->n_buckets, sizeof(mh_node_t));
	h->upper_bound = h->n_buckets * MH_DENSITY;
}

void
_mh(destroy)(struct _mh(t) *h)
{
	free(h->shadow);
	free(h->b);
	free(h->p);
	free(h);
}

void
_mh(resize)(struct _mh(t) *h,
	    mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg)
{
	struct _mh(t) *s = h->shadow;
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
		mh_int_t n = put_slot(s, mh_node(h, i), hash_arg, eq_arg);
		s->p[n] = h->p[i];
		mh_setexist(s, n);
		s->n_dirty++;
	}
	free(h->p);
	free(h->b);
	s->size = h->size;
	memcpy(h, s, sizeof(*h));
	h->resize_cnt++;
}

int
_mh(start_resize)(struct _mh(t) *h, mh_int_t buckets, mh_int_t batch,
		  mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg)
{
	if (h->resize_position) {
		/* resize has already been started */
		return 0;
	}
	if (buckets < h->n_buckets) {
		/* hash size is already greater than requested */
		return 0;
	}
	while (h->prime < __ac_HASH_PRIME_SIZE) {
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
	s->p = malloc(s->n_buckets * sizeof(mh_node_t));
	if (s->p == NULL)
		return -1;
	s->b = calloc(s->n_buckets / 16 + 1, sizeof(unsigned));
	if (s->b == NULL) {
		free(s->p);
		s->p = NULL;
		return -1;
	}
	_mh(resize)(h, hash_arg, eq_arg);

	return 0;
}

void
_mh(reserve)(struct _mh(t) *h, mh_int_t size,
	     mh_hash_arg_t hash_arg, mh_eq_arg_t eq_arg)
{
	_mh(start_resize)(h, size/MH_DENSITY, h->size, hash_arg, eq_arg);
}

#ifndef mh_stat
#define mh_stat(buf, h) ({						\
                tbuf_printf(buf, "  n_buckets: %"PRIu32 CRLF		\
			    "  n_dirty: %"PRIu32 CRLF			\
			    "  size: %"PRIu32 CRLF			\
			    "  resize_cnt: %"PRIu32 CRLF		\
			    "  resize_position: %"PRIu32 CRLF,		\
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
#undef mh_hash_arg_t
#undef mh_eq_arg_t
#undef mh_name
#undef mh_hash
#undef mh_eq
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
#endif

#undef mh_cat
#undef mh_ecat
#undef _mh
