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
 * Get (struct vy_index *) by an space index with the specified
 * identifier. If the index is not found then set the
 * corresponding error in the diagnostics area.
 * @param space Vinyl space.
 * @param iid   Identifier of the index for search.
 *
 * @retval not NULL Pointer to index->db
 * @retval NULL     The index is not found.
 */
extern "C" struct vy_index *
vy_index_find(struct space *space, uint32_t iid)
{
	Index *index = space_index(space, iid);
	if (index == NULL) {
		diag_set(ClientError, ER_NO_SUCH_INDEX, iid,
			 space_name(space));
		error_log(diag_last_error(diag_get()));
		return NULL;
	}
	return ((struct VinylIndex *) index)->db;
}

/**
 * Get (struct vy_index *) by (struct Index *).
 * @param index VinylIndex to convert.
 * @retval Pointer to index->db.
 */
extern "C" struct vy_index *
vy_index(struct Index *index)
{
	return ((struct VinylIndex *) index)->db;
}

struct vinyl_iterator {
	struct iterator base;
	const char *key;
	int part_count;
	const VinylIndex *index;
	struct key_def *key_def;
	struct vy_cursor *cursor;
};

VinylIndex::VinylIndex(struct vy_env *env_arg, struct key_def *key_def_arg)
	:Index(key_def_arg)
	 ,env(env_arg)
	 ,db(NULL)
{}

