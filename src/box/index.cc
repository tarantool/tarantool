/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "index.h"
#include "tuple.h"
#include "say.h"
#include "schema.h"
#include "user_def.h"
#include "space.h"
#include "iproto_constants.h"
#include "request.h"
#include "txn.h"
#include "rmean.h"

const char *iterator_type_strs[] = {
	/* [ITER_EQ]  = */ "EQ",
	/* [ITER_REQ]  = */ "REQ",
	/* [ITER_ALL] = */ "ALL",
	/* [ITER_LT]  = */ "LT",
	/* [ITER_LE]  = */ "LE",
	/* [ITER_GE]  = */ "GE",
	/* [ITER_GT]  = */ "GT",
	/* [ITER_BITS_ALL_SET] = */ "BITS_ALL_SET",
	/* [ITER_BITS_ANY_SET] = */ "BITS_ANY_SET",
	/* [ITER_BITS_ALL_NOT_SET] = */ "BITS_ALL_NOT_SET",
	/* [ITER_OVERLAPS] = */ "OVERLAPS",
	/* [ITER_NEIGHBOR] = */ "NEIGHBOR",
};

static_assert(sizeof(iterator_type_strs) / sizeof(const char *) ==
	iterator_type_MAX, "iterator_type_str constants");

/* {{{ Utilities. **********************************************/

void
key_validate_parts(struct key_def *key_def, const char *key,
		   uint32_t part_count)
{
	(void) key_def;
	(void) key;

	for (uint32_t part = 0; part < part_count; part++) {
		enum mp_type mp_type = mp_typeof(*key);
		mp_next(&key);

		key_mp_type_validate(key_def->parts[part].type, mp_type,
				     ER_KEY_PART_TYPE, part);
	}
}

void
key_validate(struct key_def *key_def, enum iterator_type type, const char *key,
	     uint32_t part_count)
{
	assert(key != NULL || part_count == 0);
	if (part_count == 0) {
		/*
		 * Zero key parts are allowed:
		 * - for TREE index, all iterator types,
		 * - ITER_ALL iterator type, all index types
		 * - ITER_GT iterator in HASH index (legacy)
		 */
		if (key_def->type == TREE || type == ITER_ALL ||
		    (key_def->type == HASH && type == ITER_GT))
			return;
		/* Fall through. */
	}

	if (key_def->type == RTREE) {
		unsigned d = key_def->opts.dimension;
		if (part_count != 1 && part_count != d && part_count != d * 2)
			tnt_raise(ClientError, ER_KEY_PART_COUNT,
				  d  * 2, part_count);
		if (part_count == 1) {
			enum mp_type mp_type = mp_typeof(*key);
			key_mp_type_validate(ARRAY, mp_type, ER_KEY_PART_TYPE, 0);
			uint32_t array_size = mp_decode_array(&key);
			if (array_size != d && array_size != d * 2)
				tnt_raise(ClientError, ER_RTREE_RECT,
					  "Key", d, d * 2);
			for (uint32_t part = 0; part < array_size; part++) {
				enum mp_type mp_type = mp_typeof(*key);
				mp_next(&key);
				key_mp_type_validate(NUMBER, mp_type, ER_KEY_PART_TYPE, 0);
			}
		} else {
			for (uint32_t part = 0; part < part_count; part++) {
				enum mp_type mp_type = mp_typeof(*key);
				mp_next(&key);
				key_mp_type_validate(NUMBER, mp_type, ER_KEY_PART_TYPE, part);
			}
		}
	} else {
		if (part_count > key_def->part_count)
			tnt_raise(ClientError, ER_KEY_PART_COUNT,
				  key_def->part_count, part_count);

		/* Partial keys are allowed only for TREE index type. */
		if (key_def->type != TREE && part_count < key_def->part_count) {
			tnt_raise(ClientError, ER_EXACT_MATCH,
				  key_def->part_count, part_count);
		}
		key_validate_parts(key_def, key, part_count);
	}
}

void
primary_key_validate(struct key_def *key_def, const char *key,
		     uint32_t part_count)
{
	assert(key != NULL || part_count == 0);
	if (key_def->part_count != part_count) {
		tnt_raise(ClientError, ER_EXACT_MATCH,
			  key_def->part_count, part_count);
	}
	key_validate_parts(key_def, key, part_count);
}

/* }}} */

/* {{{ Index -- base class for all indexes. ********************/

Index::Index(struct key_def *key_def_arg)
	:key_def(key_def_dup(key_def_arg)),
	sc_version(::sc_version)
{}

