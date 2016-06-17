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
#include "phia_index.h"

#include <stdio.h>
#include <inttypes.h>
#include <bit/bit.h> /* load/store */

#include "trivia/util.h"
#include "cfg.h"
#include "say.h"
#include "scoped_guard.h"

#include "phia_engine.h"
#include "phia_space.h"
#include "tuple.h"
#include "tuple_update.h"
#include "schema.h"
#include "space.h"
#include "txn.h"
#include "phia.h"

PhiaIndex::PhiaIndex(struct key_def *key_def_arg)
	: Index(key_def_arg)
{
	struct space *space = space_cache_find(key_def->space_id);
	PhiaEngine *engine =
		(PhiaEngine *)space->handler->engine;
	env = engine->env;
	int rc;
	phia_workers_start(env);
	/* create database */
	db = phia_index_new(env, key_def);
	if (db == NULL)
		phia_raise();
	/* start two-phase recovery for a space:
	 * a. created after snapshot recovery
	 * b. created during log recovery
	*/
	rc = phia_index_open(db);
	if (rc == -1)
		phia_raise();
	format = space->format;
	tuple_format_ref(format, 1);
}

PhiaIndex::~PhiaIndex()
{
	if (db == NULL)
		return;
	/* schedule database shutdown */
	int rc = phia_index_close(db);
	if (rc == -1)
		goto error;
	/* unref database object */
	rc = phia_index_delete(db);
	if (rc == -1)
		goto error;
	return;
error:;
	say_info("phia space %" PRIu32 " close error: %s",
			 key_def->space_id, diag_last_error(diag_get())->errmsg);
}

size_t
PhiaIndex::size() const
{
	return phia_index_size(db);
}

size_t
PhiaIndex::bsize() const
{
	return phia_index_bsize(db);
}

struct tuple *
PhiaIndex::findByKey(struct phia_tuple *phia_key) const
{
	struct phia_tx *transaction = NULL;
	/* engine_tx might be empty, even if we are in txn context.
	 *
	 * This can happen on a first-read statement. */
	if (in_txn())
		transaction = (struct phia_tx *) in_txn()->engine_tx;
	/* try to read from cache first, if nothing is found
	 * retry using disk */
	int rc;
	struct phia_tuple *result = NULL;
	if (transaction == NULL) {
		rc = phia_index_get(db, phia_key, &result, true);
	} else {
		rc = phia_get(transaction, db, phia_key, &result, true);
	}
	if (rc != 0)
		diag_raise();
	if (result == NULL) { /* cache miss or not found */
		if (transaction == NULL) {
			rc = phia_index_coget(db, phia_key, &result);
		} else {
			rc = phia_coget(transaction, db, phia_key, &result);
		}
		if (rc != 0)
			diag_raise();
	}
	if (result == NULL) /* not found */
		return NULL;

	return phia_convert_tuple(db, result, key_def, format);
}

struct tuple *
PhiaIndex::findByKey(const char *key, uint32_t part_count) const
{
	assert(key_def->opts.is_unique && part_count == key_def->part_count);
	struct phia_tuple *phia_key =
		phia_tuple_from_key_data(db, key, part_count, PHIA_EQ);
	if (phia_key == NULL)
		diag_raise();
	auto key_guard = make_scoped_guard([=] {
		phia_tuple_unref(db, phia_key);
	});
	return findByKey(phia_key);
}

struct tuple *
PhiaIndex::replace(struct tuple*, struct tuple*, enum dup_replace_mode)
{
	/* This method is unused by phia index.
	 *
	 * see: phia_space.cc
	*/
	unreachable();
	return NULL;
}

struct phia_iterator {
	struct iterator base;
	/* key and part_count used only for EQ */
	const char *key;
	int part_count;
	struct space *space;
	struct key_def *key_def;
	struct phia_env *env;
	struct phia_index *db;
	struct phia_cursor *cursor;
};

void
phia_iterator_free(struct iterator *ptr)
{
	assert(ptr->free == phia_iterator_free);
	struct phia_iterator *it = (struct phia_iterator *) ptr;
	if (it->cursor) {
		phia_cursor_delete(it->cursor);
		it->cursor = NULL;
	}
	free(ptr);
}

