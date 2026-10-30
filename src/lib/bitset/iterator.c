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

#include "bitset/iterator.h"
#include "bitset/expr.h"
#include "page.h"

#include <assert.h>

const size_t ITERATOR_DEFAULT_CAPACITY = 2;
const size_t ITERATOR_CONJ_DEFAULT_CAPACITY = 32;

struct tt_bitset_iterator_conj {
	size_t page_first_pos;
	size_t size;
	size_t capacity;
	struct tt_bitset **bitsets;
	bool *pre_nots;
	struct tt_bitset_page **pages;
};

/**
 * @brief Construct iterator
 * @param it iterator
 * @param realloc memory allocator to use
 */
void
tt_bitset_iterator_create(struct tt_bitset_iterator *it,
			  void *(*realloc)(void *ptr, size_t size))
{
	memset(it, 0, sizeof(*it));
	it->realloc = realloc;
}

/**
 * @brief Destroys the @a it object
 * @param it object
 * @see bitset_iterator_new
 */
void
tt_bitset_iterator_destroy(struct tt_bitset_iterator *it)
{
	for (size_t c = 0; c < it->size; c++) {
		if (it->conjs[c].capacity == 0)
			continue;

		it->realloc(it->conjs[c].bitsets, 0);
		it->realloc(it->conjs[c].pre_nots, 0);
		it->realloc(it->conjs[c].pages, 0);
	}

	if (it->capacity > 0) {
		it->realloc(it->conjs, 0);
	}

	if (it->page != NULL) {
		tt_bitset_page_destroy(it->page);
		it->realloc(it->page, 0);
	}

	if (it->page_tmp != NULL) {
		tt_bitset_page_destroy(it->page_tmp);
		it->realloc(it->page_tmp, 0);
	}

	memset(it, 0, sizeof(*it));
}


static int
tt_bitset_iterator_reserve(struct tt_bitset_iterator *it, size_t size)
{
	if (size <= it->capacity)
		return 0;

	size_t capacity = (it->capacity > 0)
				? it->capacity
				: ITERATOR_DEFAULT_CAPACITY;

	while (capacity <= size) {
		capacity *= 2;
	}

	struct tt_bitset_iterator_conj *conjs =
			it->realloc(it->conjs, capacity * sizeof(*it->conjs));
	if (conjs == NULL)
		return -1;

	memset(conjs + it->capacity, 0,
	       (capacity - it->capacity) * sizeof(*it->conjs));

	it->conjs = conjs;
	it->capacity = capacity;

	return 0;
}

static int
tt_bitset_iterator_conj_reserve(struct tt_bitset_iterator *it,
				struct tt_bitset_iterator_conj *conj,
				size_t size)
{
	if (size <= conj->capacity)
		return 0;

	size_t capacity = (conj->capacity > 0)
				? conj->capacity
				: ITERATOR_CONJ_DEFAULT_CAPACITY;

	while (capacity <= size) {
		capacity *= 2;
	}

	struct tt_bitset **bitsets = it->realloc(conj->bitsets,
					capacity * sizeof(*conj->bitsets));
	if (bitsets == NULL)
		goto error_1;
	bool *pre_nots = it->realloc(conj->pre_nots,
					capacity * sizeof(*conj->pre_nots));
	if (pre_nots == NULL)
		goto error_2;
	struct tt_bitset_page **pages = it->realloc(conj->pages,
					capacity * sizeof(*conj->pages));
	if (pages == NULL)
		goto error_3;

	memset(bitsets + conj->capacity, 0,
	       (capacity - conj->capacity) * sizeof(*conj->bitsets));
	memset(pre_nots + conj->capacity, 0,
	       (capacity - conj->capacity) * sizeof(*conj->pre_nots));
	memset(pages + conj->capacity, 0,
	       (capacity - conj->capacity) * sizeof(*conj->pages));

	conj->bitsets = bitsets;
	conj->pre_nots = pre_nots;
	conj->pages = pages;
	conj->capacity = capacity;

	return 0;

error_3:
	it->realloc(pre_nots, 0);
error_2:
	it->realloc(bitsets, 0);
error_1:
	return -1;
}

