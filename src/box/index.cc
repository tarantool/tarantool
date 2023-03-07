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
#include "result.h"
#include "iproto_constants.h"
#include "txn.h"
#include "rmean.h"
#include "info/info.h"
#include "memtx_tx.h"
#include "box.h"
#include "base64.h"
#include "scoped_guard.h"

struct rlist box_on_select = RLIST_HEAD_INITIALIZER(box_on_select);

/* {{{ Utilities. **********************************************/

UnsupportedIndexFeature::UnsupportedIndexFeature(const char *file,
	unsigned line, struct index_def *index_def, const char *what)
	: ClientError(file, line, ER_UNKNOWN)
{
	struct space *space = space_cache_find_xc(index_def->space_id);
	code = ER_UNSUPPORTED_INDEX_FEATURE;
	error_format_msg(this, tnt_errcode_desc(code), index_def->name,
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
			if (key_part_validate(FIELD_TYPE_ARRAY, key, 0, false))
				return -1;
			uint32_t array_size = mp_decode_array(&key);
			if (array_size != d && array_size != d * 2) {
				diag_set(ClientError, ER_RTREE_RECT, "Key", d,
					 d * 2);
				return -1;
			}
			for (uint32_t part = 0; part < array_size; part++) {
				if (key_part_validate(FIELD_TYPE_NUMBER, key,
						      0, false))
					return -1;
				mp_next(&key);
			}
		} else {
			for (uint32_t part = 0; part < part_count; part++) {
				if (key_part_validate(FIELD_TYPE_NUMBER, key,
						      part, false))
					return -1;
				mp_next(&key);
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
		const char *key_end;
		if (key_validate_parts(index_def->key_def, key,
				       part_count, true, &key_end) != 0)
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
	const char *key_end;
	return key_validate_parts(key_def, key, part_count, false, &key_end);
}

int
exact_key_validate_nullable(struct key_def *key_def, const char *key,
			    uint32_t part_count)
{
	assert(key != NULL || part_count == 0);
	if (key_def->part_count != part_count) {
		diag_set(ClientError, ER_EXACT_MATCH, key_def->part_count,
			 part_count);
		return -1;
	}
	const char *key_end;
	return key_validate_parts(key_def, key, part_count, true, &key_end);
}

char *
box_tuple_extract_key(box_tuple_t *tuple, uint32_t space_id, uint32_t index_id,
		      uint32_t *key_size)
{
	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return NULL;
	struct index *index = index_find(space, index_id);
	if (index == NULL)
		return NULL;
	return tuple_extract_key(tuple, index->def->key_def,
				 MULTIKEY_NONE, key_size);
}

static int
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

int
box_iterator_position_from_tuple(const char *tuple, const char *tuple_end,
				 struct key_def *cmp_def,
				 const char **packed_pos,
				 const char **packed_pos_end)
{
	if (cmp_def->for_func_index) {
		diag_set(ClientError, ER_UNSUPPORTED, "Functional index",
			 "position by tuple");
		return -1;
	}
	if (cmp_def->is_multikey) {
		diag_set(ClientError, ER_UNSUPPORTED, "Multikey index",
			 "position by tuple");
		return -1;
	}
	if (tuple_validate_key_parts_raw(cmp_def, tuple) != 0) {
		diag_set(ClientError, ER_ITERATOR_POSITION);
		return -1;
	}
	uint32_t key_size;
	const char *key = tuple_extract_key_raw(tuple, tuple_end, cmp_def,
						MULTIKEY_NONE, &key_size);
	if (key == NULL) {
		diag_set(ClientError, ER_ITERATOR_POSITION);
		return -1;
	}
	const char *key_end = key + key_size;
	uint32_t buf_size = iterator_position_pack_bufsize(key, key_end);
	char *buf = (char *)xregion_alloc(&fiber()->gc, buf_size);
	iterator_position_pack(key, key_end, buf, buf_size,
			       packed_pos, packed_pos_end);
	return 0;
}

int
box_index_tuple_position(uint32_t space_id, uint32_t index_id,
			 const char *tuple, const char *tuple_end,
			 const char **packed_pos, const char **packed_pos_end)
{
	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return -1;
	struct index *index = index_find(space, index_id);
	if (index == NULL)
		return -1;
	return box_iterator_position_from_tuple(tuple, tuple_end,
						index->def->cmp_def,
						packed_pos, packed_pos_end);
}

/* }}} */

/* {{{ Public API */

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
	struct result_processor res_proc;
	result_process_prepare(&res_proc, space);
	int rc = index_random(index, rnd, result);
	result_process_perform(&res_proc, &rc, result);
	if (rc != 0)
		return -1;
	if (*result != NULL)
		tuple_bless(*result);
	return 0;
}

int
box_index_get(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result)
{
	assert(key != NULL && key_end != NULL && result != NULL);
	mp_tuple_assert(key, key_end);
	if (box_check_slice() != 0)
		return -1;
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	if (!index->def->opts.is_unique) {
		diag_set(ClientError, ER_MORE_THAN_ONE_TUPLE);
		return -1;
	}
	const char *key_array = key;
	uint32_t part_count = mp_decode_array(&key);
	if (exact_key_validate(index->def->key_def, key, part_count))
		return -1;
	box_run_on_select(space, index, ITER_EQ, key_array);
	/* Start transaction in the engine. */
	struct txn *txn;
	struct txn_ro_savepoint svp;
	if (txn_begin_ro_stmt(space, &txn, &svp) != 0)
		return -1;
	struct result_processor res_proc;
	result_process_prepare(&res_proc, space);
	int rc = index_get(index, key, part_count, result);
	result_process_perform(&res_proc, &rc, result);
	txn_end_ro_stmt(txn, &svp);
	if (rc != 0)
		return -1;
	/* Count statistics. */
	rmean_collect(rmean_box, IPROTO_SELECT, 1);
	if (*result != NULL)
		tuple_bless(*result);
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
	const char *key_array = key;
	uint32_t part_count = mp_decode_array(&key);
	if (key_validate(index->def, ITER_GE, key, part_count))
		return -1;
	box_run_on_select(space, index, ITER_GE, key_array);
	/* Start transaction in the engine. */
	struct txn *txn;
	struct txn_ro_savepoint svp;
	if (txn_begin_ro_stmt(space, &txn, &svp) != 0)
		return -1;
	struct result_processor res_proc;
	result_process_prepare(&res_proc, space);
	int rc = index_min(index, key, part_count, result);
	result_process_perform(&res_proc, &rc, result);
	txn_end_ro_stmt(txn, &svp);
	if (rc != 0)
		return -1;
	if (*result != NULL)
		tuple_bless(*result);
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
	const char *key_array = key;
	uint32_t part_count = mp_decode_array(&key);
	if (key_validate(index->def, ITER_LE, key, part_count))
		return -1;
	box_run_on_select(space, index, ITER_LE, key_array);
	/* Start transaction in the engine. */
	struct txn *txn;
	struct txn_ro_savepoint svp;
	if (txn_begin_ro_stmt(space, &txn, &svp) != 0)
		return -1;
	struct result_processor res_proc;
	result_process_prepare(&res_proc, space);
	int rc = index_max(index, key, part_count, result);
	result_process_perform(&res_proc, &rc, result);
	txn_end_ro_stmt(txn, &svp);
	if (rc != 0)
		return -1;
	if (*result != NULL)
		tuple_bless(*result);
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
	struct txn_ro_savepoint svp;
	if (txn_begin_ro_stmt(space, &txn, &svp) != 0)
		return -1;
	ssize_t count = index_count(index, itype, key, part_count);
	txn_end_ro_stmt(txn, &svp);
	if (count < 0)
		return -1;
	return count;
}

/* }}} */

/* {{{ Iterators ************************************************/

box_iterator_t *
box_index_iterator_after(uint32_t space_id, uint32_t index_id, int type,
			 const char *key, const char *key_end,
			 const char *packed_pos, const char *packed_pos_end)
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
	const char *key_array = key;
	assert(mp_typeof(*key) == MP_ARRAY); /* checked by Lua */
	uint32_t part_count = mp_decode_array(&key);
	if (key_validate(index->def, itype, key, part_count))
		return NULL;
	const char *pos, *pos_end;
	char *pos_buf = NULL;
	uint32_t pos_buf_size = 0;
	uint32_t region_svp = region_used(&fiber()->gc);
	auto pos_guard =
		make_scoped_guard([&pos_buf, &pos_buf_size, region_svp] {
			region_truncate(&fiber()->gc, region_svp);
			if (pos_buf != NULL)
				runtime_memory_free(pos_buf, pos_buf_size);
		});
	if (box_iterator_position_unpack(packed_pos, packed_pos_end,
					 index->def->cmp_def, key, part_count,
					 type, &pos, &pos_end) != 0)
		return NULL;
	if (pos != NULL) {
		pos_buf_size = pos_end - pos;
		pos_buf = (char *)xruntime_memory_alloc(pos_buf_size);
		memcpy(pos_buf, pos, pos_buf_size);
		pos = pos_buf;
		pos_end = pos_buf + pos_buf_size;
	}
	box_run_on_select(space, index, itype, key_array);
	struct txn *txn;
	struct txn_ro_savepoint svp;
	if (txn_begin_ro_stmt(space, &txn, &svp) != 0)
		return NULL;
	struct iterator *it = index_create_iterator_after(index, itype, key,
							  part_count, pos);
	txn_end_ro_stmt(txn, &svp);
	if (it == NULL)
		return NULL;
	it->space = space;
	it->pos_buf = pos_buf;
	it->pos_buf_size = pos_buf_size;
	pos_guard.is_active = false;
	region_truncate(&fiber()->gc, region_svp);
	rmean_collect(rmean_box, IPROTO_SELECT, 1);
	return it;
}

