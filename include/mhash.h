/*
 * Copyright (C) 2011 Mail.RU
 * Copyright (C) 2011 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
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
#define MH_TYPEDEFS
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

struct _mh(pair) {
	mh_key_t key;
	mh_val_t val;
};

struct _mh(t) {
	struct _mh(pair) * p;
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

#define mh_value(h, i)		(h)->p[(i)].val
#define mh_size(h)		({ (h)->size;		})
#define mh_capacity(h)		({ (h)->n_buckets;	})
#define mh_begin(h)		({ 0;			})
#define mh_end(h)		({ (h)->n_buckets;	})

#define MH_DENSITY 0.7

struct _mh(t) * _mh(init)();
void _mh(clear)(struct _mh(t) *h);
void _mh(destroy)(struct _mh(t) *h);
void _mh(resize)(struct _mh(t) *h);
mh_int_t _mh(start_resize)(struct _mh(t) *h, mh_int_t buckets, mh_int_t batch);
void _mh(reserve)(struct _mh(t) *h, mh_int_t size);
void __attribute__((noinline)) _mh(put_resize)(struct _mh(t) *h, mh_key_t key, mh_val_t val);
void __attribute__((noinline)) _mh(del_resize)(struct _mh(t) *h, mh_int_t x);
void _mh(dump)(struct _mh(t) *h);

#define put_slot(h, key) _mh(put_slot)(h, key)

static inline mh_int_t
_mh(next_slot)(mh_int_t slot, mh_int_t inc, mh_int_t size)
{
	slot += inc;
	return slot >= size ? slot - size : slot;
}

static inline mh_int_t
_mh(get)(struct _mh(t) *h, mh_key_t key)
{
	mh_int_t k = mh_hash(key);
	mh_int_t i = k % h->n_buckets;
	mh_int_t inc = 1 + k % (h->n_buckets - 1);
	for (;;) {
		if ((mh_exist(h, i) && mh_eq(h->p[i].key, key)))
			return i;

		if (!mh_dirty(h, i))
			return h->n_buckets;

		i = _mh(next_slot)(i, inc, h->n_buckets);
	}
}

static inline mh_int_t
_mh(put_slot)(struct _mh(t) *h, mh_key_t key)
{
	mh_int_t k = mh_hash(key); /* hash key */
	mh_int_t i = k % h->n_buckets; /* offset in the hash table. */
	mh_int_t inc = 1 + k % (h->n_buckets - 1); /* overflow chain increment. */

	/* Skip through all collisions. */
	while (mh_exist(h, i)) {
		if (mh_eq(h->p[i].key, key))
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

		if (mh_exist(h, i) && mh_eq(h->p[i].key, key))
			return i;               /* Found a duplicate. */
	}
	/* Reached the end of the collision chain: no duplicates. */
	return save_i;
}

static inline mh_int_t
_mh(put)(struct _mh(t) *h, mh_key_t key, mh_val_t val, int * ret)
{
	mh_int_t x = mh_end(h);
	if (h->size == h->n_buckets)
		/* no one free elements in the hash table */
		goto put_done;

#if MH_INCREMENTAL_RESIZE
	if (mh_unlikely(h->n_dirty >= h->upper_bound || h->resize_position > 0))
		_mh(put_resize)(h, key, val);
#else
	if (mh_unlikely(h->n_dirty >= h->upper_bound))
		_mh(start_resize)(h, h->n_buckets + 1, -1);
#endif

	x = put_slot(h, key);
	int exist = mh_exist(h, x);
	if (ret)
		*ret = !exist;

	if (!exist) {
		/* add new */
		mh_setexist(h, x);
		h->size++;
		if (!mh_dirty(h, x))
			h->n_dirty++;

		h->p[x].key = key;
		h->p[x].val = val;
	} else {
		/* replace old */
		h->p[x].val = val;
	}

put_done:
	return x;
}

static inline void
_mh(del)(struct _mh(t) *h, mh_int_t x)
{
	if (x != h->n_buckets && mh_exist(h, x)) {
		mh_setfree(h, x);
		h->size--;
		if (!mh_dirty(h, x))
			h->n_dirty--;
#if MH_INCREMENTAL_RESIZE
		if (mh_unlikely(h->resize_position))
			_mh(del_resize)(h, x);
#endif
	}
}
#endif


#ifdef MH_SOURCE
void __attribute__((noinline))
_mh(put_resize)(struct _mh(t) *h, mh_key_t key, mh_val_t val)
{
	if (h->resize_position > 0)
		_mh(resize)(h);
	else
		_mh(start_resize)(h, h->n_buckets + 1, 0);
	if (h->resize_position)
		_mh(put)(h->shadow, key, val, NULL);
}


void __attribute__((noinline))
_mh(del_resize)(struct _mh(t) *h, mh_int_t x)
{
	struct _mh(t) *s = h->shadow;
	uint32_t y = _mh(get)(s, h->p[x].key);
	_mh(del)(s, y);
	_mh(resize)(h);
}

struct _mh(t) *
_mh(init)()
{
	struct _mh(t) *h = calloc(1, sizeof(*h));
	h->shadow = calloc(1, sizeof(*h));
	h->prime = 0;
	h->n_buckets = __ac_prime_list[h->prime];
	h->p = calloc(h->n_buckets, sizeof(struct _mh(pair)));
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
	h->p = calloc(h->n_buckets, sizeof(struct _mh(pair)));
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
_mh(resize)(struct _mh(t) *h)
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
		mh_int_t n = put_slot(s, h->p[i].key);
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

mh_int_t
_mh(start_resize)(struct _mh(t) *h, mh_int_t buckets, mh_int_t batch)
{
	if (h->resize_position) {
		/* resize has already been started */
		return h->n_buckets;
	}
	if (buckets < h->n_buckets) {
		/* hash size is already greater than requested */
		return h->n_buckets;
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
	s->p = malloc(s->n_buckets * sizeof(struct _mh(pair)));
	s->b = calloc(s->n_buckets / 16 + 1, sizeof(unsigned));
	_mh(resize)(h);

	return h->n_buckets;
}

void
_mh(reserve)(struct _mh(t) *h, mh_int_t size)
{
	_mh(start_resize)(h, size/MH_DENSITY, h->size);
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
				printf("   -> %i", (int)h->p[i].key);
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
#undef mh_key_t
#undef mh_val_t
#undef mh_name
#undef mh_hash
#undef mh_eq
#undef mh_dirty
#undef mh_free
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
