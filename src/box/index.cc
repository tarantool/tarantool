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
	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return NULL;
	struct index *index = index_find(space, index_id);
	if (index == NULL)
		return NULL;
	return tuple_extract_key(tuple, index->def->key_def, key_size);
}

static inline int
check_index(uint32_t space_id, uint32_t index_id,
	    struct space **space, struct index **index)
{
	*space = space_cache_find(space_id);
	if (*space == NULL)
		return -1;
	if (access_check_space(*space, PRIV_R) != 0)
		return -1;
	*index = index_find(*space, index_id);
	if (*index == NULL)
		return -1;
	return 0;
}

/* }}} */

/* {{{ Public API */

const box_key_def_t *
box_iterator_key_def(box_iterator_t *iterator)
{
	return iterator->index->def->key_def;
}

ssize_t
box_index_len(uint32_t space_id, uint32_t index_id)
{
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	/* No tx management, len() doesn't work in vinyl anyway. */
	return index_size(index);
}

ssize_t
box_index_bsize(uint32_t space_id, uint32_t index_id)
{
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	/* No tx management for statistics. */
	return index_bsize(index);
}

int
box_index_random(uint32_t space_id, uint32_t index_id, uint32_t rnd,
		box_tuple_t **result)
{
	assert(result != NULL);
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	/* No tx management, random() is for approximation anyway. */
	if (index_random(index, rnd, result) != 0)
		return -1;
	if (*result != NULL && tuple_bless(*result) == NULL)
		return -1;
	return 0;
}

int
box_index_get(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result)
{
	assert(key != NULL && key_end != NULL && result != NULL);
	mp_tuple_assert(key, key_end);
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	if (!index->def->opts.is_unique) {
		diag_set(ClientError, ER_MORE_THAN_ONE_TUPLE);
		return -1;
	}
	uint32_t part_count = mp_decode_array(&key);
	if (exact_key_validate(index->def->key_def, key, part_count))
		return -1;
	/* Start transaction in the engine. */
	struct txn *txn;
	if (txn_begin_ro_stmt(space, &txn) != 0)
		return -1;
	if (index_get(index, key, part_count, result) != 0) {
		txn_rollback_stmt();
		return -1;
	}
	txn_commit_ro_stmt(txn);
	/* Count statistics. */
	rmean_collect(rmean_box, IPROTO_SELECT, 1);
	if (*result != NULL && tuple_bless(*result) == NULL)
		return -1;
	return 0;
}

int
box_index_min(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result)
{
	assert(key != NULL && key_end != NULL && result != NULL);
	mp_tuple_assert(key, key_end);
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	if (index->def->type != TREE) {
		/* Show nice error messages in Lua. */
		diag_set(UnsupportedIndexFeature, index->def, "min()");
		return -1;
	}
	uint32_t part_count = mp_decode_array(&key);
	if (key_validate(index->def, ITER_GE, key, part_count))
		return -1;
	/* Start transaction in the engine. */
	struct txn *txn;
	if (txn_begin_ro_stmt(space, &txn) != 0)
		return -1;
	if (index_min(index, key, part_count, result) != 0) {
		txn_rollback_stmt();
		return -1;
	}
	txn_commit_ro_stmt(txn);
	if (*result != NULL && tuple_bless(*result) == NULL)
		return -1;
	return 0;
}

int
box_index_max(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	assert(result != NULL);
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	if (index->def->type != TREE) {
		/* Show nice error messages in Lua. */
		diag_set(UnsupportedIndexFeature, index->def, "max()");
		return -1;
	}
	uint32_t part_count = mp_decode_array(&key);
	if (key_validate(index->def, ITER_LE, key, part_count))
		return -1;
	/* Start transaction in the engine. */
	struct txn *txn;
	if (txn_begin_ro_stmt(space, &txn) != 0)
		return -1;
	if (index_max(index, key, part_count, result) != 0) {
		txn_rollback_stmt();
		return -1;
	}
	txn_commit_ro_stmt(txn);
	if (*result != NULL && tuple_bless(*result) == NULL)
		return -1;
	return 0;
}

ssize_t
box_index_count(uint32_t space_id, uint32_t index_id, int type,
		const char *key, const char *key_end)
{
	assert(key != NULL && key_end != NULL);
	mp_tuple_assert(key, key_end);
	if (type < 0 || type >= iterator_type_MAX) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "Invalid iterator type");
		return -1;
	}
	enum iterator_type itype = (enum iterator_type) type;
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	uint32_t part_count = mp_decode_array(&key);
	if (key_validate(index->def, itype, key, part_count))
		return -1;
	/* Start transaction in the engine. */
	struct txn *txn;
	if (txn_begin_ro_stmt(space, &txn) != 0)
		return -1;
	ssize_t count = index_count(index, itype, key, part_count);
	if (count < 0) {
		txn_rollback_stmt();
		return -1;
	}
	txn_commit_ro_stmt(txn);
	return count;
}

