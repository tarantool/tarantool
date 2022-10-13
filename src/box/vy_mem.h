#ifndef INCLUDES_TARANTOOL_BOX_VY_MEM_H
#define INCLUDES_TARANTOOL_BOX_VY_MEM_H
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

#include <small/rlist.h>
#include <small/lsregion.h>
#include <small/slab_arena.h>
#include <small/quota.h>

#include "fiber_cond.h"
#include "iterator_type.h"
#include "vy_stmt.h" /* for comparators */
#include "vy_stmt_stream.h"
#include "vy_read_view.h"
#include "vy_stat.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_history;

/** Vinyl memory environment. */
struct vy_mem_env {
	struct lsregion allocator;
	struct slab_arena arena;
	struct quota quota;
	/** Size of memory used for storing tree extents. */
	size_t tree_extent_size;
};

/**
 * Initialize a vinyl memory environment.
 * @param env[out] The environment to initialize.
 * @param memory The maximum number of in-memory bytes that vinyl uses.
 */
void
vy_mem_env_create(struct vy_mem_env *env, size_t memory);

/**
 * Destroy a vinyl memory environment.
 * @param env The environment to destroy.
 */
void
vy_mem_env_destroy(struct vy_mem_env *env);

/** @cond false */

struct vy_mem_tree_key {
	struct vy_entry entry;
	int64_t lsn;
};

/**
 * Internal. Extracted to speed up BPS tree.
 */
static int
vy_mem_tree_cmp(struct vy_entry a, struct vy_entry b,
		struct key_def *cmp_def)
{
	int res = vy_entry_compare(a, b, cmp_def);
	if (res)
		return res;
	int64_t a_lsn = vy_stmt_lsn(a.stmt), b_lsn = vy_stmt_lsn(b.stmt);
	return a_lsn > b_lsn ? -1 : a_lsn < b_lsn;
}

/**
 * Internal. Extracted to speed up BPS tree.
 */
static int
vy_mem_tree_cmp_key(struct vy_entry entry, struct vy_mem_tree_key *key,
		    struct key_def *cmp_def)
{
	int res = vy_entry_compare(entry, key->entry, cmp_def);
	if (res == 0) {
		if (key->lsn == INT64_MAX - 1)
			return 0;
		int64_t a_lsn = vy_stmt_lsn(entry.stmt);
		res = a_lsn > key->lsn ? -1 : a_lsn < key->lsn;
	}
	return res;
}

#define VY_MEM_TREE_EXTENT_SIZE (16 * 1024)

#define BPS_TREE_NAME vy_mem_tree
#define BPS_TREE_BLOCK_SIZE 512
#define BPS_TREE_EXTENT_SIZE VY_MEM_TREE_EXTENT_SIZE
#define BPS_TREE_COMPARE(a, b, cmp_def) vy_mem_tree_cmp(a, b, cmp_def)
#define BPS_TREE_COMPARE_KEY(a, b, cmp_def) vy_mem_tree_cmp_key(a, b, cmp_def)
#define BPS_TREE_IS_IDENTICAL(a, b) vy_entry_is_equal(a, b)
#define bps_tree_elem_t struct vy_entry
#define bps_tree_key_t struct vy_mem_tree_key *
#define bps_tree_arg_t struct key_def *

#include <salad/bps_tree.h>

#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef bps_tree_arg_t
#undef BPS_TREE_IS_IDENTICAL

/** @endcond false */

/**
 * vy_mem is an in-memory container for tuples in a single vinyl
 * range.
 * Internally it uses bps_tree to store tuples, which are ordered
 * by statement key and, for the same key, by lsn, in descending
 * order.
 *
 * For example, assume there are two statements with the same key,
 * but different LSN. These are duplicates of the same key,
 * maintained for the purpose of MVCC/consistent read view.
 * In Vinyl terms, they form a duplicate chain.
 *
 * vy_mem distinguishes between the first duplicate in the chain
 * and other keys in that chain.
 */