int
tt_bitset_iterator_init(struct tt_bitset_iterator *it,
			struct tt_bitset_expr *expr,
			struct tt_bitset **p_bitsets, size_t bitsets_size)
{
	assert(it != NULL);
	assert(expr != NULL);
	if (bitsets_size > 0) {
		assert(p_bitsets != NULL);
	}

	size_t page_alloc_size = tt_bitset_page_alloc_size(it->realloc);
	if (it->page != NULL) {
		tt_bitset_page_destroy(it->page);
	} else {
		it->page = it->realloc(NULL, page_alloc_size);
	}

	tt_bitset_page_create(it->page);

	if (it->page_tmp != NULL) {
		tt_bitset_page_destroy(it->page_tmp);
	} else {
		it->page_tmp = it->realloc(NULL, page_alloc_size);
		if (it->page_tmp == NULL)
			return -1;
	}

	tt_bitset_page_create(it->page_tmp);

	if (tt_bitset_iterator_reserve(it, expr->size) != 0)
		return -1;

	for (size_t c = 0; c < expr->size; c++) {
		struct tt_bitset_expr_conj *exconj = &expr->conjs[c];
		struct tt_bitset_iterator_conj *itconj = &it->conjs[c];
		itconj->page_first_pos = 0;

		if (tt_bitset_iterator_conj_reserve(it, itconj,
						    exconj->size) != 0)
			return -1;

		for (size_t b = 0; b < exconj->size; b++) {
			assert(exconj->bitset_ids[b] < bitsets_size);
			assert(p_bitsets[exconj->bitset_ids[b]] != NULL);
			itconj->bitsets[b] = p_bitsets[exconj->bitset_ids[b]];
			itconj->pre_nots[b] = exconj->pre_nots[b];
			itconj->pages[b] = NULL;
		}

		itconj->size = exconj->size;
	}

	it->size = expr->size;

	tt_bitset_iterator_rewind(it);

	return 0;
}

static void
tt_bitset_iterator_conj_rewind(struct tt_bitset_iterator_conj *conj,
			       size_t pos)
{
	assert(conj != NULL);
	assert(pos % (BITSET_PAGE_DATA_SIZE * CHAR_BIT) == 0);
	assert(conj->page_first_pos <= pos);

	if (conj->size == 0) {
		conj->page_first_pos = SIZE_MAX;
		return;
	}

	struct tt_bitset_page key;
	key.first_pos = pos;

	restart:
	for (size_t b = 0; b < conj->size; b++) {
		conj->pages[b] =
			tt_bitset_pages_nsearch(&conj->bitsets[b]->pages, &key);
#if 0
		if (conj->pages[b] != NULL) {
			fprintf(stderr, "rewind [%zu] => %zu (%p)\n", b,
				conj->pages[b]->first_pos, conj->pages[b]);
		} else {
			fprintf(stderr, "rewind [%zu] => NULL\n", b);
		}
#endif
		if (conj->pre_nots[b])
			continue;

		/* bitset b does not have more pages */
		if (conj->pages[b] == NULL) {
			conj->page_first_pos = SIZE_MAX;
			return;
		}

		assert(conj->pages[b]->first_pos >= key.first_pos);

		/* bitset b have a next page, but it is beyond pos scope */
		if (conj->pages[b]->first_pos > key.first_pos) {
			key.first_pos = conj->pages[b]->first_pos;
			goto restart;
		}
	}

	conj->page_first_pos = key.first_pos;
}

static int
tt_bitset_iterator_conj_cmp(const void *p1, const void *p2)
{
	assert(p1 != NULL && p2 != NULL);

	struct tt_bitset_iterator_conj *conj1 =
		(struct tt_bitset_iterator_conj *) p1;
	struct tt_bitset_iterator_conj *conj2 =
		(struct tt_bitset_iterator_conj *) p2;

	if (conj1->page_first_pos < conj2->page_first_pos) {
		return -1;
	} else if (conj1->page_first_pos > conj2->page_first_pos) {
		return 1;
	} else {
		return 0;
	}
}

