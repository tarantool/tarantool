#ifndef INCLUDES_TARANTOOL_BOX_VY_POINT_ITERATOR_H
#define INCLUDES_TARANTOOL_BOX_VY_POINT_ITERATOR_H
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

#include <small/rlist.h>

/**
 * @file
 * Point-lookup iterator
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_run_env;
struct vy_index;
struct vy_tx;
struct vy_read_view;
struct tuple;


/**
 * ID of an iterator source type. Can be used in bitmaps.
 */
enum iterator_src_type {
	ITER_SRC_TXW = 1,
	ITER_SRC_CACHE = 2,
	ITER_SRC_MEM = 4,
	ITER_SRC_RUN = 8,
};

/**
 * History of a key in vinyl is a continuous sequence of statements of the
 * same key in order of decreasing lsn. The history can be represented as a
 * list, the structure below describes one node of the list.
 */
struct vy_stmt_history_node {
	/* Type of source that the history statement came from */
	enum iterator_src_type src_type;
	/* The history statement. Referenced for runs. */
	struct tuple *stmt;
	/* Link in the history list */
	struct rlist link;
};

/**
 * Point iterator is a special read iterator that is designed for
 * retrieving one value from index by a full key (all parts are present).
 *
 * Iterator collects necessary history of the given key from different sources
 * (txw, cache, mems, runs) that consists of some number of sequential upserts
 * and possibly one terminal statement (replace or delete). The iterator
 * sequentially scans txw, cache, mems and runs until a terminal statement is
 * met. After reading the slices the iterator checks that the list of mems
 * hasn't been changed and restarts if it is the case.
 * After the history is collected the iterator calculates resultant statement
 * and, if the result is the latest version of the key, adds it to cache.
 */
struct vy_point_iterator {
	/** Vinyl run environment. */
	struct vy_run_env *run_env;
	/* Search location and options */
	struct vy_index *index;
	struct vy_tx *tx;
	const struct vy_read_view **p_read_view;
	struct tuple *key;

	/**
	 *  For compatibility reasons, the iterator references the
	 * resultant statement until own destruction.
	 */
	struct tuple *curr_stmt;
};

/**
 * Create an iterator by full key.
 */
void
vy_point_iterator_open(struct vy_point_iterator *itr,
		       struct vy_run_env *run_env, struct vy_index *index,
		       struct vy_tx *tx, const struct vy_read_view **rv,
		       struct tuple *key);


/**
 * Free resources and close the iterator.
 */
void
vy_point_iterator_close(struct vy_point_iterator *itr);

/*
 * Get a resultant tuple from the iterator. Actually do not change
 * iterator state thus second call will return the same statement
 * (unlike all other iterators that would return NULL on the second call)
 */
int
vy_point_iterator_get(struct vy_point_iterator *itr, struct tuple **result);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_POINT_ITERATOR_H */
