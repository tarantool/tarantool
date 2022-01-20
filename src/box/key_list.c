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
#include "key_list.h"

#include "errcode.h"
#include "diag.h"
#include "index_def.h"
#include "func.h"
#include "func_def.h"
#include "fiber.h"
#include "key_def.h"
#include "port.h"
#include "schema.h"
#include "tt_static.h"

int
key_list_iterator_create(struct key_list_iterator *it, struct tuple *tuple,
			 struct index_def *index_def, bool validate,
			 key_list_allocator_t key_allocator)
{
	it->index_def = index_def;
	it->validate = validate;
	it->tuple = tuple;
	it->key_allocator = key_allocator;

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct func *func = index_def->key_def->func_index_func;

	struct port out_port, in_port;
	port_c_create(&in_port);
	port_c_add_tuple(&in_port, tuple);
	int rc = func_call(func, &in_port, &out_port);
	port_destroy(&in_port);
	if (rc != 0) {
		/* Can't evaluate function. */
		struct space *space = space_by_id(index_def->space_id);
		diag_add(ClientError, ER_FUNC_INDEX_FUNC, index_def->name,
			 space ? space_name(space) : "",
			 "can't evaluate function");
		return -1;
	}
	uint32_t key_data_sz;
	const char *key_data = port_get_msgpack(&out_port, &key_data_sz);
	port_destroy(&out_port);
	if (key_data == NULL) {
		struct space *space = space_by_id(index_def->space_id);
		/* Can't get a result returned by function . */
		diag_add(ClientError, ER_FUNC_INDEX_FUNC, index_def->name,
			 space ? space_name(space) : "",
			 "can't get a value returned by function");
		return -1;
	}

	it->data_end = key_data + key_data_sz;
	assert(mp_typeof(*key_data) == MP_ARRAY);
	if (mp_decode_array(&key_data) != 1) {
		struct space *space = space_by_id(index_def->space_id);
		/*
		 * Function return doesn't follow the
		 * convention: to many values were returned.
		 * i.e. return 1, 2
		 */
		region_truncate(region, region_svp);
		diag_set(ClientError, ER_FUNC_INDEX_FORMAT, index_def->name,
			 space ? space_name(space) : "",
			 "to many values were returned");
		return -1;
	}
	if (func->def->opts.is_multikey) {
		if (mp_typeof(*key_data) != MP_ARRAY) {
			struct space * space = space_by_id(index_def->space_id);
			/*
			 * Multikey function must return an array
			 * of keys.
			 */
			region_truncate(region, region_svp);
			diag_set(ClientError, ER_FUNC_INDEX_FORMAT,
				 index_def->name,
				 space ? space_name(space) : "",
				 "a multikey function mustn't return a scalar");
			return -1;
		}
		(void)mp_decode_array(&key_data);
	}
	it->data = key_data;
	return 0;
}

int
key_list_iterator_next(struct key_list_iterator *it, const char **value)
{
	assert(it->data <= it->data_end);
	if (it->data == it->data_end) {
		*value = NULL;
		return 0;
	}
	const char *key = it->data;
	if (!it->validate) {
		/*
		 * Valid key is a MP_ARRAY, so just go to the
		 * next key via mp_next().
		 */
		mp_next(&it->data);
		assert(it->data <= it->data_end);
		*value = it->key_allocator(key, it->data - key);
		return *value != NULL ? 0 : -1;
	}

	if (mp_typeof(*key) != MP_ARRAY) {
		struct space *space = space_by_id(it->index_def->space_id);
		/*
		 * A value returned by func_index function is
		 * not a valid key, i.e. {1}.
		 */
		diag_set(ClientError, ER_FUNC_INDEX_FORMAT, it->index_def->name,
			 space ? space_name(space) : "",
			 tt_sprintf("supplied key type is invalid: expected %s",
				    field_type_strs[FIELD_TYPE_ARRAY]));
		return -1;
	}
	struct key_def *key_def = it->index_def->key_def;
	const char *rptr = key;
	uint32_t part_count = mp_decode_array(&rptr);
	if (part_count != key_def->part_count) {
		struct space *space = space_by_id(it->index_def->space_id);
		/*
		 * The key must have exact functional index
		 * definition's part_count(s).
		 */
		diag_set(ClientError, ER_FUNC_INDEX_FORMAT, it->index_def->name,
			 space ? space_name(space) : "",
			 tt_sprintf(tnt_errcode_desc(ER_EXACT_MATCH),
				   key_def->part_count, part_count));
		return -1;
	}
	const char *key_end;
	if (key_validate_parts(key_def, rptr, part_count, true,
			       &key_end) != 0) {
		struct space *space = space_by_id(it->index_def->space_id);
		/*
		 * The key doesn't follow functional index key
		 * definition.
		 */
		diag_add(ClientError, ER_FUNC_INDEX_FORMAT, it->index_def->name,
			 space ? space_name(space) : "",
			 "key does not follow functional index definition");
		return -1;
	}

	it->data = key_end;
	*value = it->key_allocator(key, key_end - key);
	return *value != NULL ? 0 : -1;
}
