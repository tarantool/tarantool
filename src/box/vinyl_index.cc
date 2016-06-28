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
#include <inttypes.h>
#include <bit/bit.h> /* load/store */

#include "trivia/util.h"
#include "cfg.h"
#include "say.h"
#include "scoped_guard.h"

#include "vinyl_engine.h"
#include "vinyl_space.h"
#include "tuple.h"
#include "tuple_update.h"
#include "schema.h"
#include "space.h"
#include "txn.h"
#include "vinyl.h"

VinylIndex::VinylIndex(struct key_def *key_def_arg)
	: Index(key_def_arg)
{
	struct space *space = space_cache_find(key_def->space_id);
	VinylEngine *engine =
		(VinylEngine *)space->handler->engine;
	env = engine->env;
	int rc;
	vinyl_workers_start(env);
	/* create database */
	db = vinyl_index_new(env, key_def);
	if (db == NULL)
		vinyl_raise();
	/* start two-phase recovery for a space:
	 * a. created after snapshot recovery
	 * b. created during log recovery
	*/
	rc = vinyl_index_open(db);
	if (rc == -1)
		vinyl_raise();
	format = space->format;
	tuple_format_ref(format, 1);
}

VinylIndex::~VinylIndex()
{
	if (db == NULL)
		return;
	/* schedule database shutdown */
	int rc = vinyl_index_close(db);
	if (rc == -1)
		goto error;
	return;
error:;
	say_info("vinyl space %" PRIu32 " close error: %s",
			 key_def->space_id, diag_last_error(diag_get())->errmsg);
}

size_t
VinylIndex::size() const
{
	return vinyl_index_size(db);
}

size_t
VinylIndex::bsize() const
{
	return vinyl_index_bsize(db);
}

struct tuple *
VinylIndex::findByKey(struct vinyl_tuple *vinyl_key) const
{
	struct vinyl_tx *transaction = NULL;
	/* engine_tx might be empty, even if we are in txn context.
	 *
	 * This can happen on a first-read statement. */
	if (in_txn())
		transaction = (struct vinyl_tx *) in_txn()->engine_tx;
	/* try to read from cache first, if nothing is found
	 * retry using disk */
	struct vinyl_tuple *result = NULL;
	int rc = vinyl_coget(transaction, db, vinyl_key, &result);
	if (rc != 0)
		diag_raise();
	if (result == NULL) /* not found */
		return NULL;

	struct tuple *tuple = vinyl_convert_tuple(db, result, format);
	vinyl_tuple_unref(db, result);
	if (tuple == NULL)
		diag_raise();
	return tuple;
}

struct tuple *
VinylIndex::findByKey(const char *key, uint32_t part_count) const
{
	assert(key_def->opts.is_unique && part_count == key_def->part_count);
	struct vinyl_tuple *vinyl_key =
		vinyl_tuple_from_key_data(db, key, part_count, VINYL_EQ);
	if (vinyl_key == NULL)
		diag_raise();
	auto key_guard = make_scoped_guard([=] {
		vinyl_tuple_unref(db, vinyl_key);
	});
	return findByKey(vinyl_key);
}

struct tuple *
VinylIndex::replace(struct tuple*, struct tuple*, enum dup_replace_mode)
{
	/* This method is unused by vinyl index.
	 *
	 * see: vinyl_space.cc
	*/
	unreachable();
	return NULL;
}

struct vinyl_iterator {
	struct iterator base;
	/* key and part_count used only for EQ */
	const char *key;
	int part_count;
	struct space *space;
	struct key_def *key_def;
	struct vinyl_env *env;
	struct vinyl_index *db;
	struct vinyl_cursor *cursor;
};

void
vinyl_iterator_free(struct iterator *ptr)
{
	assert(ptr->free == vinyl_iterator_free);
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	if (it->cursor) {
		vinyl_cursor_delete(it->cursor);
		it->cursor = NULL;
	}
	free(ptr);
}

struct tuple *
vinyl_iterator_last(struct iterator *ptr __attribute__((unused)))
{
	return NULL;
}

