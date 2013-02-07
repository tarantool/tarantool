#ifndef TARANTOOL_LIB_BITSET_INDEX_H_INCLUDED
#define TARANTOOL_LIB_BITSET_INDEX_H_INCLUDED
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

/**
 * @file
 * @brief bitset_index - a bit index based on @link bitset @endlink.
 *
 * @section Purpose
 *
 * bitset_index is an associative container that stores (key, value) pairs
 * in a way that is very optimized for searching values by performing logical
 * expressions on key bits. The organization structure of bitset_index makes it
 * easy to respond to queries like 'give me all pairs where bit i and bit j
 * in pairs keys are set'. The implementation supports evaluation of arbitrary
 * logical expressions represented in the Disjunctive normal form.
 *
 * bitset_index is optimized for querying a large count of values using a single
 * logical expression. The expression can be constructed one time and used for
 * multiple queries. bitset_index is not designed for querying a single value
 * using exact matching by a key.
 *
 * @section Organization
 *
 * bitset_index consists of N+1 @link bitset bitsets @endlink where N is a
 * maximum size of keys in an index (in bits). These bitsets are indexed
 * by the pair's value. A bitset #n+1 corresponds to a bit #n in keys and
 * contains all pairs where this bit is set. That is, if a pair with
 * (key, value) is inserted to the index and its key, say, has 0, 2, 5, 6
 * bits set then bitsets #1, #3, #6, #7 are set at position = pair.value
 * (@link bitset_test bitset_test(bitset, pair.value) @endlink is true) and
 * bitsets #2, #4, #7 , ... are unset at the position.
 *
 * bitset_index also uses a special bitset #0 that is set to true for each
 * position where a pair with value = position exists in an index. This
 * bitset is mostly needed for evaluation expressions with binary NOTs.
 *
 * bitset_index is a little bit different than traditional containers like
 * 'map' or 'set'. Using bitset_index you can certainly have multiple pairs
 * with same key, but all values in an index must be unique. You might think
 * that bitset_index is implemented in an inverted form - a pair's value is
 * used as a positions in internal bitsets and a key is consist of value of
 * this bitsets.
 *
 * @section Performance
 *
 * For certain kind of tasks bitset_index is more efficient by performance and
 * memory utilization than ordinary binary search tree or hashtable.
 *
 * The complexity of the @link bitset_insert @endlink operation is mostly
 * equivalent to inserting one value into \a k balanced binary search trees with
 * height \a m, where \a k is a number of set bits in your key and \ m is
 * a number of pairs in an index divided by some constant (bitset page size).
 *
 * The complexity of an iteration is mostly linear to the number of pairs
 * in where a search expression evals to true. The complexity of an iteration
 * expression does not affect performance directly. Only the number of resulting
 * pairs is important.
 *
 * The real performance heavily depends on the pairs values. If a values
 * space is dense, then an internal bitsets also will be compact and better
 * optimized for iteration.
 *
 * @section Limitations
 *
 * The size of keys is limited only by available memory.
 * bitset_index automatically resizes on 'insert' if new bits are found.
 *
 * Since values are used as a position in bitsets, the actual range of
 * values must be in [0..SIZE_MAX) range.
 *
 * @see bitset.h
 * @see expr.h
 * @see iterator.h
 */

#include <lib/bitset/bitset.h>
#include <lib/bitset/iterator.h>

/**
 * @brief BitsetIndex
 */
struct bitset_index {
	/** @cond false **/
	/* Used bitsets */
	struct bitset **bitsets;
	/* Capacity of bitsets array */
	size_t capacity;
	/* Memory allocator to use */
	void *(*realloc)(void *ptr, size_t size);
	/* A buffer used for rollback changes in bitset_insert */
	char *rollback_buf;
	/** @endcond **/
};

/**
 * @brief Construct \a index
 * @param index bitset index
 * @param realloc memory allocator to use
 * @retval 0 on success
 * @retval -1 on memory error
 */
int
bitset_index_create(struct bitset_index *index,
		    void *(*realloc)(void *ptr, size_t size));

/**
 * @brief Destruct \a index
 * @param index bitset index
 */
void
bitset_index_destroy(struct bitset_index *index);

/**
 * @brief Insert (\a key, \a value) pair into \a index.
 * Only one pair with same value can exist in the index.
 * If pair with same \a value is exist, it will be updated quietly.
 * The method is atomic, i.e. \a index will be in a consistent state after
 * a return even in case of error.
 * @param index object
 * @param key key
 * @param key_size size of the key
 * @param value value
 * @retval 0 on success
 * @retval -1 on memory error
 */
int
bitset_index_insert(struct bitset_index *index, const void *key, size_t key_size,
		    size_t value);

/**
 * @brief Remove a pair with \a value (*, \a value) from \a index.
 * @param index bitset index
 * @param value value
 */
void
bitset_index_remove_value(struct bitset_index *index, size_t value);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'All' algorithm. Matches all pairs in a index.
 *
 * @param expr bitset expression
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
bitset_index_expr_all(struct bitset_expr *expr);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'Equals' algorithm. Matches all pairs where \a key exactly equals to
 * pair.key (\a key == pair.key).
 *
 * @param expr bitset expression
 * @param key key
 * @param key_size of \a key (in char, as sizeof returns)
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
bitset_index_expr_equals(struct bitset_expr *expr, const void *key,
			 size_t key_size);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'All-Bits-Set' algorithm. Matches all pairs where all bits from \a key
 * is set in pair.key ((\a key & pair.key) == \a key).
 *
 * @param expr bitset expression
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
bitset_index_expr_all_set(struct bitset_expr *expr, const void *key,
			  size_t key_size);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'Any-Bits-Set' algorithm. Matches all pairs where at least one bit from
 * \a key is set in pair.key ((\a key & pair.key) != 0).
 *
 * @param expr bitset expression
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
bitset_index_expr_any_set(struct bitset_expr *expr, const void *key,
			  size_t key_size);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'All-Bits-Not-Set' algorithm. Matches all pairs in the \a index, where all
 * bits from the \a key is not set in pair.key ((\a key & pair.key) == 0).
 *
 * @param expr bitset expression
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
bitset_index_expr_all_not_set(struct bitset_expr *expr, const void *key,
			      size_t key_size);

/**
 * @brief Initialize \a it using \a expr and bitsets used in \a index.
 *
 * @param index bitset index
 * @param it bitset iterator
 * @param expr bitset expression
 * @retval 0 on success
 * @retval 1 on memory error
 */
int
bitset_index_init_iterator(struct bitset_index *index,
			   struct bitset_iterator *it,
			   struct bitset_expr *expr);

/**
 * @brief Checks if a (*, \a value) pair exists in \a index
 * @param index bitset index
 * @param value
 * @retval true if \a index contains pair with the \a value
 * @retval false otherwise
 */
bool
bitset_index_contains_value(struct bitset_index *index, size_t value);

/**
 * @brief Return a number of pairs in \a index.
 * @param index bitset index
 * @return number of pairs in \a index
 */
inline size_t
bitset_index_size(struct bitset_index *index)
{
	return bitset_cardinality(index->bitsets[0]);
}

#if defined(DEBUG)
void
bitset_index_dump(struct bitset_index *index, int verbose, FILE *stream);
#endif /* defined(DEBUG) */

#endif /* TARANTOOL_LIB_BITSET_INDEX_H_INCLUDED */
