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
#include "vinyl_index.h"
#include "vinyl_engine.h"

#include <stdio.h>
#include <small/mempool.h>

#include "trivia/util.h"
#include "fiber.h"
#include "schema.h"
#include "txn.h"
#include "vinyl.h"
#include "tuple.h"

/**
 * Get (struct vy_index *) by (struct index *).
 * @param index vinyl_index to convert.
 * @retval Pointer to index->db.
 */
struct vy_index *
vy_index(struct index *index)
{
	return ((struct vinyl_index *) index)->db;
}

struct vinyl_iterator {
	struct iterator base;
	struct vy_env *env;
	struct vy_cursor *cursor;
	/** Memory pool the iterator was allocated from. */
	struct mempool *pool;
};

static void
vinyl_index_destroy(struct index *base)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	struct vinyl_engine *vinyl = (struct vinyl_engine *)base->engine;
	vy_delete_index(vinyl->env, index->db);
	free(index);
}

static void
vinyl_index_commit_create(struct index *base, int64_t signature)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	struct vinyl_engine *vinyl = (struct vinyl_engine *)base->engine;
	vy_index_commit_create(vinyl->env, index->db, signature);
}

static void
vinyl_index_commit_drop(struct index *base)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	struct vinyl_engine *vinyl = (struct vinyl_engine *)base->engine;
	vy_index_commit_drop(vinyl->env, index->db);
}

static int
vinyl_index_get(struct index *base, const char *key,
		uint32_t part_count, struct tuple **result)
{
	assert(base->def->opts.is_unique &&
	       part_count == base->def->key_def->part_count);
	struct vinyl_index *index = (struct vinyl_index *)base;
	struct vinyl_engine *vinyl = (struct vinyl_engine *)base->engine;
	/*
	 * engine_tx might be empty, even if we are in txn context.
	 * This can happen on a first-read statement.
	 */
	struct vy_tx *transaction = in_txn() ?
		(struct vy_tx *) in_txn()->engine_tx : NULL;
	struct tuple *tuple = NULL;
	if (vy_get(vinyl->env, transaction, index->db,
		   key, part_count, &tuple) != 0)
		return -1;
	if (tuple != NULL) {
		tuple = tuple_bless(tuple);
		if (tuple == NULL)
			return -1;
		tuple_unref(tuple);
	}
	*result = tuple;
	return 0;
}

static ssize_t
vinyl_index_bsize(struct index *base)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	return vy_index_bsize(index->db);
}

static int
vinyl_iterator_last(MAYBE_UNUSED struct iterator *ptr, struct tuple **ret)
{
	*ret = NULL;
	return 0;
}

static int
vinyl_iterator_next(struct iterator *base_it, struct tuple **ret)
{
	struct vinyl_iterator *it = (struct vinyl_iterator *) base_it;
	struct vy_env *env = it->env;
	struct tuple *tuple;

	/* found */
	if (vy_cursor_next(env, it->cursor, &tuple) != 0) {
		/* immediately close the cursor */
		vy_cursor_delete(env, it->cursor);
		it->cursor = NULL;
		it->base.next = vinyl_iterator_last;
		return -1;
	}
	if (tuple != NULL) {
		tuple = tuple_bless(tuple);
		if (tuple == NULL)
			return -1;
		tuple_unref(tuple);
		*ret = tuple;
		return 0;
	}

	/* immediately close the cursor */
	vy_cursor_delete(env, it->cursor);
	it->cursor = NULL;
	it->base.next = vinyl_iterator_last;
	*ret = NULL;
	return 0;
}

static void
vinyl_iterator_free(struct iterator *ptr)
{
	assert(ptr->free == vinyl_iterator_free);
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	if (it->cursor) {
		vy_cursor_delete(it->env, it->cursor);
		it->cursor = NULL;
	}
	mempool_free(it->pool, it);
}

static struct iterator *
vinyl_index_create_iterator(struct index *base, enum iterator_type type,
			    const char *key, uint32_t part_count)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	struct vinyl_engine *vinyl = (struct vinyl_engine *)base->engine;

	assert(part_count == 0 || key != NULL);
	struct vy_tx *tx = in_txn() ? in_txn()->engine_tx : NULL;
	if (type > ITER_GT) {
		diag_set(UnsupportedIndexFeature, base->def,
			 "requested iterator type");
		return NULL;
	}
	struct vinyl_iterator *it = mempool_alloc(&vinyl->iterator_pool);
	if (it == NULL) {
	        diag_set(OutOfMemory, sizeof(struct vinyl_iterator),
			 "mempool", "struct vinyl_iterator");
		return NULL;
	}
	iterator_create(&it->base, base);
	it->pool = &vinyl->iterator_pool;
	it->base.next = vinyl_iterator_next;
	it->base.free = vinyl_iterator_free;

	it->env = vinyl->env;
	it->cursor = vy_cursor_new(it->env, tx, index->db,
				   key, part_count, type);
	if (it->cursor == NULL) {
		mempool_free(&vinyl->iterator_pool, it);
		return NULL;
	}
	return (struct iterator *)it;
}

static void
vinyl_index_info(struct index *base, struct info_handler *handler)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	vy_index_info(index->db, handler);
}

static const struct index_vtab vinyl_index_vtab = {
	/* .destroy = */ vinyl_index_destroy,
	/* .commit_create = */ vinyl_index_commit_create,
	/* .commit_drop = */ vinyl_index_commit_drop,
	/* .size = */ generic_index_size,
	/* .bsize = */ vinyl_index_bsize,
	/* .min = */ generic_index_min,
	/* .max = */ generic_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ generic_index_count,
	/* .get = */ vinyl_index_get,
	/* .replace = */ generic_index_replace,
	/* .create_iterator = */ vinyl_index_create_iterator,
	/* .create_snapshot_iterator = */
		generic_index_create_snapshot_iterator,
	/* .info = */ vinyl_index_info,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

struct vinyl_index *
vinyl_index_new(struct vinyl_engine *vinyl, struct index_def *def,
		struct tuple_format *format, struct vy_index *pk)
{
	if (!mempool_is_initialized(&vinyl->iterator_pool)) {
		mempool_create(&vinyl->iterator_pool, cord_slab_cache(),
			       sizeof(struct vinyl_iterator));
	}

	struct vinyl_index *index =
		(struct vinyl_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct vinyl_index");
		return NULL;
	}
	struct vy_index *db = vy_new_index(vinyl->env, def, format, pk);
	if (db == NULL) {
		free(index);
		return NULL;
	}
	if (index_create(&index->base, (struct engine *)vinyl,
			 &vinyl_index_vtab, def) != 0) {
		vy_delete_index(vinyl->env, db);
		free(index);
		return NULL;
	}
	index->db = db;
	return index;
}

int
vinyl_index_open(struct vinyl_index *index)
{
	struct vinyl_engine *vinyl = (struct vinyl_engine *)index->base.engine;
	return vy_index_open(vinyl->env, index->db);
}