box_iterator_t *
box_index_iterator(uint32_t space_id, uint32_t index_id, int type,
		   const char *key, const char *key_end)
{
	return box_index_iterator_after(space_id, index_id, type, key, key_end,
					NULL, NULL);
}

int
box_iterator_next(box_iterator_t *itr, box_tuple_t **result)
{
	assert(result != NULL);
	if (box_check_slice() != 0)
		return -1;
	struct space *space = iterator_space(itr);
	if (space == NULL) {
		*result = NULL;
		return 0;
	}
	struct result_processor res_proc;
	result_process_prepare(&res_proc, space);
	int rc = iterator_next(itr, result);
	result_process_perform(&res_proc, &rc, result);
	if (rc != 0)
		return -1;
	if (*result != NULL)
		tuple_bless(*result);
	return 0;
}

void
box_iterator_free(box_iterator_t *it)
{
	iterator_delete(it);
}

/* }}} */

/* {{{ Other index functions */

int
box_index_stat(uint32_t space_id, uint32_t index_id,
	       struct info_handler *info)
{
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	index_stat(index, info);
	return 0;
}

int
box_index_compact(uint32_t space_id, uint32_t index_id)
{
	struct space *space;
	struct index *index;
	if (check_index(space_id, index_id, &space, &index) != 0)
		return -1;
	index_compact(index);
	return 0;
}

