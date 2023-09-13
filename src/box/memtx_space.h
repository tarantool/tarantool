#ifndef TARANTOOL_BOX_MEMTX_SPACE_H_INCLUDED
#define TARANTOOL_BOX_MEMTX_SPACE_H_INCLUDED
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
#include "space.h"
#include "tuple.h"
#include "memtx_engine.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct memtx_engine;

struct memtx_space {
	struct space base;
	/**
	 * Cumulative statistics on the memory usage by tuples in the space,
	 * grouped by the following arena types:
	 *  0 - TUPLE_ARENA_MEMTX;
	 *  1 - TUPLE_ARENA_MALLOC.
	 */
	struct tuple_info tuple_stat[2];
	/**
	 * This counter is used to generate unique ids for
	 * ephemeral spaces. Mostly used by SQL: values of this
	 * var are stored as separate field to hold non-unique
	 * tuples within one unique primary key.
	 */
	uint64_t rowid;
	/**
	 * A pointer to replace function, set to different values
	 * at different stages of recovery.
	 */
	int (*replace)(struct space *, struct tuple *, struct tuple *,
		       enum dup_replace_mode, struct tuple **);
};

/**
 * Update memory usage statistics of a space by subtracting old tuple's sizes
 * and adding new tuple's sizes. Used also for rollback by swapping old and new
 * tuples.
 *
 * @param space Instance of memtx space.
 * @param old_tuple Old tuple (replaced or deleted).
 * @param new_tuple New tuple (inserted).
 */
void
memtx_space_update_tuple_stat(struct space *space, struct tuple *old_tuple,
			      struct tuple *new_tuple);

int
memtx_space_replace_no_keys(struct space *, struct tuple *, struct tuple *,
			    enum dup_replace_mode, struct tuple **);
int
memtx_space_replace_build_next(struct space *, struct tuple *, struct tuple *,
			       enum dup_replace_mode, struct tuple **);
int
memtx_space_replace_primary_key(struct space *, struct tuple *, struct tuple *,
				enum dup_replace_mode, struct tuple **);
int
memtx_space_replace_all_keys(struct space *, struct tuple *, struct tuple *,
			     enum dup_replace_mode, struct tuple **);

struct space *
memtx_space_new(struct memtx_engine *memtx,
		struct space_def *def, struct rlist *key_list);

static inline bool
memtx_space_is_recovering(struct space *space)
{
	assert(space_is_memtx(space));
	struct memtx_engine *memtx = (struct memtx_engine *)space->engine;
	return memtx->state < MEMTX_OK;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_MEMTX_SPACE_H_INCLUDED */
