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

#include <stdio.h>

#include "trivia/util.h"
#include "scoped_guard.h"
#include "schema.h"
#include "txn.h"
#include "vinyl.h"
#include "tuple.h"
#include "cfg.h"

/**
 * Get (struct vy_index *) by (struct index *).
 * @param index vinyl_index to convert.
 * @retval Pointer to index->db.
 */
extern "C" struct vy_index *
vy_index(struct index *index)
{
	return ((struct vinyl_index *) index)->db;
}

struct vinyl_iterator {
	struct iterator base;
	struct vy_env *env;
	struct vy_cursor *cursor;
};

static void
vinyl_index_destroy(struct index *base)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	vy_delete_index(index->env, index->db);
	free(index);
}

static void
vinyl_index_commit_create(struct index *base, int64_t signature)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	vy_index_commit_create(index->env, index->db, signature);
}

static void
vinyl_index_commit_drop(struct index *base)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	vy_index_commit_drop(index->env, index->db);
}

static struct tuple *
vinyl_index_get(struct index *base, const char *key, uint32_t part_count)
{
	assert(base->def->opts.is_unique &&
	       part_count == base->def->key_def->part_count);
	struct vinyl_index *index = (struct vinyl_index *)base;
	/*
	 * engine_tx might be empty, even if we are in txn context.
	 * This can happen on a first-read statement.
	 */
	struct vy_tx *transaction = in_txn() ?
		(struct vy_tx *) in_txn()->engine_tx : NULL;
	struct tuple *tuple = NULL;
	if (vy_get(index->env, transaction, index->db,
		   key, part_count, &tuple) != 0)
		diag_raise();
	if (tuple != NULL) {
		tuple = tuple_bless_xc(tuple);
		tuple_unref(tuple);
	}
	return tuple;
}

static size_t
vinyl_index_bsize(struct index *base)
{
	struct vinyl_index *index = (struct vinyl_index *)base;
	return vy_index_bsize(index->db);
}

static struct tuple *
vinyl_index_min(struct index *index, const char *key, uint32_t part_count)
{
	struct iterator *it = index_alloc_iterator_xc(index);
	auto guard = make_scoped_guard([=]{it->free(it);});
	index_init_iterator_xc(index, it, ITER_GE, key, part_count);
	return iterator_next_xc(it);
}

static struct tuple *
vinyl_index_max(struct index *index, const char *key, uint32_t part_count)
{
	struct iterator *it = index_alloc_iterator_xc(index);
	auto guard = make_scoped_guard([=]{it->free(it);});
	index_init_iterator_xc(index, it, ITER_LE, key, part_count);
	return iterator_next_xc(it);
}

static size_t
vinyl_index_count(struct index *index, enum iterator_type type,
		  const char *key, uint32_t part_count)
{
	struct iterator *it = index_alloc_iterator_xc(index);
	auto guard = make_scoped_guard([=]{it->free(it);});
	index_init_iterator_xc(index, it, type, key, part_count);
	size_t count = 0;
	struct tuple *tuple = NULL;
	while ((tuple = iterator_next_xc(it)) != NULL)
		++count;
	return count;
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
	free(ptr);
}

static struct iterator *
vinyl_index_alloc_iterator(void)
{
	struct vinyl_iterator *it =
	        (struct vinyl_iterator *) calloc(1, sizeof(*it));
	if (it == NULL) {
	        tnt_raise(OutOfMemory, sizeof(struct vinyl_iterator),
	                  "calloc", "vinyl_iterator");
	}
	it->base.next = vinyl_iterator_last;
	it->base.free = vinyl_iterator_free;
	return (struct iterator *) it;
}

static void
vinyl_index_init_iterator(struct index *base, struct iterator *ptr,
			  enum iterator_type type,
			  const char *key, uint32_t part_count)
{
	assert(part_count == 0 || key != NULL);
	struct vinyl_index *index = (struct vinyl_index *)base;
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	struct vy_tx *tx =
		in_txn() ? (struct vy_tx *) in_txn()->engine_tx : NULL;
	assert(it->cursor == NULL);
	it->env = index->env;
	ptr->next = vinyl_iterator_next;
	if (type > ITER_GT || type < 0)
		tnt_raise(UnsupportedIndexFeature, base->def,
			  "requested iterator type");

	it->cursor = vy_cursor_new(index->env, tx, index->db,
				   key, part_count, type);
	if (it->cursor == NULL)
		diag_raise();
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
	/* .min = */ vinyl_index_min,
	/* .max = */ vinyl_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ vinyl_index_count,
	/* .get = */ vinyl_index_get,
	/* .replace = */ generic_index_replace,
	/* .alloc_iterator = */ vinyl_index_alloc_iterator,
	/* .init_iterator = */ vinyl_index_init_iterator,
	/* .create_snapshot_iterator = */
		generic_index_create_snapshot_iterator,
	/* .info = */ vinyl_index_info,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

struct vinyl_index *
vinyl_index_new(struct vy_env *env, struct index_def *def,
		struct tuple_format *format, struct vy_index *pk)
{
	struct vinyl_index *index =
		(struct vinyl_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		tnt_raise(OutOfMemory, sizeof(*index),
			  "malloc", "struct vinyl_index");
	}
	struct vy_index *db = vy_new_index(env, def, format, pk);
	if (db == NULL) {
		free(index);
		diag_raise();
	}
	if (index_create(&index->base, &vinyl_index_vtab, def) != 0) {
		vy_delete_index(env, db);
		free(index);
		diag_raise();
	}
	index->env = env;
	index->db = db;
	return index;
}

void
vinyl_index_open(struct vinyl_index *index)
{
	if (vy_index_open(index->env, index->db,
			  cfg_geti("force_recovery")) != 0)
	    diag_raise();
}