/* }}} */

/* {{{ Internal API */

void
iterator_create(struct iterator *it, struct index *index)
{
	it->next_internal = NULL;
	it->next = NULL;
	it->free = NULL;
	it->space_cache_version = space_cache_version;
	it->space_id = index->def->space_id;
	it->index_id = index->def->iid;
	it->index = index;
	it->space = NULL;
	it->pos_buf = NULL;
	it->pos_buf_size = 0;
}

/**
 * Helper function that checks that the iterated index wasn't dropped
 * and updates it->space and it->space_cache_version on success.
 * Returns 0 on success, -1 on failure.
 */
static int
iterator_check_space(struct iterator *it)
{
	struct space *space = space_by_id(it->space_id);
	if (space == NULL)
		return -1;
	struct index *index = space_index(space, it->index_id);
	if (index != it->index ||
	    index->space_cache_version > it->space_cache_version)
		return -1;
	it->space_cache_version = space_cache_version;
	it->space = space;
	return 0;
}

static bool
iterator_is_valid(struct iterator *it)
{
	/* In case of ephemeral space there is no need to check schema version */
	if (it->space_id == 0)
		return true;
	if (unlikely(it->space_cache_version != space_cache_version)) {
		if (iterator_check_space(it) != 0)
			return false;
	}
	return true;
}

struct space *
iterator_space_slow(struct iterator *it)
{
	if (iterator_check_space(it) != 0)
		return NULL;
	return it->space;
}

int
iterator_next(struct iterator *it, struct tuple **ret)
{
	assert(it->next != NULL);
	if (!iterator_is_valid(it)) {
		*ret = NULL;
		return 0;
	}
	return it->next(it, ret);
}