struct tuple *
vinyl_iterator_next(struct iterator *ptr)
{
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	assert(it->cursor != NULL);
	struct vinyl_tuple *result;

	uint32_t it_sc_version = ::sc_version;

	if (vinyl_cursor_conext(it->cursor, &result) != 0)
		diag_raise();
	if (result == NULL) { /* not found */
		/* immediately close the cursor */
		vinyl_cursor_delete(it->cursor);
		it->cursor = NULL;
		ptr->next = NULL;
		return NULL;
	}

	/* found */
	if (it_sc_version != ::sc_version) {
		vinyl_tuple_unref(it->db, result);
		return NULL;
	}
	struct tuple *tuple = vinyl_convert_tuple(it->db, result,
		it->space->format);
	vinyl_tuple_unref(it->db, result);
	if (tuple == NULL)
		diag_raise();
	return tuple;
}

static struct tuple *
vinyl_iterator_eq(struct iterator *ptr)
{
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	struct tuple *tuple = vinyl_iterator_next(ptr);
	if (tuple == NULL)
		return NULL; /* not found */

	/* check equality */
	if (tuple_compare_with_key(tuple, it->key, it->part_count,
				it->key_def) != 0) {
		/*
		 * tuple is destroyed on the next call to
		 * box_tuple_XXX() API. See box_tuple_ref()
		 * comments.
		 */
		vinyl_cursor_delete(it->cursor);
		it->cursor = NULL;
		ptr->next = NULL;
		return NULL;
	}
	return tuple;
}

static struct tuple *
vinyl_iterator_exact(struct iterator *ptr)
{
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	ptr->next = vinyl_iterator_last;
	VinylIndex *index = (VinylIndex *)index_find(it->space, 0);
	return index->findByKey(it->key, it->part_count);
}

struct iterator *
VinylIndex::allocIterator() const
{
	struct vinyl_iterator *it =
		(struct vinyl_iterator *) calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct vinyl_iterator),
			  "Vinyl Index", "iterator");
	}
	it->base.next = vinyl_iterator_last;
	it->base.free = vinyl_iterator_free;
	return (struct iterator *) it;
}

void
VinylIndex::initIterator(struct iterator *ptr,
                          enum iterator_type type,
                          const char *key, uint32_t part_count) const
{
	assert(part_count == 0 || key != NULL);
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	assert(it->cursor == NULL);
	it->space = space_cache_find(key_def->space_id);
	it->key_def = key_def;
	it->env = env;
	it->db  = db; /* refcounted by vinyl_cursor_new() */
	it->key = key;
	it->part_count = part_count;

	enum vinyl_order order;
	switch (type) {
	case ITER_ALL:
	case ITER_GE:
		order = VINYL_GE;
		ptr->next = vinyl_iterator_next;
		break;
	case ITER_GT:
		order = VINYL_GT;
		ptr->next = vinyl_iterator_next;
		break;
	case ITER_LE:
		order = VINYL_LE;
		ptr->next = vinyl_iterator_next;
		break;
	case ITER_LT:
		order = VINYL_LT;
		ptr->next = vinyl_iterator_next;
		break;
	case ITER_EQ:
		/* point-lookup iterator (optimization) */
		if (part_count == key_def->part_count) {
			ptr->next = vinyl_iterator_exact;
			return;
		}
		order = VINYL_GE;
		ptr->next = vinyl_iterator_eq;
		break;
	case ITER_REQ:
		/* point-lookup iterator (optimization) */
		if (part_count == key_def->part_count) {
			ptr->next = vinyl_iterator_exact;
			return;
		}
		order = VINYL_LE;
		ptr->next = vinyl_iterator_eq;
		break;
	default:
		return Index::initIterator(ptr, type, key, part_count);
	}

	struct vinyl_tuple *vinyl_key =
		vinyl_tuple_from_key_data(db, key, part_count, order);
	if (vinyl_key == NULL)
		diag_raise();
	it->cursor = vinyl_cursor_new(db, vinyl_key, order);
	vinyl_tuple_unref(db, vinyl_key);
	if (it->cursor == NULL)
		diag_raise();
}