struct vy_mem {
	/** Vinyl memory environment. */
	struct vy_mem_env *env;
	/** Link in range->sealed list. */
	struct rlist in_sealed;
	/** BPS tree */
	struct vy_mem_tree tree;
	/** Size of memory used for storing tree extents. */
	size_t tree_extent_size;
	/** Number of statements. */
	struct vy_stmt_counter count;
	/**
	 * Max LSN covered by this in-memory tree.
	 *
	 * Once the tree is dumped to disk it will be used to update
	 * vy_lsm::dump_lsn, see vy_task_dump_new().
	 *
	 * Note, we account not only original LSN (vy_stmt_lsn())
	 * in this variable, but also WAL LSN of deferred DELETE
	 * statements. This is needed to skip WAL recovery of both
	 * deferred and normal statements that have been dumped to
	 * disk. See vy_deferred_delete_on_replace() for more details.
	 */
	int64_t dump_lsn;
	/**
	 * Key definition for this index, extended with primary
	 * key parts.
	 */
	struct key_def *cmp_def;
	/** version is initially 0 and is incremented on every write */
	uint32_t version;
	/** Data dictionary cache version at the time of creation. */
	uint32_t space_cache_version;
	/**
	 * Generation of statements stored in the tree.
	 * Used as lsregion allocator identifier.
	 */
	int64_t generation;
	/**
	 * Format of statements stored in this in-memory index.
	 * Note, the statements don't reference the format by
	 * themselves, instead it is referenced once by vy_mem.
	 * This allows us to drop vy_mem in O(1).
	 */
	struct tuple_format *format;
	/**
	 * Number of active writers to this index.
	 *
	 * Incremented for modified in-memory trees when
	 * preparing a transaction. Decremented after writing
	 * to WAL or rollback.
	 */
	int pin_count;
	/**
	 * Condition variable signaled by vy_mem_unpin()
	 * if pin_count reaches 0.
	 */
	struct fiber_cond pin_cond;
};

/**
 * Pin an in-memory index.
 *
 * A pinned in-memory index can't be dumped until it's unpinned.
 */
static inline void
vy_mem_pin(struct vy_mem *mem)
{
	mem->pin_count++;
}

/**
 * Unpin an in-memory index.
 *
 * This function reverts the effect of vy_mem_pin().
 */
static inline void
vy_mem_unpin(struct vy_mem *mem)
{
	assert(mem->pin_count > 0);
	mem->pin_count--;
	if (mem->pin_count == 0)
		fiber_cond_broadcast(&mem->pin_cond);
}

/**
 * Wait until an in-memory index is unpinned.
 */
static inline void
vy_mem_wait_pinned(struct vy_mem *mem)
{
	while (mem->pin_count > 0)
		fiber_cond_wait(&mem->pin_cond);
}

/**
 * Instantiate a new in-memory level.
 *
 * @param env Vinyl memory environment.
 * @param key_def key definition.
 * @param format Format for REPLACE and DELETE tuples.
 * @param generation Generation of statements stored in the tree.
 * @param space_cache_version Data dictionary cache version
 * @retval new vy_mem instance on success.
 * @retval NULL on error, check diag.
 */
struct vy_mem *
vy_mem_new(struct vy_mem_env *env, struct key_def *cmp_def,
	   struct tuple_format *format, int64_t generation,
	   uint32_t space_cache_version);

/**
 * Delete in-memory level.
 */
void
vy_mem_delete(struct vy_mem *index);

/*
 * Return the older statement for the given one.
 */
struct vy_entry
vy_mem_older_lsn(struct vy_mem *mem, struct vy_entry entry);

/**
 * Insert a statement into the in-memory level.
 * @param mem        vy_mem.
 * @param entry      Vinyl statement.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
vy_mem_insert(struct vy_mem *mem, struct vy_entry entry);

/**
 * Insert an upsert statement into the mem.
 *
 * @param mem Mem to insert to.
 * @param entry Upsert statement to insert.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
vy_mem_insert_upsert(struct vy_mem *mem, struct vy_entry entry);

/**
 * Confirm insertion of a statement into the in-memory level.
 * @param mem        vy_mem.
 * @param entry      Vinyl statement.
 */
void
vy_mem_commit_stmt(struct vy_mem *mem, struct vy_entry entry);

/**
 * Remove a statement from the in-memory level.
 * @param mem        vy_mem.
 * @param entry      Vinyl statement.
 */
