#ifndef TARANTOOL_LIB_BITSET_ITERATOR_H_INCLUDED
#define TARANTOOL_LIB_BITSET_ITERATOR_H_INCLUDED

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
 * @brief Iterator for @link bitset @endlink objects with
 * expression support.
 *
 * @link bitset_iterator @endlink is used to iterate over a result
 * of the evaluation a @link bitset_expr logical expression
 * @endlink on a set of bitsets. The iterator evaluates its
 * expression on the fly, without producing temporary bitsets.
 * Each iteration (@link bitset_iterator_next @endlink) returns
 * the next position where a given expression evaluates to true on
 * a given set of bitsets.
 *
 * @see expr.h
 */

#include "bitset/bitset.h"
#include "bitset/expr.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** @cond false **/
struct tt_bitset_iterator_conj;
/** @endcond **/

/**
 * @brief Bitset Iterator
 */
struct tt_bitset_iterator {
	/** @cond false **/
	size_t size;
	size_t capacity;
	struct tt_bitset_iterator_conj *conjs;
	struct tt_bitset_page *page;
	struct tt_bitset_page *page_tmp;
	void *(*realloc)(void *ptr, size_t size);
	struct bit_iterator page_it;
	/** @endcond **/
};

/**
 * @brief Construct \a it.
 *
 * The created iterator must be initialized by
 * @link bitset_iterator_init @endlink method before first usage.
 * @param it bitset iterator
 * @param realloc memory allocator to use
 */
void
tt_bitset_iterator_create(struct tt_bitset_iterator *it,
			  void *(*realloc)(void *ptr, size_t size));

/**
 * @brief Destruct \a it.
 * @param it bitset iterator
 */
void
tt_bitset_iterator_destroy(struct tt_bitset_iterator *it);

/**
 * @brief Initialize the \a it using \a expr and \a bitsets and rewind the
 * iterator to the start position.
 *
 * @note It is safe to reinitialize an iterator with a new expression and new
 * bitsets. All internal buffers are safely reused in this case with minimal
 * number of new allocations.
 *
 * @note @a expr object is only used during initialization time and can be
 * safetly reused or destroyed just after this call.
 *
 * @param it bitset iterator
 * @param expr bitset expression
 * @param bitsets array of pointers to bitsets that should be used to bind
 * the expression parameters.
 * @param size of @a bitsets array
 * @retval 0 on success
 * @retval -1 on memory error
 * @see expr.h
 */
int
tt_bitset_iterator_init(struct tt_bitset_iterator *it,
			struct tt_bitset_expr *expr, struct tt_bitset **bitsets,
			size_t bitsets_size);

/**
 * @brief Rewind the \a it to the start position.
 * @param it bitset iterator
 * @see @link bitset_iterator_init @endlink
 */
void
tt_bitset_iterator_rewind(struct tt_bitset_iterator *it);

/**
 * @brief Move \a it to a next position
 * @param it bitset iterator
 * @return the next offset where the expression evaluates to true
 * or SIZE_MAX if there is no more bits in the result set.
 * @see @link bitset_iterator_init @endlink
 */
size_t
tt_bitset_iterator_next(struct tt_bitset_iterator *it);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_BITSET_ITERATOR_H_INCLUDED */
