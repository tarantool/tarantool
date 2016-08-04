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

/**
 * Allocate a new key_def with a set union of key parts from
 * first and second key defs. Parts of the new key_def consist
 * of first key_def's parts and those parts of the second key_def
 * that were not among the first parts.
 *
 * @throws OutOfMemory
 */
static struct key_def *
merge_key_defs(struct key_def *first, struct key_def *second)
{
	int new_part_count = first->part_count + second->part_count;
	struct key_part *sec_parts = second->parts;
	/*
	 * Find and remove part duplicates, i.e. parts counted
	 * twice since they are present in both key defs.
	 */
	for (struct key_part *iter = sec_parts,
	     *end = sec_parts + second->part_count; iter != end; ++iter)
		if (key_def_contains_fieldno(first, iter->fieldno)) {
			--new_part_count;
		}

	struct key_def *new_def;
	new_def =  key_def_new(first->space_id, first->iid, first->name,
			      first->type, &first->opts, new_part_count);

	/* Write position in the new key def. */
	uint32_t pos = 0;
	/* Append first key def's parts to the new key_def. */
	for (struct key_part *iter = first->parts,
	     *end = first->parts + first->part_count; iter != end; ++iter) {

		key_def_set_part(new_def, pos++, iter->fieldno, iter->type);
	}
	/* Set-append second key def's part to the new key def. */
	for (struct key_part *iter = sec_parts,
	     *end = sec_parts + second->part_count; iter != end; ++iter) {

		if (key_def_contains_fieldno(first, iter->fieldno)) {
			continue;
		}
		key_def_set_part(new_def, pos++, iter->fieldno, iter->type);
	}
	return new_def;
}

VinylIndex::VinylIndex(struct key_def *key_def_arg)
	: Index(key_def_arg), db(NULL)
{
	struct space *space = space_cache_find(key_def->space_id);
	VinylEngine *engine =
		(VinylEngine *)space->handler->engine;
	env = engine->env;
	format = space->format;
	tuple_format_ref(format, 1);
}

void
VinylIndex::open()
{
	assert(db == NULL);
	struct space *space;
	struct key_def *vinyl_key_def = key_def;
	auto guard = make_scoped_guard([&]{
		if (vinyl_key_def != key_def) {
			key_def_delete(vinyl_key_def);
		}
        });
	space = space_cache_find(key_def->space_id);
	/*
	 * If the index is not unique, add primary key
	 * to the end of parts.
	 */
	if (!key_def->opts.is_unique) {
		Index *primary = index_find(space, 0);
		/* Allocates a new (temporary) key_def. */
		vinyl_key_def = merge_key_defs(key_def, primary->key_def);
	}
	/* Create vinyl database. */
	db = vinyl_index_new(env, vinyl_key_def, space->format);
	if ((db == NULL) || vinyl_index_open(db))
		diag_raise();
}

VinylIndex::~VinylIndex() { }

size_t
VinylIndex::bsize() const
{
	return vinyl_index_bsize(db);
}

struct tuple *
VinylIndex::min(const char *key, uint32_t part_count) const
{
	struct iterator *it = allocIterator();
	auto guard = make_scoped_guard([=]{it->free(it);});
	initIterator(it, ITER_GE, key, part_count);
	return it->next(it);
}

struct tuple *
VinylIndex::max(const char *key, uint32_t part_count) const
{
	struct iterator *it = allocIterator();
	auto guard = make_scoped_guard([=]{it->free(it);});
	initIterator(it, ITER_LE, key, part_count);
	return it->next(it);
}

size_t
VinylIndex::count(enum iterator_type type, const char *key,
		  uint32_t part_count) const
{
	struct iterator *it = allocIterator();
	auto guard = make_scoped_guard([=]{it->free(it);});
	initIterator(it, type, key, part_count);
	size_t count = 0;
	struct tuple *tuple = NULL;
	while ((tuple = it->next(it)) != NULL)
		++count;
	return count;
}

struct tuple *
VinylIndex::findByKey(const char *key, uint32_t part_count) const
{
	assert(key_def->opts.is_unique && part_count == key_def->part_count);
	/*
	 * engine_tx might be empty, even if we are in txn context.
	 * This can happen on a first-read statement.
	 */
	struct vinyl_tx *transaction = in_txn() ?
		(struct vinyl_tx *) in_txn()->engine_tx : NULL;
	struct tuple *tuple = NULL;
	if (vinyl_coget(transaction, db, key, part_count, &tuple) != 0)
		diag_raise();
	return tuple;
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
	const VinylIndex *index;
	struct key_def *key_def;
	struct vinyl_env *env;
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

	uint32_t it_sc_version = ::sc_version;

	struct tuple *tuple;
	if (vinyl_cursor_conext(it->cursor, &tuple) != 0)
		diag_raise();
	if (tuple == NULL) { /* not found */
		/* immediately close the cursor */
		vinyl_cursor_delete(it->cursor);
		it->cursor = NULL;
		ptr->next = NULL;
		return NULL;
	}

	/* found */
	if (it_sc_version != ::sc_version)
		return NULL;
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
	const VinylIndex *index = it->index;
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
	it->index = this;
	it->key_def = vy_index_key_def(db);
	it->env = env;
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
		order = part_count > 0 ? VINYL_GT : VINYL_GE;
		ptr->next = vinyl_iterator_next;
		break;
	case ITER_LE:
		order = VINYL_LE;
		ptr->next = vinyl_iterator_next;
		break;
	case ITER_LT:
		order = part_count > 0 ? VINYL_LT : VINYL_LE;
		ptr->next = vinyl_iterator_next;
		break;
	case ITER_EQ:
		/* point-lookup iterator (optimization) */
		if ((key_def->opts.is_unique) &&
		    (part_count == key_def->part_count)) {
			ptr->next = vinyl_iterator_exact;
			return;
		}
		order = VINYL_GE;
		ptr->next = vinyl_iterator_eq;
		break;
	case ITER_REQ:
		/* point-lookup iterator (optimization) */
		if ((key_def->opts.is_unique) &&
		    (part_count == key_def->part_count)) {
			ptr->next = vinyl_iterator_exact;
			return;
		}
		order = VINYL_LE;
		ptr->next = vinyl_iterator_eq;
		break;
	default:
		return Index::initIterator(ptr, type, key, part_count);
	}
	it->cursor = vinyl_cursor_new(db, key, part_count, order);
	if (it->cursor == NULL)
		diag_raise();
}