int
iterator_next_internal(struct iterator *it, struct tuple **ret)
{
	assert(it->next_internal != NULL);
	if (!iterator_is_valid(it)) {
		*ret = NULL;
		return 0;
	}
	return it->next_internal(it, ret);
}

int
iterator_position(struct iterator *it, const char **pos, uint32_t *size)
{
	assert(it->position != NULL);
	assert(pos != NULL);
	assert(size != NULL);
	if (!iterator_is_valid(it)) {
		*pos = NULL;
		*size = 0;
		return 0;
	}
	return it->position(it, pos, size);
}

#define ITERATOR_POS_B64_OPTS (BASE64_NOPAD | BASE64_NOWRAP)

size_t
iterator_position_pack_bufsize(const char *pos, const char *pos_end)
{
	assert(pos != NULL);
	return base64_bufsize(pos_end - pos, ITERATOR_POS_B64_OPTS);
}

void
iterator_position_pack(const char *pos, const char *pos_end,
		       char *buf, size_t buf_size,
		       const char **packed_pos, const char **packed_pos_end)
{
	assert(pos != NULL);
	assert(pos_end != NULL);
	assert(buf != NULL);
	assert(buf_size >= iterator_position_pack_bufsize(pos, pos_end));
	int encoded_size = base64_encode(pos, pos_end - pos, buf, buf_size,
					 ITERATOR_POS_B64_OPTS);
	*packed_pos = buf;
	*packed_pos_end = buf + encoded_size;
}

#undef ITERATOR_POS_B64_OPTS

size_t
iterator_position_unpack_bufsize(const char *packed_pos,
				 const char *packed_pos_end)
{
	assert(packed_pos != NULL);
	assert(packed_pos_end != NULL);
	assert(packed_pos < packed_pos_end);
	/* See description of base64_decode. */
	return 3 * (packed_pos_end - packed_pos) / 4 + 1;
}

int
iterator_position_unpack(const char *packed_pos, const char *packed_pos_end,
			 char *buf, size_t buf_size, const char **pos,
			 const char **pos_end)
{
	assert(packed_pos != NULL);
	assert(packed_pos_end != NULL);
	assert(buf != NULL);
	assert(pos != NULL);
	assert(pos_end != NULL);
	assert(packed_pos_end > packed_pos);
	assert(buf_size >=
	       iterator_position_unpack_bufsize(packed_pos, packed_pos_end));

	*pos = NULL;
	*pos_end = NULL;
	int decoded_size = base64_decode(packed_pos,
					 packed_pos_end - packed_pos,
					 buf, buf_size);
	const char *decoded_pos = buf;
	const char *decoded_pos_end = buf + decoded_size;
	const char *decoded_pos_check = decoded_pos;
	if (mp_check(&decoded_pos_check, decoded_pos_end) != 0)
		goto fail;
	if (decoded_pos_check != decoded_pos_end)
		goto fail;
	if (mp_typeof(*decoded_pos) != MP_ARRAY)
		goto fail;
	*pos = decoded_pos;
	*pos_end = decoded_pos_end;
	return 0;
fail:
	diag_set(ClientError, ER_ITERATOR_POSITION);
	return -1;
}

int
iterator_position_validate(const char *pos, uint32_t pos_part_count,
			   const char *key, uint32_t key_part_count,
			   struct key_def *cmp_def, enum iterator_type type)
{
	int cmp;
	/* Position must be compatible with the index. */
	if (exact_key_validate_nullable(cmp_def, pos, pos_part_count) != 0)
		goto fail;
	/* Position msut meet the search criteria. */
	cmp = key_compare(pos, pos_part_count, HINT_NONE,
			  key, key_part_count, HINT_NONE, cmp_def);
	if (iterator_direction(type) * cmp < 0)
		goto fail;
	if ((type == ITER_EQ || type == ITER_REQ) && cmp != 0)
		goto fail;
	return 0;
fail:
	diag_set(ClientError, ER_ITERATOR_POSITION);
	return -1;
}

void
iterator_delete(struct iterator *it)
{
	if (it->pos_buf != NULL) {
		runtime_memory_free(it->pos_buf, it->pos_buf_size);
		it->pos_buf = NULL;
		it->pos_buf_size = 0;
	}
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
	index->refs = 1;
	index->space_cache_version = space_cache_version;
	static uint32_t unique_id = 0;
	index->unique_id = unique_id++;
	/* Unusable until set to proper value during space creation. */
	index->dense_id = UINT32_MAX;
	rlist_create(&index->read_gaps);
	rlist_create(&index->full_scans);
	return 0;
}

