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

static char PHIA_STRING_MIN[] = { '\0' };
static char PHIA_STRING_MAX[1024]; /* initialized in PhiaIndex::PhiaIndex() */
static uint64_t num_parts[BOX_INDEX_PART_MAX];

void
phia_set_fields(struct key_def *key_def, struct phia_field *fields,
		const char **data, uint32_t part_count)
{
	for (uint32_t i = 0; i < part_count; i++) {
		switch (key_def->parts[i].type) {
		case NUM:
			num_parts[i] = mp_decode_uint(data);
			fields[i].data = (char *)&num_parts[i];
			fields[i].size = sizeof(uint64_t);
			break;
		case STRING:
			fields[i].data = (char *)
				mp_decode_str(data, &fields[i].size);
			break;
		default:
			mp_unreachable();
			return;
		}
	}
}

struct phia_tuple *
phia_tuple_from_key_data(struct phia_index *index, struct key_def *key_def,
			 const char *key, uint32_t part_count, int order)
{
	assert(part_count == 0 || key != NULL);
	assert(part_count <= key_def->part_count);
	struct phia_field fields[BOX_INDEX_PART_MAX + 1]; /* parts + value */
	phia_set_fields(key_def, fields, &key, part_count);
	/* Fill remaining parts of key */
	for (uint32_t i = part_count; i < key_def->part_count; i++) {
		struct phia_field *field = &fields[i];
		switch (key_def->parts[i].type) {
		case NUM:
			if (order == PHIA_LT || order == PHIA_LE) {
				num_parts[i] = UINT64_MAX;
				field->data = (char*)&num_parts[i];
				field->size = sizeof(uint64_t);
			} else {
				num_parts[i] = 0;
				field->data = (char*)&num_parts[i];
				field->size = sizeof(uint64_t);
			}
			break;
		case STRING:
			if (order == PHIA_LT || order == PHIA_LE) {
				field->data = PHIA_STRING_MAX;
				field->size = sizeof(PHIA_STRING_MAX);
			} else {
				field->data = PHIA_STRING_MIN;
				field->size = 0;
			}
			break;
		default:
			mp_unreachable();
			return NULL;
		}
	}
	/* Add an empty value. Value is stored after key parts. */
	struct phia_field *value_field = &fields[key_def->part_count];
	if (order == PHIA_LT || order == PHIA_LE) {
		value_field->data = PHIA_STRING_MAX;
		value_field->size = sizeof(PHIA_STRING_MAX);
	} else {
		value_field->data = PHIA_STRING_MIN;
		value_field->size = 0;
	}
	/* Create tuple */
	return phia_tuple_new(index, fields, key_def->part_count + 1);
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
	memset(PHIA_STRING_MAX, 0xff, sizeof(PHIA_STRING_MAX));
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
		phia_tuple_from_key_data(db, key_def, key, part_count, PHIA_EQ);
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
	assert(0);
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

struct tuple *
phia_iterator_eq(struct iterator *ptr)
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
	it->env = env;
	it->db  = db;
	/* point-lookup iterator (optimization) */
	if (type == ITER_EQ && part_count == key_def->part_count) {
		it->key = key;
		it->part_count = part_count;
		ptr->next = phia_iterator_eq;
		return;
	}
	/* prepare for the range scan */
	enum phia_order order;
	switch (type) {
	case ITER_ALL:
	case ITER_GE:
		order = PHIA_GE;
		break;
	case ITER_GT:
		order = PHIA_GT;
		break;
	case ITER_LE:
		order = PHIA_LE;
		break;
	case ITER_LT:
		order = PHIA_LT;
		break;
	case ITER_EQ:
		order = PHIA_EQ;
		break;
	default:
		return Index::initIterator(ptr, type, key, part_count);
	}
	/* Position first key here, since key pointer might be
	 * unavailable from lua.
	 *
	 * Read from disk and fill cursor cache.
	 */
	struct phia_tuple *phia_key =
		phia_tuple_from_key_data(db, key_def, key, part_count, order);
	it->cursor = phia_cursor_new(db, phia_key, order);
	phia_tuple_unref(db, phia_key);
	if (it->cursor == NULL)
		diag_raise();
	ptr->next = phia_iterator_next;
}