Index::~Index()
{
	key_def_delete(key_def);
}

size_t
Index::size() const
{
	tnt_raise(ClientError, ER_UNSUPPORTED,
	          index_type_strs[key_def->type],
	          "size()");
}

struct tuple *
Index::min(const char* /* key */, uint32_t /* part_count */) const
{
	tnt_raise(ClientError, ER_UNSUPPORTED,
		  index_type_strs[key_def->type],
		  "min()");
	return NULL;
}

struct tuple *
Index::max(const char* /* key */, uint32_t /* part_count */) const
{
	tnt_raise(ClientError, ER_UNSUPPORTED,
		  index_type_strs[key_def->type],
		  "max()");
	return NULL;
}

struct tuple *
Index::random(uint32_t rnd) const
{
	(void) rnd;
	tnt_raise(ClientError, ER_UNSUPPORTED,
		  index_type_strs[key_def->type],
		  "random()");
	return NULL;
}

size_t
Index::count(enum iterator_type /* type */, const char* /* key */,
             uint32_t /* part_count */) const
{
	tnt_raise(ClientError, ER_UNSUPPORTED,
		  index_type_strs[key_def->type],
		  "count()");
	return 0;
}

struct tuple *
Index::findByTuple(struct tuple *tuple) const
{
	(void) tuple;
	tnt_raise(ClientError, ER_UNSUPPORTED,
		  index_type_strs[key_def->type],
		  "findByTuple()");
	return NULL;
}

size_t
Index::bsize() const
{
	return 0;
}

/**
 * Create a read view for iterator so further index modifications
 * will not affect the iterator iteration.
 */
void
Index::createReadViewForIterator(struct iterator *iterator)
{
	(void) iterator;
	tnt_raise(ClientError, ER_UNSUPPORTED,
		  index_type_strs[key_def->type],
		  "consistent read view");
}

/**
 * Destroy a read view of an iterator. Must be called for iterators,
 * for which createReadViewForIterator was called.
 */
void
Index::destroyReadViewForIterator(struct iterator *iterator)
{
	(void) iterator;
	tnt_raise(ClientError, ER_UNSUPPORTED,
		  index_type_strs[key_def->type],
		  "consistent read view");
}

static inline Index *
check_index(uint32_t space_id, uint32_t index_id, struct space **space)
{
	*space = space_cache_find(space_id);
	access_check_space(*space, PRIV_R);
	return index_find(*space, index_id);
}

static inline box_tuple_t *
tuple_bless_null(struct tuple *tuple)
{
	if (tuple != NULL)
		return tuple_bless(tuple);
	return NULL;
}

ssize_t
box_index_len(uint32_t space_id, uint32_t index_id)
{
	try {
		struct space *space;
		/* no tx management, len is approximate in sophia anyway. */
		return check_index(space_id, index_id, &space)->size();
	} catch (Exception *) {
		return (size_t) -1; /* handled by box.error() in Lua */
	}
}

ssize_t
box_index_bsize(uint32_t space_id, uint32_t index_id)
{
       try {
	       /* no tx management for statistics */
	       struct space *space;
	       return check_index(space_id, index_id, &space)->bsize();
       } catch (Exception *) {
               return (size_t) -1; /* handled by box.error() in Lua */
       }
}

int
box_index_random(uint32_t space_id, uint32_t index_id, uint32_t rnd,
		box_tuple_t **result)
{
	assert(result != NULL);
	try {
		struct space *space;
		/* no tx management, random() is for approximation anyway */
		Index *index = check_index(space_id, index_id, &space);
		struct tuple *tuple = index->random(rnd);
		*result = tuple_bless_null(tuple);
		return 0;
	}  catch (Exception *) {
		return -1;
	}
}

int
box_index_get(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	assert(result != NULL);
	try {
		struct space *space;
		Index *index = check_index(space_id, index_id, &space);
		if (!index->key_def->opts.is_unique)
			tnt_raise(ClientError, ER_MORE_THAN_ONE_TUPLE);
		uint32_t part_count = key ? mp_decode_array(&key) : 0;
		primary_key_validate(index->key_def, key, part_count);
		/* Start transaction in the engine. */
		struct txn *txn = txn_begin_ro_stmt(space);
		struct tuple *tuple = index->findByKey(key, part_count);
		/* Count statistics */
		rmean_collect(rmean_box, IPROTO_SELECT, 1);

		*result = tuple_bless_null(tuple);
		txn_commit_ro_stmt(txn);
		return 0;
	}  catch (Exception *) {
		txn_rollback_stmt();
		return -1;
	}
}