void
index_delete(struct index *index)
{
	assert(index->refs == 0);
	/*
	 * Free index_def after destroying the index as
	 * engine might still need it, e.g. to check if
	 * the index is primary or secondary.
	 */
	struct index_def *def = index->def;
	memtx_tx_on_index_delete(index);
	index->vtab->destroy(index);
	index_def_delete(def);
}

int
index_read_view_create(struct index_read_view *rv,
		       const struct index_read_view_vtab *vtab,
		       struct index_def *def)
{
	rv->vtab = vtab;
	rv->def = index_def_dup(def);
	if (rv->def == NULL)
		return -1;
	rv->space = NULL;
	return 0;
}

void
index_read_view_delete(struct index_read_view *rv)
{
	struct index_def *def = rv->def;
	rv->vtab->free(rv);
	index_def_delete(def);
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
generic_index_commit_drop(struct index *, int64_t)
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

bool
generic_index_def_change_requires_rebuild(struct index *,
					  const struct index_def *)
{
	return true;
}

ssize_t
generic_index_size(struct index *index)
{
	diag_set(UnsupportedIndexFeature, index->def, "size()");
	return -1;
}

ssize_t
generic_index_bsize(struct index *)
{
	return 0;
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
generic_index_get_internal(struct index *index, const char *key,
			   uint32_t part_count, struct tuple **result)
{
	(void)key;
	(void)part_count;
	(void)result;
	diag_set(UnsupportedIndexFeature, index->def, "get_internal()");
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
		      struct tuple **result, struct tuple **successor)
{
	(void)old_tuple;
	(void)new_tuple;
	(void)mode;
	(void)result;
	(void)successor;
	diag_set(UnsupportedIndexFeature, index->def, "replace()");
	return -1;
}

struct iterator *
generic_index_create_iterator(struct index *base, enum iterator_type type,
			      const char *key, uint32_t part_count,
			      const char *pos)
{
	(void)type;
	(void)key;
	(void)part_count;
	(void)pos;
	diag_set(UnsupportedIndexFeature, base->def, "read view");
	return NULL;
}


struct index_read_view *
generic_index_create_read_view(struct index *index)
{
	diag_set(UnsupportedIndexFeature, index->def, "consistent read view");
	return NULL;
}

void
generic_index_stat(struct index *index, struct info_handler *handler)
{
	(void)index;
	info_begin(handler);
	info_end(handler);
}

void
generic_index_compact(struct index *index)
{
	(void)index;
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
	/*
	 * Note this is not no-op call in case of rtee index:
	 * reserving 0 bytes is required during rtree recovery.
	 * For details see memtx_rtree_index_reserve().
	 */
	if (index_reserve(index, 0) != 0)
		return -1;
	return index_replace(index, NULL, tuple, DUP_INSERT, &unused, &unused);
}

void
generic_index_end_build(struct index *)
{
}

int
disabled_index_build_next(struct index *index, struct tuple *tuple)
{
	(void) index; (void) tuple;
	return 0;
}

int
disabled_index_replace(struct index *index, struct tuple *old_tuple,
		       struct tuple *new_tuple, enum dup_replace_mode mode,
		       struct tuple **result, struct tuple **successor)
{
	(void) old_tuple; (void) new_tuple; (void) mode;
	(void) index;
	*result = NULL;
	*successor = NULL;
	return 0;
}

int
exhausted_iterator_next(struct iterator *it, struct tuple **ret)
{
	(void)it;
	*ret = NULL;
	return 0;
}

int
exhausted_index_read_view_iterator_next_raw(struct index_read_view_iterator *it,
					    struct read_view_tuple *result)
{
	(void)it;
	*result = read_view_tuple_none();
	return 0;
}

int
generic_iterator_position(struct iterator *it, const char **pos,
			  uint32_t *size)
{
	(void)it;
	(void)pos;
	(void)size;
	diag_set(UnsupportedIndexFeature, it->index->def, "pagination");
	return -1;
}

int
generic_index_read_view_iterator_position(struct index_read_view_iterator *it,
					  const char **pos, uint32_t *size)
{
	(void)it;
	(void)pos;
	(void)size;
	diag_set(UnsupportedIndexFeature, it->base.index->def, "pagination");
	return -1;
}

/* }}} */
