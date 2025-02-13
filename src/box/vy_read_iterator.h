#ifndef INCLUDES_TARANTOOL_BOX_VY_READ_ITERATOR_H
#define INCLUDES_TARANTOOL_BOX_VY_READ_ITERATOR_H
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdbool.h>

#include "iterator_type.h"
#include "trivia/util.h"
#include "vy_entry.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Vinyl read iterator.
 *
 * Used for executing a SELECT request over an LSM tree.
 */
struct vy_read_iterator {
	/** LSM tree to iterate over. */
	struct vy_lsm *lsm;
	/** Active transaction or NULL. */
	struct vy_tx *tx;
	/** Iterator type. */
	enum iterator_type iterator_type;
	/** Search key. */
	struct vy_entry key;
	/** Read view the iterator lives in. */
	const struct vy_read_view **read_view;
	/**
	 * Set if the resulting statement needs to be
	 * checked to match the search key.
	 */
	bool need_check_eq;
	/**
	 * Set if (a) the iterator hasn't returned anything yet and (b) there
	 * may be at most one tuple equal to the search key and the iterator
	 * will return it if it exists (i.e. it's GE/LE/EQ/REQ). This flag
	 * enables the optimization that skips deeper read sources if a tuple
	 * exactly matching the search key is found.
	 */
	bool check_exact_match;
	/** Set to true on the first iteration. */
	bool is_started;
	/**
	 * Set to true if the statement that is about to be added to
	 * the cache is the first one matching the iteration criteria.
	 * See vy_read_iterator_cache_add().
	 */
	bool is_first_cached;
	/**
	 * LSN of the cache chain link following the last cached statement.
	 * Used by vy_read_iterator_cache_add().
	 */
	int64_t cache_link_lsn;
	/** Last statement returned by vy_read_iterator_next(). */
	struct vy_entry last;
	/**
	 * Last statement added to the tuple cache by
	 * vy_read_iterator_cache_add().
	 */
	struct vy_entry last_cached;
	/**
	 * Copy of lsm->range_tree_version.
	 * Used for detecting range tree changes.
	 */
	uint32_t range_tree_version;
	/**
	 * Copy of lsm->mem_list_version.
	 * Used for detecting memory level changes.
	 */
	uint32_t mem_list_version;
	/**
	 * Copy of curr_range->version.
	 * Used for detecting changes in the current range.
	 */
	uint32_t range_version;
	/** Range the iterator is currently positioned at. */
	struct vy_range *curr_range;
	/**
	 * Array of merge sources. Sources are sorted by age.
	 * In particular, this means that all mutable sources
	 * come first while all sources that may yield (runs)
	 * go last.
	 */
	struct vy_read_src *src;
	/** Number of elements in the src array. */
	uint32_t src_count;
	/** Maximal capacity of the src array. */
	uint32_t src_capacity;
	/** Offset of the transaction write set source. */
	uint32_t txw_src;
	/** Offset of the cache source. */
	uint32_t cache_src;
	/** Offset of the first memory source. */
	uint32_t mem_src;
	/** Offset of the first disk source. */
	uint32_t disk_src;
	/** Offset of the first skipped source. */
	uint32_t skipped_src;
	/**
	 * vy_read_src::front_id <= front_id for any source.
	 * vy_read_src::front_id == front_id iff the source
	 * iterator is positioned at the next key.
	 */
	uint32_t front_id;
	/**
	 * front_id from the previous iteration.
	 */
	uint32_t prev_front_id;
};

/**
 * Open the read iterator.
 * @param itr           Read iterator.
 * @param lsm           LSM tree to iterate.
 * @param tx            Current transaction, if exists.
 * @param iterator_type Type of the iterator that determines order
 *                      of the iteration.
 * @param key           Key for the iteration.
 * @param last          Key to start the iteration after. Note, the iterator
 *                      takes ownership of the entry: it will unref the entry
 *                      when it's no longer needed.
 * @param rv            Read view.
 */
void
vy_read_iterator_open_after(struct vy_read_iterator *itr, struct vy_lsm *lsm,
			    struct vy_tx *tx, enum iterator_type iterator_type,
			    struct vy_entry key, struct vy_entry last,
			    const struct vy_read_view **rv);

static inline void
vy_read_iterator_open(struct vy_read_iterator *itr, struct vy_lsm *lsm,
		      struct vy_tx *tx, enum iterator_type iterator_type,
		      struct vy_entry key, const struct vy_read_view **rv)
{
	vy_read_iterator_open_after(itr, lsm, tx, iterator_type, key,
				    vy_entry_none(), rv);
}

/**
 * Get the next statement with another key, or start the iterator,
 * if it wasn't started.
 * @param itr         Read iterator.
 * @param[out] result Found statement is stored here.
 *
 * @retval  0 Success.
 * @retval -1 Read error.
 */
NODISCARD int
vy_read_iterator_next(struct vy_read_iterator *itr, struct vy_entry *result);

/**
 * Add the last tuple returned by the read iterator to the cache.
 * @param itr   Read iterator
 * @param entry Last tuple returned by the iterator.
 * @param skipped_lsn Max LSN among all full statements skipped because
 *                    they didn't match the partial tuple key.
 *
 * We use a separate function for populating the cache rather than
 * doing that right in vy_read_iterator_next() so that we can store
 * full tuples in a secondary index cache, thus saving some memory.
 *
 * Usage pattern:
 * - Call vy_read_iterator_next() to get a partial tuple.
 * - Call vy_point_lookup() to get the full tuple corresponding
 *   to the partial tuple returned by the iterator.
 * - Call vy_read_iterator_cache_add() on the full tuple to add
 *   the result to the cache.
 *
 * The skipped_lsn is used for the cache chain link. Basically, it's the max
 * LSN over all deferred DELETE statements that fall between the previous and
 * the current cache nodes.
 */
void
vy_read_iterator_cache_add(struct vy_read_iterator *itr, struct vy_entry entry,
			   int64_t skipped_lsn);

/**
 * Close the iterator and free resources.
 */
void
vy_read_iterator_close(struct vy_read_iterator *itr);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_READ_ITERATOR_H */
