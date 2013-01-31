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

#include <lib/bitset/index.h>
#include <lib/bitset/expr.h>
#include <lib/bit/bit.h>

#include <string.h>
#include <assert.h>

const size_t INDEX_DEFAULT_CAPACITY = 32;

static int
bitset_index_reserve(struct bitset_index *index, size_t size);

int
bitset_index_create(struct bitset_index *index,
		    void *(*realloc)(void *ptr, size_t size))
{
	assert (index != NULL);
	memset(index, 0, sizeof(*index));
	index->realloc = realloc;
	if (bitset_index_reserve(index, 1) != 0)
		return -1;

	return 0;
}

void
bitset_index_destroy(struct bitset_index *index)
{
	assert (index != NULL);
	assert (index->capacity > 0);

	for (size_t b = 0; b < index->capacity; b++) {
		if (index->bitsets[b] == NULL)
			break;

		bitset_destroy(index->bitsets[b]);
		index->realloc(index->bitsets[b], 0);
		index->bitsets[b] = NULL;
	}
	if (index->capacity > 0) {
		index->realloc(index->bitsets, 0);
	}

	memset(index, 0, sizeof(*index));
}

static int
bitset_index_reserve(struct bitset_index *index, size_t size)
{
	if (size <= index->capacity)
		return 0;

	size_t capacity = (index->capacity > 0)
				? index->capacity
				: INDEX_DEFAULT_CAPACITY;

	while (capacity <= size) {
		capacity *= 2;
	}

	struct bitset **bitsets = index->realloc(index->bitsets,
					capacity * sizeof(*index->bitsets));
	if (bitsets == NULL)
		goto error_1;

	memset(bitsets + index->capacity, 0,
	       (capacity - index->capacity) * sizeof(*index->bitsets));

	/* Save bitset ** but do not update index->capacity */
	index->bitsets = bitsets;

	for (size_t b = index->capacity; b < capacity; b++) {
		index->bitsets[b] = index->realloc(NULL,
					sizeof(*index->bitsets[b]));
		if (index->bitsets[b] == NULL)
			goto error_2;

		bitset_create(index->bitsets[b], index->realloc);
	}

	index->capacity = capacity;

	return 0;

error_2:
	for (size_t b = index->capacity; b < capacity; b++) {
		if (index->bitsets[b] == NULL)
			break;

		bitset_destroy(index->bitsets[b]);
		index->realloc(index->bitsets[b], 0);
		index->bitsets[b] = NULL;
	}
error_1:
	return -1;
}

int
bitset_index_insert(struct bitset_index *index, const void *key,
		    size_t key_size, size_t value)
{
	assert (index != NULL);
	assert (key != NULL);
	assert (index->capacity > 0);

	const size_t size = 1 + key_size * CHAR_BIT;
	if (bitset_index_reserve(index, size) != 0)
		return -1;

	struct bit_iterator bit_it;
	bit_iterator_init(&bit_it, key, key_size, true);
	size_t pos;
	while ( (pos = bit_iterator_next(&bit_it)) != SIZE_MAX) {
		size_t b = pos + 1;
		if (bitset_set(index->bitsets[b], value) < 0)
			goto rollback;
	}

	if (bitset_set(index->bitsets[0], value) < 0)
		goto rollback;

	return 0;

	/* TODO: partial rollback is not work properly here */
rollback:
	/* Rollback changes */
	bit_iterator_init(&bit_it, key, size, true);
	while ( (pos = bit_iterator_next(&bit_it)) != SIZE_MAX) {
		size_t b = pos + 1;
		if (index->bitsets[b] == NULL)
			continue;

		bitset_clear(index->bitsets[b], value);
	}

	bitset_clear(index->bitsets[0], value);

	return -1;
}

void
bitset_index_remove_value(struct bitset_index *index, size_t value)
{
	assert(index != NULL);

	if (index->capacity == 0)
		return;

	for (size_t b = 1; b < index->capacity; b++) {
		if (index->bitsets[b] == NULL)
			continue;

		/* Ignore all errors here */
		bitset_clear(index->bitsets[b], value);
	}
	bitset_clear(index->bitsets[0], value);
}

bool
bitset_index_contains_value(struct bitset_index *index, size_t value)
{
	assert(index != NULL);

	return bitset_test(index->bitsets[0], value);
}

int
bitset_index_expr_all(struct bitset_expr *expr)
{
	(void) index;

	bitset_expr_clear(expr);
	if (bitset_expr_add_conj(expr) != 0)
		return -1;

	if (bitset_expr_add_param(expr, 0, false) != 0)
		return -1;

	return 0;
}

int
bitset_index_expr_equals(struct bitset_expr *expr, const void *key,
			 size_t key_size)
{
	bitset_expr_clear(expr);

	if (bitset_expr_add_conj(expr) != 0)
		return -1;

	for (size_t pos = 0; pos < key_size * CHAR_BIT; pos++) {
		size_t b = pos + 1;
		bool bit_exist = bit_test(key, pos);
		if (bitset_expr_add_param(expr, b, !bit_exist) != 0)
			return -1;
	}

	if (bitset_expr_add_param(expr, 0, false) != 0) {
		return -1;
	}

	return 0;
}

int
bitset_index_expr_all_set(struct bitset_expr *expr, const void *key,
			  size_t key_size)
{
	bitset_expr_clear(expr);

	if (bitset_expr_add_conj(expr) != 0)
		return -1;

	struct bit_iterator bit_it;
	bit_iterator_init(&bit_it, key, key_size, true);
	size_t pos;
	while ( (pos = bit_iterator_next(&bit_it)) != SIZE_MAX ) {
		size_t b = pos + 1;
		if (bitset_expr_add_param(expr, b, false) != 0)
			return -1;
	}

	return 0;
}

int
bitset_index_expr_any_set(struct bitset_expr *expr, const void *key,
			  size_t key_size)
{
	bitset_expr_clear(expr);

	struct bit_iterator bit_it;
	bit_iterator_init(&bit_it, key, key_size, true);
	size_t pos;
	while ( (pos = bit_iterator_next(&bit_it)) != SIZE_MAX) {
		size_t b = pos + 1;
		if (bitset_expr_add_conj(expr) != 0)
			return -1;
		if (bitset_expr_add_param(expr, b, false) != 0)
			return -1;
	}

	return 0;
}

int
bitset_index_expr_all_not_set(struct bitset_expr *expr, const void *key,
			      size_t key_size) {
	bitset_expr_clear(expr);

	if (bitset_expr_add_conj(expr) != 0)
		return -1;

	if (bitset_expr_add_param(expr, 0, false) != 0)
		return -1;

	struct bit_iterator bit_it;
	bit_iterator_init(&bit_it, key, key_size, true);
	size_t pos;
	while ( (pos = bit_iterator_next(&bit_it)) != SIZE_MAX) {
		size_t b = pos + 1;
		if (bitset_expr_add_param(expr, b, true) != 0)
			return -1;
	}

	return 0;
}

int
bitset_index_init_iterator(struct bitset_index *index,
			   struct bitset_iterator *it, struct bitset_expr *expr)
{
	assert (index != NULL);
	assert (it != NULL);

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
	if (bitset_index_reserve(index, max + 1) != 0)
		return -1;

	return bitset_iterator_init(it, expr, index->bitsets, index->capacity);
}

extern inline size_t
bitset_index_size(struct bitset_index *index);
