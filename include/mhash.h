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
e * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
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

#define mh_cat(a, b) mh##a##_##b
#define mh_ecat(a, b) mh_cat(a, b)
#define _mh(x) mh_ecat(mh_name, x)

#define mh_unlikely(x)  __builtin_expect((x),0)

#ifndef __ac_HASH_PRIME_SIZE
#define __ac_HASH_PRIME_SIZE 31
static const uint32_t __ac_prime_list[__ac_HASH_PRIME_SIZE] = {
	3ul,		11ul,		23ul,		53ul,
	97ul,		193ul,		389ul,		769ul,
	1543ul,		3079ul,		6151ul,		12289ul,
	24593ul,	49157ul,	98317ul,	196613ul,
	393241ul,	786433ul,	1572869ul,	3145739ul,
	6291469ul,	12582917ul,	25165843ul,	50331653ul,
	100663319ul,	201326611ul,	402653189ul,	805306457ul,
	1610612741ul,	3221225473ul,	4294967291ul
};
#endif

#ifndef MH_HEADER
#define MH_HEADER

struct _mh(pair) {
	mh_key_t key;
	mh_val_t val;
};

struct _mh(t) {
	struct _mh(pair) * p;
	uint32_t *b;
	uint32_t n_buckets, n_dirty, size, upper_bound;
	uint32_t prime;

	uint32_t resize_cnt;
	uint32_t resizing, batch;
	struct _mh(t) *shadow;
};

#define mh_exist(h, i)		({ h->b[i >> 4] & (1 << (i % 16)); })
#define mh_dirty(h, i)		({ h->b[i >> 4] & (1 << (i % 16 + 16)); })

#define mh_setfree(h, i)	({ h->b[i >> 4] &= ~(1 << (i % 16)); })
#define mh_setexist(h, i)	({ h->b[i >> 4] |= (1 << (i % 16)); })
#define mh_setdirty(h, i)	({ h->b[i >> 4] |= (1 << (i % 16 + 16)); })

#define mh_value(h, i)		({ (h)->p[(i)].val;	})
#define mh_size(h)		({ (h)->size; 		})
#define mh_capacity(h)		({ (h)->n_buckets;	})
#define mh_begin(h)		({ 0;			})
#define mh_end(h)		({ (h)->n_buckets;	})


struct _mh(t) * _mh(init)();
void _mh(clear)(struct _mh(t) *h);
void _mh(destroy)(struct _mh(t) *h);
void _mh(resize)(struct _mh(t) *h);
int _mh(start_resize)(struct _mh(t) *h, uint32_t buckets, uint32_t batch);
void __attribute__((noinline)) _mh(put_resize)(struct _mh(t) *h, mh_key_t key, mh_val_t val);
void __attribute__((noinline)) _mh(del_resize)(struct _mh(t) *h, uint32_t x);
void _mh(dump)(struct _mh(t) *h);

#define get_slot(h, key) _mh(get_slot)(h, key)
#define put_slot(h, key) _mh(put_slot)(h, key)

static inline uint32_t
_mh(get_slot)(struct _mh(t) *h, mh_key_t key)
{
	uint32_t inc, k, i;
	k = mh_hash(key);
	i = k % h->n_buckets;
	inc = 1 + k % (h->n_buckets - 1);
	for (;;) {
		if ((mh_exist(h, i) && mh_eq(h->p[i].key, key)))
			return i;

		if (!mh_dirty(h, i))
			return h->n_buckets;

		i += inc;
		if (i >= h->n_buckets)
			i -= h->n_buckets;
	}
}

#if 0
static inline uint32_t
_mh(put_slot)(struct _mh(t) *h, mh_key_t key)
{
	uint32_t inc, k, i, p = h->n_buckets;
	k = mh_hash(key);
	i = k % h->n_buckets;
	inc = 1 + k % (h->n_buckets - 1);
	for (;;) {
		if (mh_exist(h, i)) {
			if (mh_eq(h->p[i].key, key))
				return i;
			if (p == h->n_buckets)
				mh_setdirty(h, i);
		} else {
			if (p == h->n_buckets)
				p = i;
			if (!mh_dirty(h, i))
				return p;
		}

		i += inc;
		if (i >= h->n_buckets)
			i -= h->n_buckets;
	}
}

/* Faster variant of above loop */
static inline uint32_t
_mh(put_slot)(struct _mh(t) *h, mh_key_t key)
{
	uint32_t inc, k, i, p = h->n_buckets;
	void *loop = &&marking_loop;
	k = mh_hash(key);
	i = k % h->n_buckets;
	inc = 1 + k % (h->n_buckets - 1);
marking_loop:
	if (mh_exist(h, i)) {
		if (mh_eq(h->p[i].key, key))
			return i;

		mh_setdirty(h, i);
		goto next_slot;
	} else {
		p = i;
		loop = &&nonmarking_loop;
		goto continue_nonmarking;
	}

nonmarking_loop:
	if (mh_exist(h, i)) {
		if (mh_eq(h->p[i].key, key))
			return i;
	} else {
	continue_nonmarking:
		if (!mh_dirty(h, i))
			return p;
	}

next_slot:
	i += inc;
	if (i >= h->n_buckets)
		i -= h->n_buckets;
	goto *loop;
}
#endif

