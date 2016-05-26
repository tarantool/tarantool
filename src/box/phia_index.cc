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
#include "phia_engine.h"
#include "phia_space.h"
#include "phia_index.h"
#include "say.h"
#include "tuple.h"
#include "tuple_update.h"
#include "scoped_guard.h"
#include "schema.h"
#include "space.h"
#include "txn.h"
#include "cfg.h"
#include "phia.h"
#include <stdio.h>
#include <inttypes.h>
#include <bit/bit.h> /* load/store */

static uint64_t num_parts[8];

struct phia_document *
PhiaIndex::createDocument(const char *key, const char **keyend)
{
	assert(key_def->part_count <= 8);
	struct phia_document *obj = phia_document_new(db);
	if (obj == NULL)
		phia_raise();
	if (key == NULL)
		return obj;
	uint32_t i = 0;
	while (i < key_def->part_count) {
		char partname[32];
		snprintf(partname, sizeof(partname), "key_%" PRIu32, i);
		const char *part;
		uint32_t partsize;
		if (key_def->parts[i].type == STRING) {
			part = mp_decode_str(&key, &partsize);
		} else {
			num_parts[i] = mp_decode_uint(&key);
			part = (char *)&num_parts[i];
			partsize = sizeof(uint64_t);
		}
		if (partsize == 0)
			part = "";
		if (phia_document_set_field(obj, partname, part, partsize) == -1)
			phia_raise();
		i++;
	}
	if (keyend) {
		*keyend = key;
	}
	return obj;
}

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
PhiaIndex::findByKey(const char *key, uint32_t part_count = 0) const
{
	(void)part_count;
	struct phia_document *obj = ((PhiaIndex *)this)->
		createDocument(key, NULL);
	auto key_guard = make_scoped_guard([=] {
		phia_document_delete(obj);
	});
	struct phia_tx *transaction = NULL;
	/* engine_tx might be empty, even if we are in txn context.
	 *
	 * This can happen on a first-read statement. */
	if (in_txn())
		transaction = (struct phia_tx *) in_txn()->engine_tx;
	/* try to read from cache first, if nothing is found
	 * retry using disk */
	int rc;
	struct phia_document *result = NULL;
	if (transaction == NULL) {
		rc = phia_index_get(db, obj, &result, true);
	} else {
		rc = phia_get(transaction, obj, &result, true);
	}
	if (rc != 0)
		diag_raise();
	if (result == NULL) { /* cache miss or not found */
		if (transaction == NULL) {
			rc = phia_index_coget(db, obj, &result);
		} else {
			rc = phia_coget(transaction, obj, &result);
		}
		if (rc != 0)
			diag_raise();
	}
	if (result == NULL) /* not found */
		return NULL;

	auto result_guard = make_scoped_guard([=] {
		phia_document_delete(result);
	});
	return phia_tuple_new(result, key_def, format);
}

struct tuple *
PhiaIndex::replace(struct tuple*, struct tuple*, enum dup_replace_mode)
{
	/* This method is unused by phia index.
	 *
	 * see: phia_space.cc
	*/
	assert(0);
	return NULL;
}

struct phia_iterator {
	struct iterator base;
	const char *key;
	const char *keyend;
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
	struct phia_document *result;

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
		phia_document_delete(result);
	});
	return phia_tuple_new(result, it->key_def, it->space->format);
}

struct tuple *
phia_iterator_eq(struct iterator *ptr)
{
	struct phia_iterator *it = (struct phia_iterator *) ptr;
	ptr->next = phia_iterator_last;
	PhiaIndex *index = (PhiaIndex *)index_find(it->space, 0);
	return index->findByKey(it->key);
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
	struct phia_iterator *it = (struct phia_iterator *) ptr;
	assert(it->cursor == NULL);
	if (part_count > 0) {
		if (part_count != key_def->part_count) {
			tnt_raise(UnsupportedIndexFeature, this, "partial keys");
		}
	} else {
		key = NULL;
	}
	it->space = space_cache_find(key_def->space_id);
	it->key_def = key_def;
	it->key = key;
	it->env = env;
	it->db  = db;
	/* point-lookup iterator */
	if (type == ITER_EQ) {
		ptr->next = phia_iterator_eq;
		return;
	}
	/* prepare for the range scan */
	enum phia_order order;
	switch (type) {
	case ITER_ALL:
	case ITER_GE: order = PHIA_GE;
		break;
	case ITER_GT: order = PHIA_GT;
		break;
	case ITER_LE: order = PHIA_LE;
		break;
	case ITER_LT: order = PHIA_LT;
		break;
	default:
		return initIterator(ptr, type, key, part_count);
	}
	/* Position first key here, since key pointer might be
	 * unavailable from lua.
	 *
	 * Read from disk and fill cursor cache.
	 */
	PhiaIndex *index = (PhiaIndex *)this;
	struct phia_document *obj = index->createDocument(key, &it->keyend);
	it->cursor = phia_cursor_new(db, obj, order);
	if (it->cursor == NULL) {
		phia_document_delete(obj);
		phia_raise();
	}
	ptr->next = phia_iterator_next;
}