struct tuple*
VinylIndex::findByKey(const char *key, uint32_t part_count) const
{
	assert(key_def->opts.is_unique && part_count == key_def->part_count);
	/*
	 * engine_tx might be empty, even if we are in txn context.
	 * This can happen on a first-read statement.
	 */
	struct vy_tx *transaction = in_txn() ?
		(struct vy_tx *) in_txn()->engine_tx : NULL;
	struct tuple *tuple = NULL;
	if (vy_get(transaction, db, key, part_count, &tuple) != 0)
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

size_t
VinylIndex::bsize() const
{
	return vy_index_bsize(db);
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

VinylPrimaryIndex::VinylPrimaryIndex(struct vy_env *env_arg,
				     struct key_def *key_def_arg)
	:VinylIndex(env_arg, key_def_arg)
{}

void
VinylPrimaryIndex::open()
{
	assert(db == NULL);
	/* Create vinyl database. */
	db = vy_index_new(env, key_def, key_def, key_def,
			  space_by_id(key_def->iid));
	if (db == NULL || vy_index_open(db))
		diag_raise();
}

VinylSecondaryIndex::VinylSecondaryIndex(struct vy_env *env_arg,
					 VinylPrimaryIndex *pk_arg,
					 struct key_def *key_def_arg)
	:VinylIndex(env_arg, key_def_arg)
	 ,key_def_tuple_to_key(NULL)
	 ,key_def_secondary_to_primary(NULL)
	 ,column_mask(0)
	 ,primary_index(pk_arg)
{
	/* Calculate the bitmask of columns used in this index. */
	for (uint32_t i = 0; i < key_def->part_count; ++i) {
		uint32_t fieldno = key_def->parts[i].fieldno;
		if (fieldno >= 64) {
			column_mask = UINT64_MAX;
			break;
		}
		column_mask |= ((uint64_t)1) << (63 - fieldno);
	}
}

/**
 * Get tuple from the primary index by the partial tuple from secondary index.
 */
static struct tuple *
lookup_full_tuple(const VinylSecondaryIndex *index, struct tuple *tuple,
		  struct vy_tx *tx)
{
	assert(index->key_def->iid != 0);
	const char *primary_key;
	/* Fetch the primary key from the secondary index tuple. */
	struct key_def *def = index->key_def_secondary_to_primary;
	primary_key = tuple_extract_key(tuple, def, NULL);
	/* Fetch the tuple from the primary index. */
	mp_decode_array(&primary_key); /* Skip array header. */
	if (vy_get(tx, index->primary_index->db, primary_key,
		   def->part_count, &tuple) != 0) {

		diag_raise();
	}
	return tuple;
}

struct tuple*
VinylSecondaryIndex::findByKey(const char *key, uint32_t part_count) const
{
	struct tuple *tuple = VinylIndex::findByKey(key, part_count);
	if (tuple) {
		struct vy_tx *transaction = in_txn() ?
			(struct vy_tx *) in_txn()->engine_tx : NULL;
		/*
		 * A secondary index does not store all tuple fields, but
		 * only the fields participating in the index and the fields
		 * of the primary key. Fetch the full tuple in the primary
		 * index.
		 */
		return lookup_full_tuple(this, tuple, transaction);
	}
	return NULL;
}

void
VinylSecondaryIndex::open()
{
	assert(db == NULL);
	key_def_tuple_to_key = key_def_merge(key_def, primary_index->key_def);

	key_def_secondary_to_primary =
		key_def_build_secondary_to_primary(primary_index->key_def, key_def);

	/**
	 * key_def_vinyl is a merged key_def of this index and key_def
	 * of the primary index, in which parts field number are
	 * renumbered.
	 *
	 * For instance:
	 * - merged primary and secondary: 3 (str), 6 (uint), 4 (scalar)
	 * - key_def_vinyl:                0 (str), 1 (uint), 2 (scalar)
	 *
	 * Condensing is necessary since partial tuple consists only
	 * from primary secondary key fields, coalesced.
	 */
	struct key_def *key_def_vinyl;
	key_def_vinyl = key_def_build_secondary(primary_index->key_def, key_def);
	/* The engine makes a copy of the key. */
	auto guard = make_scoped_guard([=]{key_def_delete(key_def_vinyl);});
	/* Create a vinyl index. */
	db = vy_index_new(env, key_def_vinyl, key_def, key_def_tuple_to_key,
			  space_by_id(key_def->iid));
	if (db == NULL || vy_index_open(db))
		diag_raise();
}

VinylSecondaryIndex::~VinylSecondaryIndex()
{
	if (key_def_tuple_to_key)
		key_def_delete(key_def_tuple_to_key);
	if (key_def_secondary_to_primary)
		key_def_delete(key_def_secondary_to_primary);
}

struct tuple *
VinylIndex::iterator_next(struct vy_tx *tx, struct vinyl_iterator *it) const
{
	(void) tx;
	struct tuple *tuple;
	uint32_t it_sc_version = ::sc_version;
	if (it_sc_version != ::sc_version)
		goto close;

	/* found */
	if (vy_cursor_next(it->cursor, &tuple) != 0)
		diag_raise();
	if (tuple != NULL)
		return tuple;
close:
	/* immediately close the cursor */
	vy_cursor_delete(it->cursor);
	it->cursor = NULL;
	it->base.next = NULL;
	return NULL;
}

struct tuple *
VinylSecondaryIndex::iterator_next(struct vy_tx *tx,
				   struct vinyl_iterator *it) const
{
	struct tuple *tuple = VinylIndex::iterator_next(tx, it);
	if (tuple)
		return lookup_full_tuple(this, tuple, tx);
	return NULL;
}

void
vinyl_iterator_free(struct iterator *ptr)
{
	assert(ptr->free == vinyl_iterator_free);
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	if (it->cursor) {
		vy_cursor_delete(it->cursor);
		it->cursor = NULL;
	}
	free(ptr);
}

struct tuple *
vinyl_iterator_last(MAYBE_UNUSED struct iterator *ptr)
{
	return NULL;
}

struct tuple *
vinyl_iterator_next(struct iterator *ptr)
{
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	struct vy_tx *tx;
	if (vy_cursor_tx(it->cursor, &tx))
		diag_raise();
	return it->index->iterator_next(tx, it);
}

struct iterator*
VinylIndex::allocIterator() const
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

void
VinylIndex::initIterator(struct iterator *ptr,
                         enum iterator_type type,
                         const char *key, uint32_t part_count) const
{
	assert(part_count == 0 || key != NULL);
	struct vinyl_iterator *it = (struct vinyl_iterator *) ptr;
	struct vy_tx *tx =
		in_txn() ? (struct vy_tx *) in_txn()->engine_tx : NULL;
	assert(it->cursor == NULL);
	it->index = this;
	it->key_def = vy_index_key_def(db);
	it->key = key;
	it->part_count = part_count;

	ptr->next = vinyl_iterator_next;
	if (type > ITER_GT || type < 0)
		return Index::initIterator(ptr, type, key, part_count);

	it->cursor = vy_cursor_new(tx, db, key, part_count, type);
	if (it->cursor == NULL)
		diag_raise();
}