/* clearer variant of above loop */
static inline uint32_t
_mh(put_slot)(struct _mh(t) *h, mh_key_t key)
{
	uint32_t hashed_key = mh_hash(key);
	uint32_t itr = hashed_key % h->n_buckets;
	uint32_t step = 1 + hashed_key % (h->n_buckets - 1);
	uint32_t found_slot = mh_end(h);
	/* marking loop */
	while (true) {
		if (mh_exist(h, itr)) {
			/* this is slop occupied */
			if (mh_eq(h->p[itr].key, key))
				/* this is same element */
				return itr;
			/* this is another element, mark it as dirty */
			mh_setdirty(h, itr);
		} else {
			/* we found not occupied element */
			found_slot = itr;
			break;
		}
		itr += step;
		if (itr > h->n_buckets)
			itr -= h->n_buckets;
	}

	/* we found not occupied slot, but element with same key
	   may exist in the hash table */
	while (true) {
		if (mh_exist(h, itr)) {
			/* this is slop occupied */
			if (mh_eq(h->p[itr].key, key)) {
				/* move found element closer to begin of sequence */
				/* copy element */
				mh_setexist(h, found_slot);
				h->p[found_slot].key = h->p[itr].key;
				h->p[found_slot].val = h->p[itr].val;
				/* mark old as free */
				mh_setfree(h, itr);
				if (!mh_dirty(h, itr))
					h->n_dirty--;
				/* this is same element */
				return found_slot;
			}
		} else {
			/* all sequence checked, element with same key not
			   found. */
			if (!mh_dirty(h, itr))
				return found_slot;
		}
		itr += step;
		if (itr > h->n_buckets)
			itr -= h->n_buckets;
	}
}

static inline uint32_t
_mh(get)(struct _mh(t) *h, mh_key_t key)
{
	uint32_t i = get_slot(h, key);
	if (!mh_exist(h, i))
		return i = h->n_buckets;
	return i;
}

static inline uint32_t
_mh(put)(struct _mh(t) *h, mh_key_t key, mh_val_t val, int * ret)
{
	if (h->size == h->n_buckets)
		/* no one free elements */
		return mh_end(h);
#if MH_INCREMENTAL_RESIZE
	if (mh_unlikely(h->n_dirty >= h->upper_bound || h->resizing > 0))
		_mh(put_resize)(h, key, val);
#else
	if (mh_unlikely(h->n_dirty >= h->upper_bound))
		_mh(start_resize)(h, h->n_buckets + 1, -1);
#endif
	uint32_t x = put_slot(h, key);
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
	return x;
}

static inline void
_mh(del)(struct _mh(t) *h, uint32_t x)
{
	if (x != h->n_buckets && mh_exist(h, x)) {
		mh_setfree(h, x);
		h->size--;
		if (!mh_dirty(h, x))
			h->n_dirty--;
#if MH_INCREMENTAL_RESIZE
		if (mh_unlikely(h->resizing))
			_mh(del_resize)(h, x);
#endif
	}
}
#endif


#ifdef MH_SOURCE
void __attribute__((noinline))
_mh(put_resize)(struct _mh(t) *h, mh_key_t key, mh_val_t val)
{
	if (h->resizing > 0)
		_mh(resize)(h);
	else
		_mh(start_resize)(h, h->n_buckets + 1, 0);
	if (h->resizing)
		_mh(put)(h->shadow, key, val, NULL);
}


void __attribute__((noinline))
_mh(del_resize)(struct _mh(t) *h, uint32_t x)
{
	struct _mh(t) *s = h->shadow;
	uint32_t y = get_slot(s, h->p[x].key);
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
	h->upper_bound = h->n_buckets * 0.7;
	return h;
}

void
_mh(clear)(struct _mh(t) *h)
{
	free(h->p);
	h->prime = 0;
	h->n_buckets = __ac_prime_list[h->prime];
	h->p = calloc(h->n_buckets, sizeof(struct _mh(pair)));
	h->upper_bound = h->n_buckets * 0.7;
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
	uint32_t batch = h->batch;
#endif
	for (uint32_t o = h->resizing; o < h->n_buckets; o++) {
#if MH_INCREMENTAL_RESIZE
		if (batch-- == 0) {
			h->resizing = o;
			return;
		}
#endif
		if (!mh_exist(h, o))
			continue;
		uint32_t n = put_slot(s, h->p[o].key);
		s->p[n] = h->p[o];
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
_mh(start_resize)(struct _mh(t) *h, uint32_t buckets, uint32_t batch)
{
	if (h->resizing)
		/* resize is already started */
		return h->n_buckets;

	if (buckets < h->n_buckets)
		/* hash size already greater than requested */
		return h->n_buckets;

	while (h->prime < __ac_HASH_PRIME_SIZE) {
		if (__ac_prime_list[h->prime] >= buckets)
			break;
		h->prime += 1;
	}

	h->batch = batch > 0 ? batch : h->n_buckets / (256 * 1024);
	if (h->batch < 256)
		/* minimum batch must be greater or equal than
		   1 / (1 - f), where f is upper bound percent = 0.7 */
		h->batch = 256;

	struct _mh(t) *s = h->shadow;
	memcpy(s, h, sizeof(*h));
	s->resizing = 0;
	s->n_buckets = __ac_prime_list[h->prime];
	s->upper_bound = s->n_buckets * 0.7;
	s->n_dirty = 0;
	s->p = malloc(s->n_buckets * sizeof(struct _mh(pair)));
	s->b = calloc(s->n_buckets / 16 + 1, sizeof(unsigned));
	_mh(resize)(h);

	return h->n_buckets;
}

#ifndef mh_stat
#define mh_stat(buf, h) ({						\
                tbuf_printf(buf, "  n_buckets: %"PRIu32 CRLF		\
			    "  n_dirty: %"PRIu32 CRLF			\
			    "  size: %"PRIu32 CRLF			\
			    "  resize_cnt: %"PRIu32 CRLF		\
			    "  resizing: %"PRIu32 CRLF,			\
			    h->n_buckets,				\
			    h->n_dirty,					\
			    h->size,					\
			    h->resize_cnt,				\
			    h->resizing);				\
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
#endif

#undef mh_cat
#undef mh_ecat
#undef _mh
