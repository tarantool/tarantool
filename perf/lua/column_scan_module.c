#include <lua.h>
#include <lauxlib.h>
#include <module.h>
#include <msgpuck.h>
#include <stddef.h>
#include <stdint.h>

#include "trivia/config.h"
#include "trivia/util.h"
#include "arrow/abi.h"

#if defined(ENABLE_MEMCS_ENGINE)
# define ENABLE_ARROW 1
#endif /* ENABLE_MEMCS_ENGINE */

#if defined(ENABLE_READ_VIEW)
static box_raw_read_view_t *rv;
#endif /* ENABLE_READ_VIEW */

static int
sum_iterator_lua_func(struct lua_State *L)
{
	uint32_t space_id = luaL_checkinteger(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	uint32_t field_no = luaL_checkinteger(L, 3);
	char key[8];
	char *key_end = mp_encode_array(key, 0);
	box_iterator_t *iter = box_index_iterator(space_id, index_id, ITER_ALL,
						  key, key_end);
	if (iter == NULL)
		return luaT_error(L);
	int rc = 0;
	uint64_t sum = 0;
	box_tuple_t *tuple;
	while (true) {
		rc = box_iterator_next(iter, &tuple);
		if (rc != 0 || tuple == NULL)
			break;
		const char *data = box_tuple_field(tuple, field_no);
		if (unlikely(data == NULL || mp_typeof(*data) != MP_UINT)) {
			rc = box_error_raise(ER_PROC_LUA, "unexpected result");
			break;
		}
		sum += mp_decode_uint(&data);
	}
	box_iterator_free(iter);
	if (rc != 0)
		return luaT_error(L);
	luaL_pushuint64(L, sum);
	return 1;
}

#if defined(ENABLE_READ_VIEW)
static int
sum_iterator_rv_lua_func(struct lua_State *L)
{
	if (rv == NULL)
		return luaL_error(L, "run init() first");
	uint32_t space_id = luaL_checkinteger(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	uint32_t field_no = luaL_checkinteger(L, 3);
	box_raw_read_view_space_t *space =
		box_raw_read_view_space_by_id(rv, space_id);
	if (space == NULL)
		return luaT_error(L);
	box_raw_read_view_index_t *index =
		box_raw_read_view_index_by_id(space, index_id);
	if (index == NULL)
		return luaT_error(L);
	char key[8];
	char *key_end = mp_encode_array(key, 0);
	box_raw_read_view_iterator_t iter;
	if (box_raw_read_view_iterator_create(&iter, index, ITER_ALL,
					      key, key_end) != 0)
		return luaT_error(L);
	int rc = 0;
	uint64_t sum = 0;
	while (true) {
		uint32_t size;
		const char *data;
		rc = box_raw_read_view_iterator_next(&iter, &data, &size);
		if (rc != 0 || data == NULL)
			break;
		if (unlikely(mp_typeof(*data) != MP_ARRAY ||
			     mp_decode_array(&data) <= field_no)) {
			rc = box_error_raise(ER_PROC_LUA, "unexpected result");
			break;
		}
		for (int i = 0; i < (int)field_no; i++)
			mp_next(&data);
		sum += mp_decode_uint(&data);
	}
	box_raw_read_view_iterator_destroy(&iter);
	if (rc != 0)
		return luaT_error(L);
	luaL_pushuint64(L, sum);
	return 1;
}
#endif /* defined(ENABLE_READ_VIEW) */

#if defined(ENABLE_ARROW)
static int
sum_arrow_lua_func(struct lua_State *L)
{
	uint32_t space_id = luaL_checkinteger(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	uint32_t field_no = luaL_checkinteger(L, 3);
	char key[8];
	char *key_end = mp_encode_array(key, 0);
	uint32_t fields[] = {field_no};
	uint32_t field_count = lengthof(fields);
	box_arrow_options_t *options = box_arrow_options_new();
	box_arrow_options_set_batch_row_count(options, 4096);
	struct ArrowArrayStream stream;
	int rc = box_index_arrow_stream(space_id, index_id, field_count, fields,
					key, key_end, options, &stream);
	if (rc != 0) {
		box_arrow_options_delete(options);
		return luaT_error(L);
	}
	uint64_t sum = 0;
	struct ArrowArray array;
	while (true) {
		rc = stream.get_next(&stream, &array);
		if (rc != 0 || array.n_children != 1)
			break;
		uint64_t *values = (uint64_t *)array.children[0]->buffers[1];
		for (int i = 0; i < (int)array.children[0]->length; i++)
			sum += values[i];
		if (array.release != NULL)
			array.release(&array);
	}
	stream.release(&stream);
	box_arrow_options_delete(options);
	if (array.release != NULL)
		array.release(&array);
	if (rc != 0)
		return luaT_error(L);
	luaL_pushuint64(L, sum);
	return 1;
}
#endif /* defined(ENABLE_ARROW) */

#if defined(ENABLE_ARROW) && defined(ENABLE_READ_VIEW)
static int
sum_arrow_rv_lua_func(struct lua_State *L)
{
	if (rv == NULL)
		return luaL_error(L, "run init() first");
	uint32_t space_id = luaL_checkinteger(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	uint32_t field_no = luaL_checkinteger(L, 3);
	box_raw_read_view_space_t *space =
		box_raw_read_view_space_by_id(rv, space_id);
	if (space == NULL)
		return luaT_error(L);
	box_raw_read_view_index_t *index =
		box_raw_read_view_index_by_id(space, index_id);
	if (index == NULL)
		return luaT_error(L);
	char key[8];
	char *key_end = mp_encode_array(key, 0);
	uint32_t fields[] = {field_no};
	uint32_t field_count = lengthof(fields);
	box_arrow_options_t *options = box_arrow_options_new();
	box_arrow_options_set_batch_row_count(options, 4096);
	struct ArrowArrayStream stream;
	int rc = box_raw_read_view_arrow_stream(index, field_count, fields,
						key, key_end, options, &stream);
	if (rc != 0) {
		box_arrow_options_delete(options);
		return luaT_error(L);
	}
	uint64_t sum = 0;
	struct ArrowArray array;
	while (true) {
		rc = stream.get_next(&stream, &array);
		if (rc != 0 || array.n_children != 1)
			break;
		uint64_t *values = (uint64_t *)array.children[0]->buffers[1];
		for (int i = 0; i < (int)array.children[0]->length; i++)
			sum += values[i];
		if (array.release != NULL)
			array.release(&array);
	}
	stream.release(&stream);
	box_arrow_options_delete(options);
	if (array.release != NULL)
		array.release(&array);
	if (rc != 0)
		return luaT_error(L);
	luaL_pushuint64(L, sum);
	return 1;
}
#endif /* defined(ENABLE_ARROW) && defined(ENABLE_READ_VIEW) */

static int
init_lua_func(struct lua_State *L)
{
#if defined(ENABLE_READ_VIEW)
	rv = box_raw_read_view_new("test");
	if (rv == NULL)
		return luaT_error(L);
#endif /* defined(ENABLE_READ_VIEW) */
	(void)L;
	return 0;
}

LUA_API int
luaopen_column_scan_module(struct lua_State *L)
{
	static const struct luaL_Reg lib[] = {
		{"init", init_lua_func},
		{"sum_iterator", sum_iterator_lua_func},
#if defined(ENABLE_READ_VIEW)
		{"sum_iterator_rv", sum_iterator_rv_lua_func},
#endif /* defined(ENABLE_READ_VIEW) */
#if defined(ENABLE_ARROW)
		{"sum_arrow", sum_arrow_lua_func},
#endif /* defined(ENABLE_ARROW) */
#if defined(ENABLE_ARROW) && defined(ENABLE_READ_VIEW)
		{"sum_arrow_rv", sum_arrow_rv_lua_func},
#endif /* defined(ENABLE_ARROW) && defined(ENABLE_READ_VIEW) */
		{NULL, NULL},
	};
	luaL_register(L, "column_scan_module", lib);
	return 1;
}
