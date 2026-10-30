#ifndef TARANTOOL_LIB_BITSET_EXPR_H_INCLUDED
#define TARANTOOL_LIB_BITSET_EXPR_H_INCLUDED
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

/**
 * @file
 * @brief Expressions on bitsets.
 *
 * This library provides full support for evaluation of logical expressions
 * on @link bitset bitsets @endlink. One can prepare an arbitrary logical
 * expression in Disjunctive normal form (DNF) using @link bitset_expr @endlink
 * methods and then evaluate the expression on the set of @link bitset @endlink
 * objects. Currently only @link bitset_iterator @endlink supports expressions.
 * It can be used for performing iteration over the expression result on the fly,
 * without producing temporary bitsets.
 *
 * @link bitset_expr @endlink holds any expression that can be represented
 * in DNF form. Since every propositional formula can be represented using DNF,
 * one can construct any such logical expression using methods from this module.
 *
 * A DNF example: (~b0 & b1 & ~b2) | (b2 & ~b3 & b4) | (b3 & b6)
 *		  where b[0-9] is an arbitrary bitset.
 *
 * @link bitset_expr @endlink does not operate directly on @link bitset @endlink
 * objects. Instead of this, one should use placeholders (identifiers)
 * which will be bound to the actual bitsets by the selected evaluator
 * (e.g. bitset_iterator).
 *
 * @link http://en.wikipedia.org/wiki/Disjunctive_normal_form @endlink
 * @note Reduce operations in both cases are left-associate.
 *
 * @see bitset_iterator_init
 */

#include "bitset.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** @cond false **/
struct tt_bitset_expr_conj {
	size_t size;
	size_t capacity;
	size_t *bitset_ids;
	bool *pre_nots;
};
/** @endcond **/

/**
 * @brief Bitset Expression
 */
struct tt_bitset_expr {
	/** @cond false **/
	/** Size of \a conjs array **/
	size_t size;
	/** Capacity of \a conjs array **/
	size_t capacity;
	/** Array of conjunctions **/
	struct tt_bitset_expr_conj *conjs;
	/** Memory allocator **/
	void *(*realloc)(void *ptr, size_t size);
	/** @endcond **/
};

/**
 * @brief Construct bitset expression \a expr
 * @param expr bitset expression
 * @param realloc memory allocator to use
 */
void
tt_bitset_expr_create(struct tt_bitset_expr *expr,
		      void *(*realloc)(void *ptr, size_t size));

/**
 * @brief Destruct bitset expression \a expr
 * @param expr bitset expression
 */
void
tt_bitset_expr_destroy(struct tt_bitset_expr *expr);

/**
 * @brief Clear @a expr (remove all conjunctions from it)
 * @param expr bitset expression
 * @note Allocated memory is not freed. One can continue using the object
 * after this operation. Use @link bitset_expr_destroy @endlink to destroy
 * the object completely.
 */
void
tt_bitset_expr_clear(struct tt_bitset_expr *expr);

/**
 * @brief Add a new conjunction to \a expr.
 * @param expr bitset expression
 * @retval 0  on success
 * @retval -1 on memory error
 */
int
tt_bitset_expr_add_conj(struct tt_bitset_expr *expr);

/**
 * @brief Add a new placeholder for a bitset to the current conjunction.
 * @param expr bitset expression
 * @param bitset_id identifier of a bitset (placeholder)
 * @param pre_not if set to true, then logical NOT will be performed on
 * the bitset during evaluation process.
 * @retval 0  on success
 * @retval -1 on memory error
 */
int
tt_bitset_expr_add_param(struct tt_bitset_expr *expr, size_t bitset_id,
			 bool pre_not);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_BITSET_EXPR_H_INCLUDED */