static void
tt_bitset_iterator_conj_prepare_page(struct tt_bitset_iterator_conj *conj,
				     struct tt_bitset_page *dst)
{
	assert(conj != NULL);
	assert(dst != NULL);
	assert(conj->size > 0);
	assert(conj->page_first_pos != SIZE_MAX);

	tt_bitset_page_set_ones(dst);
	for (size_t b = 0; b < conj->size; b++) {
		if (!conj->pre_nots[b]) {
			/* conj->pages[b] is rewinded to conj->page_first_pos */
			assert(conj->pages[b]->first_pos == conj->page_first_pos);
			tt_bitset_page_and(dst, conj->pages[b]);
		} else {
			/*
			 * If page is NULL or its position is not equal
			 * to conj->page_first_pos then conj->bitset[b]
			 * does not have page with the required position and
			 * all bits in this page are considered to be zeros.
			 * Since NAND(a, zeros) => a, we can simple skip this
			 * bitset here.
			 */
			if (conj->pages[b] == NULL ||
			    conj->pages[b]->first_pos != conj->page_first_pos)
				continue;

			tt_bitset_page_nand(dst, conj->pages[b]);
		}
	}
}

static void
tt_bitset_iterator_prepare_page(struct tt_bitset_iterator *it)
{
	qsort(it->conjs, it->size, sizeof(*it->conjs),
	      tt_bitset_iterator_conj_cmp);

	tt_bitset_page_set_zeros(it->page);
	if (it->size > 0) {
		it->page->first_pos = it->conjs[0].page_first_pos;
	} else {
		it->page->first_pos = SIZE_MAX;
	}

	/* There is no more conjunctions that can be ORed */
	if (it->page->first_pos == SIZE_MAX)
		return;

	/* For each conj where conj->page_first_pos == pos */
	for (size_t c = 0; c < it->size; c++) {
		if (it->conjs[c].page_first_pos > it->page->first_pos)
			break;

		/* Get result from conj */
		tt_bitset_iterator_conj_prepare_page(&it->conjs[c],
						     it->page_tmp);
		/* OR page from conjunction with it->page */
		tt_bitset_page_or(it->page, it->page_tmp);
	}

	/* Init the bit iterator on it->page */
	bit_iterator_init(&it->page_it, tt_bitset_page_data(it->page),
		      BITSET_PAGE_DATA_SIZE, true);
}

static void
tt_bitset_iterator_first_page(struct tt_bitset_iterator *it)
{
	assert(it != NULL);

	/* Rewind all conjunctions to first positions */
	for (size_t c = 0; c < it->size; c++) {
		tt_bitset_iterator_conj_rewind(&it->conjs[c], 0);
	}

	/* Prepare the result page */
	tt_bitset_iterator_prepare_page(it);
}

static void
tt_bitset_iterator_next_page(struct tt_bitset_iterator *it)
{
	assert(it != NULL);

	size_t PAGE_BIT = BITSET_PAGE_DATA_SIZE * CHAR_BIT;
	size_t pos = it->page->first_pos;

	/* Rewind all conjunctions that at the current position to the
	 * next position */
	for (size_t c = 0; c < it->size; c++) {
		if (it->conjs[c].page_first_pos > pos)
			break;

		tt_bitset_iterator_conj_rewind(&it->conjs[c], pos + PAGE_BIT);
		assert(pos + PAGE_BIT <= it->conjs[c].page_first_pos);
	}

	/* Prepare the result page */
	tt_bitset_iterator_prepare_page(it);
}


void
tt_bitset_iterator_rewind(struct tt_bitset_iterator *it)
{
	assert(it != NULL);

	/* Prepare first page */
	tt_bitset_iterator_first_page(it);
}

size_t
tt_bitset_iterator_next(struct tt_bitset_iterator *it)
{
	assert(it != NULL);

	while (true) {
		if (it->page->first_pos == SIZE_MAX)
			return SIZE_MAX;

		size_t pos = bit_iterator_next(&it->page_it);
		if (pos != SIZE_MAX) {
			return it->page->first_pos + pos;
		}

		tt_bitset_iterator_next_page(it);
	}
}
