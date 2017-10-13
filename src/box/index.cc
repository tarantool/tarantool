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
#include "index.h"
#include "tuple.h"
#include "say.h"
#include "schema.h"
#include "user_def.h"
#include "space.h"
#include "iproto_constants.h"
#include "txn.h"
#include "rmean.h"
#include "info.h"

/* {{{ Utilities. **********************************************/

UnsupportedIndexFeature::UnsupportedIndexFeature(const char *file,
	unsigned line, struct index_def *index_def, const char *what)
	: ClientError(file, line, ER_UNKNOWN)
{
	struct space *space = space_cache_find_xc(index_def->space_id);
	m_errcode = ER_UNSUPPORTED_INDEX_FEATURE;
	error_format_msg(this, tnt_errcode_desc(m_errcode), index_def->name,
			 index_type_strs[index_def->type],
			 space->def->name, space->def->engine_name, what);
}

struct error *
BuildUnsupportedIndexFeature(const char *file, unsigned line,
			     struct index_def *index_def, const char *what)
{
	try {
		return new UnsupportedIndexFeature(file, line, index_def, what);
	} catch (OutOfMemory *e) {
		return e;
	}
}

int
key_validate(const struct index_def *index_def, enum iterator_type type,
	     const char *key, uint32_t part_count)
{
	assert(key != NULL || part_count == 0);
	if (part_count == 0) {
		/*
		 * Zero key parts are allowed:
		 * - for TREE index, all iterator types,
		 * - ITER_ALL iterator type, all index types
		 * - ITER_GT iterator in HASH index (legacy)
		 */
		if (index_def->type == TREE || type == ITER_ALL ||
		    (index_def->type == HASH && type == ITER_GT))
			return 0;
		/* Fall through. */
	}

	if (index_def->type == RTREE) {
		unsigned d = index_def->opts.dimension;
		if (part_count != 1 && part_count != d && part_count != d * 2) {
			diag_set(ClientError, ER_KEY_PART_COUNT, d  * 2,
				 part_count);
			return -1;
		}
		if (part_count == 1) {
			enum mp_type mp_type = mp_typeof(*key);
			if (key_mp_type_validate(FIELD_TYPE_ARRAY, mp_type,
						 ER_KEY_PART_TYPE, 0, false))
				return -1;
			uint32_t array_size = mp_decode_array(&key);
			if (array_size != d && array_size != d * 2) {
				diag_set(ClientError, ER_RTREE_RECT, "Key", d,
					 d * 2);
				return -1;
			}
			for (uint32_t part = 0; part < array_size; part++) {
				enum mp_type mp_type = mp_typeof(*key);
				mp_next(&key);
				if (key_mp_type_validate(FIELD_TYPE_NUMBER,
							 mp_type,
							 ER_KEY_PART_TYPE, 0,
							 false))
					return -1;
			}
		} else {
			for (uint32_t part = 0; part < part_count; part++) {
				enum mp_type mp_type = mp_typeof(*key);
				mp_next(&key);
				if (key_mp_type_validate(FIELD_TYPE_NUMBER,
							 mp_type,
							 ER_KEY_PART_TYPE,
							 part, false))
					return -1;
			}
		}
	} else {
		if (part_count > index_def->key_def->part_count) {
			diag_set(ClientError, ER_KEY_PART_COUNT,
				 index_def->key_def->part_count, part_count);
			return -1;
		}

		/* Partial keys are allowed only for TREE index type. */
		if (index_def->type != TREE && part_count < index_def->key_def->part_count) {
			diag_set(ClientError, ER_PARTIAL_KEY,
				 index_type_strs[index_def->type],
				 index_def->key_def->part_count,
				 part_count);
			return -1;
		}
		if (key_validate_parts(index_def->key_def, key,
				       part_count, true) != 0)
			return -1;
	}
	return 0;
}

int
exact_key_validate(struct key_def *key_def, const char *key,
		   uint32_t part_count)
{
	assert(key != NULL || part_count == 0);
	if (key_def->part_count != part_count) {
		diag_set(ClientError, ER_EXACT_MATCH, key_def->part_count,
			 part_count);
		return -1;
	}
	return key_validate_parts(key_def, key, part_count, false);
}

char *
box_tuple_extract_key(const box_tuple_t *tuple, uint32_t space_id,
	uint32_t index_id, uint32_t *key_size)
{
	try {
		struct space *space = space_by_id(space_id);
		struct index *index = index_find_xc(space, index_id);
		return tuple_extract_key(tuple, index->def->key_def, key_size);
	} catch (ClientError *e) {
		return NULL;
	}
}

static inline struct index *
check_index(uint32_t space_id, uint32_t index_id, struct space **space)
{
	*space = space_cache_find_xc(space_id);
	access_check_space(*space, PRIV_R);
	return index_find_xc(*space, index_id);
}

