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

#include "bitset/index.h"
#include "bitset/expr.h"
#include "bit/bit.h"

#include <string.h>
#include <assert.h>

const size_t INDEX_DEFAULT_CAPACITY = 32;

void
tt_bitset_index_create(struct tt_bitset_index *index,
		       void *(*realloc)(void *ptr, size_t size))
{
	assert(index != NULL);
	memset(index, 0, sizeof(*index));
	index->realloc = realloc;
}

void
tt_bitset_index_destroy(struct tt_bitset_index *index)
{
	assert(index != NULL);

	for (size_t b = 0; b < index->capacity; b++) {
		if (index->bitsets[b] == NULL)
			break;

		tt_bitset_destroy(index->bitsets[b]);
		index->realloc(index->bitsets[b], 0);
		index->bitsets[b] = NULL;
	}
	if (index->capacity > 0) {
		index->realloc(index->bitsets, 0);
		index->realloc(index->rollback_buf, 0);
	}

	memset(index, 0, sizeof(*index));
}

static int
tt_bitset_index_reserve(struct tt_bitset_index *index, size_t size)
{
	if (size <= index->capacity)
		return 0;

	size_t capacity = (index->capacity > 0)
				? index->capacity
				: INDEX_DEFAULT_CAPACITY;

	while (capacity <= size) {
		capacity *= 2;
	}

	struct tt_bitset **bitsets = index->realloc(index->bitsets,
					capacity * sizeof(*index->bitsets));
	if (bitsets == NULL)
		goto error_1;

	memset(bitsets + index->capacity, 0,
	       (capacity - index->capacity) * sizeof(*index->bitsets));

	/* Save bitset ** but do not update index->capacity */
	index->bitsets = bitsets;

	/* Resize rollback buffer */
	char *rollback_buf = (char *) index->realloc(index->rollback_buf,
						     capacity);
	if (rollback_buf == NULL)
		goto error_1;

	index->rollback_buf = rollback_buf;

	/* Initialize bitsets */
	for (size_t b = index->capacity; b < capacity; b++) {
		index->bitsets[b] = index->realloc(NULL,
					sizeof(*index->bitsets[b]));
		if (index->bitsets[b] == NULL)
			goto error_2;

		tt_bitset_create(index->bitsets[b], index->realloc);
	}

	index->capacity = capacity;

	return 0;

error_2:
	for (size_t b = index->capacity; b < capacity; b++) {
		if (index->bitsets[b] == NULL)
			break;

		tt_bitset_destroy(index->bitsets[b]);
		index->realloc(index->bitsets[b], 0);
		index->bitsets[b] = NULL;
	}
error_1:
	return -1;
}

int
tt_bitset_index_insert(struct tt_bitset_index *index, const void *key,
		       size_t key_size, size_t value)
{
	assert(index != NULL);
	assert(key != NULL);

	/*
	 * Step 0: allocate enough number of bitsets
	 *
	 * tt_bitset_index_reserve could fail on realloc and return -1.
	 * Do not change anything and return the error to the caller.
	 */
	const size_t size = 1 + key_size * CHAR_BIT;
	if (tt_bitset_index_reserve(index, size) != 0)
		return -1;

	/*
	 * Step 1: set the 'flag' bitset
	 *
	 * tt_bitset_set for 'falg' bitset could fail on realloc.
	 * Do not change anything. Do not shrink buffers allocated on step 1.
	 */
	int rc = tt_bitset_set(index->bitsets[0], value);
	if (rc < 0)
		return -1;

	assert(rc == 0); /* if 1 then the value is already exist in the index */
	if (key_size == 0) /* optimization for empty key */
		return 0;

	index->rollback_buf[0] = (char) rc;

	/*
	 * Step 2: iterate over 'set' bits in the key and update related bitsets.
	 *
	 * A bitset_set somewhere in the middle also could fail on realloc.
	 * If this happens, we stop processing and jump to the rollback code.
	 * Rollback uses index->rollback_buf buffer to restore previous values
	 * of all bitsets on given position. Remember, that tt_bitset_set
	 * returns 1 if a previous value was 'true' and 0 if it was 'false'.
	 * The buffer is indexed by bytes (char *) instead of bits (bit_set)
	 * because it is a little bit faster here.
	 */
	struct bit_iterator bit_it;
	bit_iterator_init(&bit_it, key, key_size, true);
	size_t pos = 0;
	while ((pos = bit_iterator_next(&bit_it)) != SIZE_MAX) {
		size_t b = pos + 1;
		rc = tt_bitset_set(index->bitsets[b], value);
		if (rc < 0)
			goto rollback;

		index->rollback_buf[b] = (char) rc;
	}

	return 0;

rollback:
	/*
	 * Rollback changes done by Step 2.
	 */
	bit_iterator_init(&bit_it, key, size, true);
	size_t rpos;
	while ((rpos = bit_iterator_next(&bit_it)) != SIZE_MAX && rpos < pos) {
		size_t b = rpos + 1;

		if (index->rollback_buf[b] == 1) {
			tt_bitset_set(index->bitsets[b], value);
		} else {
			tt_bitset_clear(index->bitsets[b], value);
		}
	}

	/*
	 * Rollback changes done by Step 1.
	 */
	if (index->rollback_buf[0] == 1) {
		tt_bitset_set(index->bitsets[0], value);
	} else {
		tt_bitset_clear(index->bitsets[0], value);
	}

	return -1;
}