void
vy_mem_rollback_stmt(struct vy_mem *mem, struct vy_entry entry);

/**
 * Iterator for in-memory level.
 *
 * Return statements from vy_mem (in-memory index) based on
 * initial search key, iteration order and view lsn.
 *
 * All statements with lsn > vlsn are skipped.
 * The API allows to traverse over resulting statements within two
 * dimensions - key and lsn. next_key() switches to the youngest
 * statement of the next key, according to the iteration order,
 * and next_lsn() switches to an older statement for the same
 * key.
 */
struct vy_mem_iterator {
	/** Usage statistics */
	struct vy_mem_iterator_stat *stat;

	/* mem */
	struct vy_mem *mem;

	/* Search options */
	/**
	 * Iterator type, that specifies direction, start position and stop
	 * criteria if key == NULL: GT and EQ are changed to GE, LT to LE for
	 * beauty.
	 */
	enum iterator_type iterator_type;
	/** Key to search. */
	struct vy_entry key;
	/* LSN visibility, iterator shows values with lsn <= than that */
	const struct vy_read_view **read_view;

	/* State of iterator */
	/* Current position in tree */
	struct vy_mem_tree_iterator curr_pos;
	/*
	 * The pointer on a region allocated statement from vy_mem BPS tree.
	 * There is no guarantee that curr_pos points on curr in the tree.
	 * For example, cur_pos can be invalid but curr can point on a
	 * valid statement.
	 */
	struct vy_entry curr;
	/* data version from vy_mem */
	uint32_t version;

	/* Is false until first .._next_.. method is called */
	bool search_started;
	/**
	 * The iterator may return prepared (unconfirmed) statements only if
	 * this flag is set. If any prepared statements are skipped because of
	 * this flag, min_skipped_plsn will be set to the min LSN among all
	 * skipped prepared statements. The transaction is supposed to update
	 * its read view accordingly to guarantee serializability.
	 */
	bool is_prepared_ok;
	/**
	 * Initialized to INT64_MAX. Set to the min LSN among all skipped
	 * prepared statements if is_prepared_ok is false.
	 */
	int64_t min_skipped_plsn;
};

/**
 * Open an iterator over in-memory tree.
 */
void
vy_mem_iterator_open(struct vy_mem_iterator *itr, struct vy_mem_iterator_stat *stat,
		     struct vy_mem *mem, enum iterator_type iterator_type,
		     struct vy_entry key, const struct vy_read_view **rv,
		     bool is_prepared_ok);

/**
 * Advance a mem iterator to the next key.
 * The key history is returned in @history (empty if EOF).
 * Returns 0 on success, -1 on memory allocation error.
 */
NODISCARD int
vy_mem_iterator_next(struct vy_mem_iterator *itr,
		     struct vy_history *history);

/**
 * Advance a mem iterator to the key following @last.
 * The key history is returned in @history (empty if EOF).
 * Returns 0 on success, -1 on memory allocation error.
 */
NODISCARD int
vy_mem_iterator_skip(struct vy_mem_iterator *itr, struct vy_entry last,
		     struct vy_history *history);

/**
 * Check if a mem iterator was invalidated and needs to be restored.
 * If it does, set the iterator position to the newest statement for
 * the key following @last and return 1, otherwise return 0.
 * Returns -1 on memory allocation error.
 */
NODISCARD int
vy_mem_iterator_restore(struct vy_mem_iterator *itr, struct vy_entry last,
			struct vy_history *history);

/**
 * Close a mem iterator.
 */
void
vy_mem_iterator_close(struct vy_mem_iterator *itr);

/**
 * Simple stream over a mem. @see vy_stmt_stream.
 */
struct vy_mem_stream {
	/** Parent class, must be the first member */
	struct vy_stmt_stream base;
	/** Mem to stream */
	struct vy_mem *mem;
	/** Current position */
	struct vy_mem_tree_iterator curr_pos;
};

/**
 * Open a mem stream. Use vy_stmt_stream api for further work.
 */
void
vy_mem_stream_open(struct vy_mem_stream *stream, struct vy_mem *mem);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_MEM_H */