struct tuple *
phia_iterator_last(struct iterator *ptr __attribute__((unused)))
{
	return NULL;
}

struct tuple *
phia_iterator_next(struct iterator *ptr)
{
	struct phia_iterator *it = (struct phia_iterator *) ptr;
	assert(it->cursor != NULL);
	struct phia_tuple *result;

	/* read from cache */
	if (phia_cursor_next(it->cursor, &result, true) != 0)
		diag_raise();
	if (result == NULL) { /* cache miss or not found */
		/* switch to asynchronous mode (read from disk) */
		if (phia_cursor_conext(it->cursor, &result) != 0)
			diag_raise();
	}
	if (result == NULL) { /* not found */
		/* immediately close the cursor */
		phia_cursor_delete(it->cursor);
		it->cursor = NULL;
		ptr->next = NULL;
		return NULL;
	}

	/* found */
	auto result_guard = make_scoped_guard([=]{
		phia_tuple_unref(it->db, result);
	});
	return phia_convert_tuple(it->db, result, it->key_def, it->space->format);
}

static struct tuple *
phia_iterator_eq(struct iterator *ptr)
{
	struct phia_iterator *it = (struct phia_iterator *) ptr;
	struct tuple *tuple = phia_iterator_next(ptr);
	if (tuple == NULL)
		return NULL; /* not found */

	/* check equality */
	if (tuple_compare_with_key(tuple, it->key, it->part_count,
				it->key_def) != 0) {
		tuple_delete(tuple);
		phia_cursor_delete(it->cursor);
		it->cursor = NULL;
		ptr->next = NULL;
		return NULL;
	}
	return tuple;
}

static struct tuple *
phia_iterator_exact(struct iterator *ptr)
{
	struct phia_iterator *it = (struct phia_iterator *) ptr;
	ptr->next = phia_iterator_last;
	PhiaIndex *index = (PhiaIndex *)index_find(it->space, 0);
	return index->findByKey(it->key, it->part_count);
}

struct iterator *
PhiaIndex::allocIterator() const
{
	struct phia_iterator *it =
		(struct phia_iterator *) calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct phia_iterator),
			  "Phia Index", "iterator");
	}
	it->base.next = phia_iterator_last;
	it->base.free = phia_iterator_free;
	return (struct iterator *) it;
}

void
PhiaIndex::initIterator(struct iterator *ptr,
                          enum iterator_type type,
                          const char *key, uint32_t part_count) const
{
	assert(part_count == 0 || key != NULL);
	struct phia_iterator *it = (struct phia_iterator *) ptr;
	assert(it->cursor == NULL);
	it->space = space_cache_find(key_def->space_id);
	it->key_def = key_def;
	it->env = env;
	it->db  = db;
	it->key = key;
	it->part_count = part_count;

	enum phia_order order;
	switch (type) {
	case ITER_ALL:
	case ITER_GE:
		order = PHIA_GE;
		ptr->next = phia_iterator_next;
		break;
	case ITER_GT:
		order = PHIA_GT;
		ptr->next = phia_iterator_next;
		break;
	case ITER_LE:
		order = PHIA_LE;
		ptr->next = phia_iterator_next;
		break;
	case ITER_LT:
		order = PHIA_LT;
		ptr->next = phia_iterator_next;
		break;
	case ITER_EQ:
		/* point-lookup iterator (optimization) */
		if (part_count == key_def->part_count) {
			ptr->next = phia_iterator_exact;
			return;
		}
		order = PHIA_GE;
		ptr->next = phia_iterator_eq;
		break;
	case ITER_REQ:
		/* point-lookup iterator (optimization) */
		if (part_count == key_def->part_count) {
			ptr->next = phia_iterator_exact;
			return;
		}
		order = PHIA_LE;
		ptr->next = phia_iterator_eq;
		break;
	default:
		return Index::initIterator(ptr, type, key, part_count);
	}

	struct phia_tuple *phia_key =
		phia_tuple_from_key_data(db, key, part_count, order);
	if (phia_key == NULL)
		diag_raise();
	it->cursor = phia_cursor_new(db, phia_key, order);
	phia_tuple_unref(db, phia_key);
	if (it->cursor == NULL)
		diag_raise();
}