static inline box_tuple_t *
tuple_bless_null_xc(struct tuple *tuple)
{
	if (tuple != NULL)
		return tuple_bless_xc(tuple);
	return NULL;
}

/* }}} */

/* {{{ Public API */

ssize_t
box_index_len(uint32_t space_id, uint32_t index_id)
{
	try {
		struct space *space;
		struct index *index = check_index(space_id, index_id, &space);
		/* no tx management, len is approximate in vinyl anyway. */
		return index_size_xc(index);
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
	       struct index *index = check_index(space_id, index_id, &space);
	       return index_bsize_xc(index);
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
		struct index *index = check_index(space_id, index_id, &space);
		struct tuple *tuple = index_random_xc(index, rnd);
		*result = tuple_bless_null_xc(tuple);
		return 0;
	}  catch (Exception *) {
		return -1;
	}
}

int
box_index_get(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result)
{
	assert(key != NULL && key_end != NULL && result != NULL);
	mp_tuple_assert(key, key_end);
	try {
		struct space *space;
		struct index *index = check_index(space_id, index_id, &space);
		if (!index->def->opts.is_unique)
			tnt_raise(ClientError, ER_MORE_THAN_ONE_TUPLE);
		uint32_t part_count = mp_decode_array(&key);
		if (exact_key_validate(index->def->key_def, key, part_count))
			diag_raise();
		/* Start transaction in the engine. */
		struct txn *txn = txn_begin_ro_stmt(space);
		struct tuple *tuple = index_get_xc(index, key, part_count);
		/* Count statistics */
		rmean_collect(rmean_box, IPROTO_SELECT, 1);

		*result = tuple_bless_null_xc(tuple);
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
	assert(key != NULL && key_end != NULL && result != NULL);
	mp_tuple_assert(key, key_end);
	try {
		struct space *space;
		struct index *index = check_index(space_id, index_id, &space);
		if (index->def->type != TREE) {
			/* Show nice error messages in Lua */
			tnt_raise(UnsupportedIndexFeature, index->def, "min()");
		}
		uint32_t part_count = mp_decode_array(&key);
		if (key_validate(index->def, ITER_GE, key, part_count))
			diag_raise();
		/* Start transaction in the engine */
		struct txn *txn = txn_begin_ro_stmt(space);
		struct tuple *tuple = index_min_xc(index, key, part_count);
		*result = tuple_bless_null_xc(tuple);
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
		struct index *index = check_index(space_id, index_id, &space);
		if (index->def->type != TREE) {
			/* Show nice error messages in Lua */
			tnt_raise(UnsupportedIndexFeature, index->def, "max()");
		}
		uint32_t part_count = mp_decode_array(&key);
		if (key_validate(index->def, ITER_LE, key, part_count))
			diag_raise();
		/* Start transaction in the engine */
		struct txn *txn = txn_begin_ro_stmt(space);
		struct tuple *tuple = index_max_xc(index, key, part_count);
		*result = tuple_bless_null_xc(tuple);
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
	assert(key != NULL && key_end != NULL);
	mp_tuple_assert(key, key_end);
	enum iterator_type itype = (enum iterator_type) type;
	try {
		struct space *space;
		struct index *index = check_index(space_id, index_id, &space);
		uint32_t part_count = mp_decode_array(&key);
		if (key_validate(index->def, itype, key, part_count))
			diag_raise();
		/* Start transaction in the engine */
		struct txn *txn = txn_begin_ro_stmt(space);
		ssize_t count = index_count_xc(index, itype, key, part_count);
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
	assert(key != NULL && key_end != NULL);
	mp_tuple_assert(key, key_end);
	struct iterator *it = NULL;
	enum iterator_type itype = (enum iterator_type) type;
	try {
		struct space *space;
		struct index *index = check_index(space_id, index_id, &space);
		struct txn *txn = txn_begin_ro_stmt(space);
		assert(mp_typeof(*key) == MP_ARRAY); /* checked by Lua */
		uint32_t part_count = mp_decode_array(&key);
		if (key_validate(index->def, itype, key, part_count))
			diag_raise();
		it = index_alloc_iterator_xc(index);
		index_init_iterator_xc(index, it, itype, key, part_count);
		it->schema_version = schema_version;
		it->space_id = space_id;
		it->index_id = index_id;
		it->index = index;
		txn_commit_ro_stmt(txn);
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
	assert(itr->next != NULL);
	try {
		if (itr->schema_version != schema_version) {
			struct space *space;
			/* no tx management */
			struct index *index = check_index(itr->space_id,
						itr->index_id, &space);
			if (index != itr->index) {
				*result = NULL;
				return 0;
			}
			if (index->schema_version > itr->schema_version) {
				*result = NULL; /* invalidate iterator */
				return 0;
			}
			itr->schema_version = schema_version;
		}
	} catch (Exception *) {
		*result = NULL;
		return 0; /* invalidate iterator */
	}
	try {
		struct tuple *tuple = iterator_next_xc(itr);
		*result = tuple_bless_null_xc(tuple);
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

/* {{{ Introspection */

int
box_index_info(uint32_t space_id, uint32_t index_id,
	       struct info_handler *info)
{
	try {
		struct space *space;
		struct index *index = check_index(space_id, index_id, &space);
		index_info(index, info);
		return 0;
	}  catch (Exception *) {
		return -1;
	}
}

/* }}} */

/* {{{ Internal API */

int
index_create(struct index *index, const struct index_vtab *vtab,
	     struct index_def *def)
{
	def = index_def_dup(def);
	if (def == NULL)
		return -1;

	index->vtab = vtab;
	index->def = def;
	index->schema_version = schema_version;
	index->position = NULL;
	return 0;
}

void
index_delete(struct index *index)
{
	if (index->position != NULL)
		index->position->free(index->position);
	index_def_delete(index->def);
	index->vtab->destroy(index);
}

int
index_build(struct index *index, struct index *pk)
{
	ssize_t n_tuples = index_size(pk);
	if (n_tuples < 0)
		return -1;
	uint32_t estimated_tuples = n_tuples * 1.2;

	index_begin_build(index);
	if (index_reserve(index, estimated_tuples) < 0)
		return -1;

	if (n_tuples > 0) {
		say_info("Adding %zd keys to %s index '%s' ...",
			 n_tuples, index_type_strs[index->def->type],
			 index->def->name);
	}

	struct iterator *it = index_position(pk);
	if (it == NULL)
		return -1;
	if (index_init_iterator(pk, it, ITER_ALL, NULL, 0) != 0)
		return -1;

	while (true) {
		struct tuple *tuple;
		if (it->next(it, &tuple) != 0)
			return -1;
		if (tuple == NULL)
			break;
		if (index_build_next(index, tuple) != 0)
			return -1;
	}

	index_end_build(index);
	return 0;
}

/* }}} */

/* {{{ Virtual method stubs */

void
generic_index_commit_create(struct index *, int64_t)
{
}

void
generic_index_commit_drop(struct index *)
{
}

ssize_t
generic_index_size(struct index *index)
{
	diag_set(UnsupportedIndexFeature, index->def, "size()");
	return -1;
}

int
generic_index_min(struct index *index, const char *key,
		  uint32_t part_count, struct tuple **result)
{
	(void)key;
	(void)part_count;
	(void)result;
	diag_set(UnsupportedIndexFeature, index->def, "min()");
	return -1;
}

int
generic_index_max(struct index *index, const char *key,
		  uint32_t part_count, struct tuple **result)
{
	(void)key;
	(void)part_count;
	(void)result;
	diag_set(UnsupportedIndexFeature, index->def, "max()");
	return -1;
}

int
generic_index_random(struct index *index, uint32_t rnd, struct tuple **result)
{
	(void)rnd;
	(void)result;
	diag_set(UnsupportedIndexFeature, index->def, "random()");
	return -1;
}

ssize_t
generic_index_count(struct index *index, enum iterator_type type,
		    const char *key, uint32_t part_count)
{
	(void)type;
	(void)key;
	(void)part_count;
	diag_set(UnsupportedIndexFeature, index->def, "count()");
	return -1;
}

int
generic_index_get(struct index *index, const char *key,
		  uint32_t part_count, struct tuple **result)
{
	(void)key;
	(void)part_count;
	(void)result;
	diag_set(UnsupportedIndexFeature, index->def, "get()");
	return -1;
}

int
generic_index_replace(struct index *index, struct tuple *old_tuple,
		      struct tuple *new_tuple, enum dup_replace_mode mode,
		      struct tuple **result)
{
	(void)old_tuple;
	(void)new_tuple;
	(void)mode;
	(void)result;
	diag_set(UnsupportedIndexFeature, index->def, "replace()");
	return -1;
}

struct snapshot_iterator *
generic_index_create_snapshot_iterator(struct index *index)
{
	diag_set(UnsupportedIndexFeature, index->def, "consistent read view");
	return NULL;
}

void
generic_index_info(struct index *index, struct info_handler *handler)
{
	(void)index;
	info_begin(handler);
	info_end(handler);
}

void
generic_index_begin_build(struct index *)
{
}

int
generic_index_reserve(struct index *, uint32_t)
{
	return 0;
}

int
generic_index_build_next(struct index *index, struct tuple *tuple)
{
	struct tuple *unused;
	return index_replace(index, NULL, tuple, DUP_INSERT, &unused);
}

void
generic_index_end_build(struct index *)
{
}

/* }}} */