void
tt_bitset_index_remove_value(struct tt_bitset_index *index, size_t value)
{
	assert(index != NULL);

	if (index->capacity == 0)
		return;

	for (size_t b = 1; b < index->capacity; b++) {
		if (index->bitsets[b] == NULL)
			continue;

		/* Ignore all errors here */
		tt_bitset_clear(index->bitsets[b], value);
	}
	tt_bitset_clear(index->bitsets[0], value);
}

bool
tt_bitset_index_contains_value(struct tt_bitset_index *index, size_t value)
{
	assert(index != NULL);

	return tt_bitset_test(index->bitsets[0], value);
}

int
tt_bitset_index_expr_all(struct tt_bitset_expr *expr)
{
	(void) index;

	tt_bitset_expr_clear(expr);
	if (tt_bitset_expr_add_conj(expr) != 0)
		return -1;

	if (tt_bitset_expr_add_param(expr, 0, false) != 0)
		return -1;

	return 0;
}

int
tt_bitset_index_expr_equals(struct tt_bitset_expr *expr, const void *key,
			    size_t key_size)
{
	tt_bitset_expr_clear(expr);

	if (tt_bitset_expr_add_conj(expr) != 0)
		return -1;

	for (size_t pos = 0; pos < key_size * CHAR_BIT; pos++) {
		size_t b = pos + 1;
		bool bit_exist = bit_test(key, pos);
		if (tt_bitset_expr_add_param(expr, b, !bit_exist) != 0)
			return -1;
	}

	if (tt_bitset_expr_add_param(expr, 0, false) != 0) {
		return -1;
	}

	return 0;
}

int
tt_bitset_index_expr_all_set(struct tt_bitset_expr *expr, const void *key,
			     size_t key_size)
{
	tt_bitset_expr_clear(expr);

	if (tt_bitset_expr_add_conj(expr) != 0)
		return -1;

	if (key_size == 0)
		return 0; /* optimization for empty key */

	struct bit_iterator bit_it;
	bit_iterator_init(&bit_it, key, key_size, true);
	size_t pos;
	while ( (pos = bit_iterator_next(&bit_it)) != SIZE_MAX ) {
		size_t b = pos + 1;
		if (tt_bitset_expr_add_param(expr, b, false) != 0)
			return -1;
	}

	return 0;
}

int
tt_bitset_index_expr_any_set(struct tt_bitset_expr *expr, const void *key,
			     size_t key_size)
{
	tt_bitset_expr_clear(expr);

	if (key_size == 0)
		return 0; /* optimization for empty key */

	struct bit_iterator bit_it;
	bit_iterator_init(&bit_it, key, key_size, true);
	size_t pos;
	while ( (pos = bit_iterator_next(&bit_it)) != SIZE_MAX) {
		size_t b = pos + 1;
		if (tt_bitset_expr_add_conj(expr) != 0)
			return -1;
		if (tt_bitset_expr_add_param(expr, b, false) != 0)
			return -1;
	}

	return 0;
}

int
tt_bitset_index_expr_all_not_set(struct tt_bitset_expr *expr, const void *key,
				 size_t key_size) {
	tt_bitset_expr_clear(expr);

	if (tt_bitset_expr_add_conj(expr) != 0)
		return -1;

	if (tt_bitset_expr_add_param(expr, 0, false) != 0)
		return -1;

	if (key_size == 0)
		return 0; /* optimization for empty key */

	struct bit_iterator bit_it;
	bit_iterator_init(&bit_it, key, key_size, true);
	size_t pos;
	while ( (pos = bit_iterator_next(&bit_it)) != SIZE_MAX) {
		size_t b = pos + 1;
		if (tt_bitset_expr_add_param(expr, b, true) != 0)
			return -1;
	}

	return 0;
}

int
tt_bitset_index_init_iterator(struct tt_bitset_index *index,
			      struct tt_bitset_iterator *it,
			      struct tt_bitset_expr *expr)
{
	assert(index != NULL);
	assert(it != NULL);

	/* Check that we have all required bitsets */
	size_t max = 0;
	for (size_t c = 0; c < expr->size; c++) {
		for (size_t b = 0; b < expr->conjs[c].size; b++) {
			if (expr->conjs[c].bitset_ids[b] > max) {
				max = expr->conjs[c].bitset_ids[b];
			}
		}
	}

	/* Resize the index with empty bitsets */
	if (tt_bitset_index_reserve(index, max + 1) != 0)
		return -1;

	return tt_bitset_iterator_init(it, expr, index->bitsets,
				       index->capacity);
}

size_t
tt_bitset_index_bsize(const struct tt_bitset_index *index)
{
	size_t result = 0;
	for (size_t b = 0; b < index->capacity; b++) {
		if (index->bitsets[b] == NULL)
			continue;
		struct tt_bitset_info info;
		tt_bitset_info(index->bitsets[b], &info);
		result += info.page_total_size * info.pages;
	}
	return result;
}

extern inline size_t
tt_bitset_index_size(const struct tt_bitset_index *index);

extern inline size_t
tt_bitset_index_count(const struct tt_bitset_index *index, size_t bit);
