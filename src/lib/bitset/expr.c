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

#include "bitset/expr.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

const size_t EXPR_DEFAULT_CAPACITY = 2;
const size_t EXPR_CONJ_DEFAULT_CAPACITY = 32;

void
tt_bitset_expr_create(struct tt_bitset_expr *expr,
		      void *(*realloc)(void *ptr, size_t size))
{
	memset(expr, 0, sizeof(*expr));
	expr->realloc = realloc;
}

void
tt_bitset_expr_destroy(struct tt_bitset_expr *expr)
{
	for (size_t c = 0; c < expr->size; c++) {
		if (expr->conjs[c].capacity == 0)
			continue;

		expr->realloc(expr->conjs[c].bitset_ids, 0);
		expr->realloc(expr->conjs[c].pre_nots, 0);
	}

	if (expr->capacity > 0) {
		expr->realloc(expr->conjs, 0);
	}

	memset(expr, 0, sizeof(*expr));
}

void
tt_bitset_expr_clear(struct tt_bitset_expr *expr)
{
	for (size_t c = 0; c < expr->size; c++) {
		memset(expr->conjs[c].bitset_ids, 0, expr->conjs[c].size *
		       sizeof(*expr->conjs[c].bitset_ids));
		memset(expr->conjs[c].pre_nots, 0, expr->conjs[c].size *
		       sizeof(*expr->conjs[c].pre_nots));
		expr->conjs[c].size = 0;
	}

	expr->size = 0;
}

static int
tt_bitset_expr_reserve(struct tt_bitset_expr *expr, size_t size)
{
	if (size <= expr->capacity)
		return 0;

	size_t capacity = (expr->capacity > 0)
				? expr->capacity
				: EXPR_DEFAULT_CAPACITY;

	while (capacity <= expr->size) {
		capacity *= 2;
	}

	struct tt_bitset_expr_conj *conjs =
		expr->realloc(expr->conjs, capacity * sizeof(*expr->conjs));

	if (conjs == NULL)
		return -1;

	memset(conjs + expr->capacity, 0, (capacity - expr->capacity) *
	       sizeof(*expr->conjs));
	expr->conjs = conjs;
	expr->capacity = capacity;

	return 0;
}

int
tt_bitset_expr_add_conj(struct tt_bitset_expr *expr)
{
	if (tt_bitset_expr_reserve(expr, expr->size + 1) != 0)
		return -1;

	expr->size++;

	return 0;
}

static int
tt_bitset_expr_conj_reserve(struct tt_bitset_expr *expr,
			    struct tt_bitset_expr_conj *conj, size_t size)
{
	if (size <= conj->capacity)
		return 0;

	size_t capacity = (conj->capacity > 0)
				? conj->capacity
				: EXPR_CONJ_DEFAULT_CAPACITY;

	while (capacity <= conj->size) {
		capacity *= 2;
	}

	size_t *bitset_ids = expr->realloc(conj->bitset_ids,
				capacity * sizeof(*conj->bitset_ids));
	if (bitset_ids == NULL)
		goto error_1;
	bool *pre_nots = expr->realloc(conj->pre_nots,
				 capacity * sizeof(*conj->pre_nots));
	if (pre_nots == NULL)
		goto error_2;

	memset(bitset_ids + conj->capacity, 0,
	       (capacity - conj->capacity) * sizeof(*conj->bitset_ids));
	memset(pre_nots + conj->capacity, 0,
	       (capacity - conj->capacity) * sizeof(*conj->pre_nots));

	conj->bitset_ids = bitset_ids;
	conj->pre_nots = pre_nots;
	conj->capacity = capacity;

	return 0;

error_2:
	expr->realloc(bitset_ids, 0);
error_1:
	return -1;
}

int
tt_bitset_expr_add_param(struct tt_bitset_expr *expr, size_t bitset_id,
			 bool pre_not)
{
	assert(expr->size > 0);
	struct tt_bitset_expr_conj *conj = &expr->conjs[expr->size - 1];

	if (tt_bitset_expr_conj_reserve(expr, conj, conj->size + 1) != 0)
		return -1;

	conj->bitset_ids[conj->size] = bitset_id;
	conj->pre_nots[conj->size] = pre_not;
	conj->size++;

	return 0;
}