/* }}} */

/* {{{ Iterators ************************************************/

box_iterator_t *
box_index_iterator(uint32_t space_id, uint32_t index_id, int type,
                   const char *key, const char *key_end)
{
	assert(key != NULL && key_end != NULL);
	mp_tuple_assert(key, key_end);
	if (type < 0 || type >= iterator_type_MAX) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "Invalid iterator type");
		return NULL;
	}
	enum iterator_type itype = (enum iterator_type) type;
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return NULL;
	assert(mp_typeof(*key) == MP_ARRAY); /* checked by Lua */
	uint32_t part_count = mp_decode_array(&key);
	if (key_validate(index->def, itype, key, part_count))
		return NULL;
	struct txn *txn;
	if (txn_begin_ro_stmt(space, &txn) != 0)
		return NULL;
	struct iterator *it = index_create_iterator(index, itype,
						    key, part_count);
	if (it == NULL) {
		txn_rollback_stmt();
		return NULL;
	}
	txn_commit_ro_stmt(txn);
	return it;
}

int
box_iterator_next(box_iterator_t *itr, box_tuple_t **result)
{
	assert(result != NULL);
	if (iterator_next(itr, result) != 0)
		return -1;
	if (*result != NULL && tuple_bless(*result) == NULL)
		return -1;
	return 0;
}

void
box_iterator_free(box_iterator_t *it)
{
	iterator_delete(it);
}

/* }}} */

/* {{{ Introspection */

int
box_index_info(uint32_t space_id, uint32_t index_id,
	       struct info_handler *info)
{
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	index_info(index, info);
	return 0;
}

/* }}} */

/* {{{ Internal API */

void
iterator_create(struct iterator *it, struct index *index)
{
	it->next = NULL;
	it->free = NULL;
	it->schema_version = schema_version;
	it->space_id = index->def->space_id;
	it->index_id = index->def->iid;
	it->index = index;
}

int
iterator_next(struct iterator *it, struct tuple **ret)
{
	assert(it->next != NULL);
	/* In case of ephemeral space there is no need to check schema version */
	if (it->space_id == 0)
		return it->next(it, ret);
	if (unlikely(it->schema_version != schema_version)) {
		struct space *space = space_by_id(it->space_id);
		if (space == NULL)
			goto invalidate;
		struct index *index = space_index(space, it->index_id);
		if (index != it->index ||
		    index->schema_version > it->schema_version)
			goto invalidate;
		it->schema_version = schema_version;
	}
	return it->next(it, ret);

invalidate:
	*ret = NULL;
	return 0;
}

void
iterator_delete(struct iterator *it)
{
	assert(it->free != NULL);
	it->free(it);
}

int
index_create(struct index *index, struct engine *engine,
	     const struct index_vtab *vtab, struct index_def *def)
{
	def = index_def_dup(def);
	if (def == NULL)
		return -1;

	index->vtab = vtab;
	index->engine = engine;
	index->def = def;
	index->schema_version = schema_version;
	return 0;
}

void
index_delete(struct index *index)
{
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

	struct iterator *it = index_create_iterator(pk, ITER_ALL, NULL, 0);
	if (it == NULL)
		return -1;

	int rc = 0;
	while (true) {
		struct tuple *tuple;
		rc = iterator_next(it, &tuple);
		if (rc != 0)
			break;
		if (tuple == NULL)
			break;
		rc = index_build_next(index, tuple);
		if (rc != 0)
			break;
	}
	iterator_delete(it);
	if (rc != 0)
		return -1;

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
generic_index_abort_create(struct index *)
{
}

void
generic_index_commit_modify(struct index *, int64_t)
{
}

void
generic_index_commit_drop(struct index *)
{
}

void
generic_index_update_def(struct index *)
{
}

bool generic_index_depends_on_pk(struct index *)
{
	return false;
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
	struct iterator *it = index_create_iterator(index, ITER_EQ,
						    key, part_count);
	if (it == NULL)
		return -1;
	int rc = iterator_next(it, result);
	iterator_delete(it);
	return rc;
}

int
generic_index_max(struct index *index, const char *key,
		  uint32_t part_count, struct tuple **result)
{
	struct iterator *it = index_create_iterator(index, ITER_REQ,
						    key, part_count);
	if (it == NULL)
		return -1;
	int rc = iterator_next(it, result);
	iterator_delete(it);
	return rc;
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
	struct iterator *it = index_create_iterator(index, type,
						    key, part_count);
	if (it == NULL)
		return -1;
	int rc = 0;
	size_t count = 0;
	struct tuple *tuple = NULL;
	while ((rc = iterator_next(it, &tuple)) == 0 && tuple != NULL)
		++count;
	iterator_delete(it);
	if (rc < 0)
		return rc;
	return count;
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
generic_index_reset_stat(struct index *index)
{
	(void)index;
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
