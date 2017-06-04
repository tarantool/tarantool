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

#include "ipc.h"
#include "index.h" /* enum iterator_type */
#include "vy_stmt.h" /* for comparators */
#include "vy_stmt_iterator.h" /* struct vy_stmt_iterator */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_mem;
struct vy_stmt;
struct lsregion;

/** @cond false */

struct tree_mem_key {
	const struct tuple *stmt;
	int64_t lsn;
};

/**
 * Internal. Extracted to speed up BPS tree.
 */
static int
vy_mem_tree_cmp(const struct tuple *a, const struct tuple *b,
		const struct key_def *key_def)
{
	int res = vy_stmt_compare(a, b, key_def);
	if (res)
		return res;
	int64_t a_lsn = vy_stmt_lsn(a), b_lsn = vy_stmt_lsn(b);
	return a_lsn > b_lsn ? -1 : a_lsn < b_lsn;
}

/**
 * Internal. Extracted to speed up BPS tree.
 */
static int
vy_mem_tree_cmp_key(const struct tuple *a, struct tree_mem_key *key,
		    const struct key_def *key_def)
{
	int res = vy_stmt_compare(a, key->stmt, key_def);
	if (res == 0) {
		if (key->lsn == INT64_MAX - 1)
			return 0;
		int64_t a_lsn = vy_stmt_lsn(a);
		res = a_lsn > key->lsn ? -1 : a_lsn < key->lsn;
	}
	return res;
}

#define VY_MEM_TREE_EXTENT_SIZE (16 * 1024)

#define BPS_TREE_NAME vy_mem_tree
#define BPS_TREE_BLOCK_SIZE 512
#define BPS_TREE_EXTENT_SIZE VY_MEM_TREE_EXTENT_SIZE
#define BPS_TREE_COMPARE(a, b, key_def) vy_mem_tree_cmp(a, b, key_def)
#define BPS_TREE_COMPARE_KEY(a, b, key_def) vy_mem_tree_cmp_key(a, b, key_def)
#define bps_tree_elem_t const struct tuple *
#define bps_tree_key_t struct tree_mem_key *
#define bps_tree_arg_t const struct key_def *
#define BPS_TREE_NO_DEBUG

#include <salad/bps_tree.h>

#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef bps_tree_arg_t
#undef BPS_TREE_NO_DEBUG

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
	/** Link in range->sealed list. */
	struct rlist in_sealed;
	/*
	 * Link in scheduler->dump_fifo list. The mem is
	 * added to the list when it has the first statement
	 * allocated in it.
	 */
	struct rlist in_dump_fifo;
	/** BPS tree */
	struct vy_mem_tree tree;
	/** The total size of all tuples in this tree in bytes */
	size_t used;
	/** The min and max values of stmt->lsn in this tree. */
	int64_t min_lsn;
	int64_t max_lsn;
	/* A key definition for this index. */
	const struct key_def *key_def;
	/** version is initially 0 and is incremented on every write */
	uint32_t version;
	/** Schema version at the time of creation. */
	uint32_t schema_version;
	/**
	 * Generation of statements stored in the tree.
	 * Used as lsregion allocator identifier.
	 */
	int64_t generation;
	/** Allocator for extents */
	struct lsregion *allocator;
	/**
	 * Format of vy_mem REPLACE and DELETE tuples without
	 * column mask.
	 */
	struct tuple_format *format;
	/** Format of vy_mem tuples with column mask. */
	struct tuple_format *format_with_colmask;
	/** Same as format, but for UPSERT tuples. */
	struct tuple_format *upsert_format;
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
	struct ipc_cond pin_cond;
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
		ipc_cond_broadcast(&mem->pin_cond);
}

/**
 * Wait until an in-memory index is unpinned.
 */
static inline void
vy_mem_wait_pinned(struct vy_mem *mem)
{
	while (mem->pin_count > 0)
		ipc_cond_wait(&mem->pin_cond);
}

/**
 * Instantiate a new in-memory level.
 *
 * @param allocator lsregion allocator to use for BPS tree extents
 * @param generation Generation of statements stored in the tree.
 * @param key_def key definition.
 * @param format Format for REPLACE and DELETE tuples.
 * @param format_with_colmask Format for tuples, which have
 *        column mask.
 * @param upsert_format Format for UPSERT tuples.
 * @param schema_version Schema version.
 * @retval new vy_mem instance on success.
 * @retval NULL on error, check diag.
 */
struct vy_mem *
vy_mem_new(struct lsregion *allocator, int64_t generation,
	   const struct key_def *key_def, struct tuple_format *format,
	   struct tuple_format *format_with_colmask,
	   struct tuple_format *upsert_format, uint32_t schema_version);

/**
 * Delete in-memory level.
 */
void
vy_mem_delete(struct vy_mem *index);

/*
 * Return the older statement for the given one.
 */
const struct tuple *
vy_mem_older_lsn(struct vy_mem *mem, const struct tuple *stmt);

/**
 * Insert a statement into the in-memory level.
 * @param mem        vy_mem.
 * @param stmt       Vinyl statement.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
int
vy_mem_insert(struct vy_mem *mem, const struct tuple *stmt);

/**
 * Confirm insertion of a statement into the in-memory level.
 * @param mem        vy_mem.
 * @param stmt       Vinyl statement.
 */
void
vy_mem_commit_stmt(struct vy_mem *mem, const struct tuple *stmt);

/**
 * Remove a statement from the in-memory level.
 * @param mem        vy_mem.
 * @param stmt       Vinyl statement.
 */
void
vy_mem_rollback_stmt(struct vy_mem *mem, const struct tuple *stmt);

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
	/** Parent class, must be the first member */
	struct vy_stmt_iterator base;

	/** Usage statistics */
	struct vy_iterator_stat *stat;

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
	const struct tuple *key;
	/* LSN visibility, iterator shows values with lsn <= than that */
	const struct vy_read_view **read_view;
	/**
	 * If not NULL, start iteration from the key following
	 * @before_first.
	 */
	struct tuple *before_first;

	/* State of iterator */
	/* Current position in tree */
	struct vy_mem_tree_iterator curr_pos;
	/*
	 * The pointer on a region allocated statement from vy_mem BPS tree.
	 * There is no guarantee that curr_pos points on curr_stmt in the tree.
	 * For example, cur_pos can be invalid but curr_stmt can point on a
	 * valid statement.
	 */
	const struct tuple *curr_stmt;
	/*
	 * Copy of the statement returned from one of public methods
	 * (restore/next_lsn/next_key). Need to store the copy, because can't
	 * return region allocated curr_stmt.
	 */
	struct tuple *last_stmt;
	/* data version from vy_mem */
	uint32_t version;

	/* Is false until first .._next_.. method is called */
	bool search_started;
};

/**
 * Open the iterator.
 */
void
vy_mem_iterator_open(struct vy_mem_iterator *itr, struct vy_iterator_stat *stat,
		     struct vy_mem *mem, enum iterator_type iterator_type,
		     const struct tuple *key, const struct vy_read_view **rv,
		     struct tuple *before_first);

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