int
box_index_min(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	assert(result != NULL);
	try {
		struct space *space;
		Index *index = check_index(space_id, index_id, &space);
		if (index->key_def->type != TREE) {
			/* Show nice error messages in Lua */
			tnt_raise(ClientError, ER_UNSUPPORTED,
				  index_type_strs[index->key_def->type],
				  "min()");
		}
		uint32_t part_count = key ? mp_decode_array(&key) : 0;
		key_validate(index->key_def, ITER_GE, key, part_count);
		/* Start transaction in the engine */
		struct txn *txn = txn_begin_ro_stmt(space);
		struct tuple *tuple = index->min(key, part_count);
		*result = tuple_bless_null(tuple);
		txn_commit_ro_stmt(txn);
		return 0;
	}  catch (Exception *) {
		txn_rollback_stmt();
		return -1;
	}
}

int
box_index_max(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	assert(result != NULL);
	try {
		struct space *space;
		Index *index = check_index(space_id, index_id, &space);
		if (index->key_def->type != TREE) {
			/* Show nice error messages in Lua */
			tnt_raise(ClientError, ER_UNSUPPORTED,
				  index_type_strs[index->key_def->type],
				  "max()");
		}
		uint32_t part_count = key ? mp_decode_array(&key) : 0;
		key_validate(index->key_def, ITER_LE, key, part_count);
		/* Start transaction in the engine */
		struct txn *txn = txn_begin_ro_stmt(space);
		struct tuple *tuple = index->max(key, part_count);
		*result = tuple_bless_null(tuple);
		txn_commit_ro_stmt(txn);
		return 0;
	}  catch (Exception *) {
		txn_rollback_stmt();
		return -1;
	}
}

ssize_t
box_index_count(uint32_t space_id, uint32_t index_id, int type,
		const char *key, const char *key_end)
{
	mp_tuple_assert(key, key_end);
	enum iterator_type itype = (enum iterator_type) type;
	try {
		struct space *space;
		Index *index = check_index(space_id, index_id, &space);
		uint32_t part_count = key ? mp_decode_array(&key) : 0;
		key_validate(index->key_def, itype, key, part_count);
		/* Start transaction in the engine */
		struct txn *txn = txn_begin_ro_stmt(space);
		ssize_t count = index->count(itype, key, part_count);
		txn_commit_ro_stmt(txn);
		return count;
	} catch (Exception *) {
		txn_rollback_stmt();
		return -1; /* handled by box.error() in Lua */
	}
}

/* }}} */

/* {{{ Iterators ************************************************/

box_iterator_t *
box_index_iterator(uint32_t space_id, uint32_t index_id, int type,
                   const char *key, const char *key_end)
{
	mp_tuple_assert(key, key_end);
	struct iterator *it = NULL;
	enum iterator_type itype = (enum iterator_type) type;
	try {
		struct space *space;
		Index *index = check_index(space_id, index_id, &space);
		assert(mp_typeof(*key) == MP_ARRAY); /* checked by Lua */
		uint32_t part_count = mp_decode_array(&key);
		key_validate(index->key_def, itype, key, part_count);
		it = index->allocIterator();
		index->initIterator(it, itype, key, part_count);
		it->sc_version = sc_version;
		it->space_id = space_id;
		it->index_id = index_id;
		it->index = index;
		/*
		 * No transaction management: iterators are
		 * "dirty" in tarantool now, they exist in
		 * their own read view in sophia or access dirty
		 * data in memtx.
		 */
		return it;
	} catch (Exception *) {
		if (it)
			it->free(it);
		/* will be hanled by box.error() in Lua */
		return NULL;
	}
}

int
box_iterator_next(box_iterator_t *itr, box_tuple_t **result)
{
	assert(result != NULL);
	try {
		if (itr->sc_version != sc_version) {
			struct space *space;
			/* no tx management */
			Index *index = check_index(itr->space_id, itr->index_id,
						   &space);
			if (index != itr->index) {
				*result = NULL;
				return 0;
			}
			if (index->sc_version > itr->sc_version) {
				*result = NULL; /* invalidate iterator */
				return 0;
			}
			itr->sc_version = sc_version;
		}
	} catch (Exception *) {
		*result = NULL;
		return 0; /* invalidate iterator */
	}
	try {
		struct tuple *tuple = itr->next(itr);
		*result = tuple_bless_null(tuple);
		return 0;
	} catch (Exception *) {
		return -1;
	}
}

void
box_iterator_free(box_iterator_t *it)
{
	if (it->free)
		it->free(it);
}

/* }}} */
