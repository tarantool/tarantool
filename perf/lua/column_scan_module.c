#include <lua.h>
#include <lauxlib.h>
#include <module.h>
#include <msgpuck.h>
#include <stddef.h>
#include <stdint.h>

#include "trivia/config.h"
#include "trivia/util.h"

#if defined(ENABLE_MEMCS_ENGINE)
# define ENABLE_SCANNER 1
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
		box_raw_read_view_index_by_id(space, 0);
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

#if defined(ENABLE_SCANNER)
static int
sum_scanner_lua_func(struct lua_State *L)
{
	uint32_t space_id = luaL_checkinteger(L, 1);
	uint32_t index_id = luaL_checkinteger(L, 2);
	uint32_t field_no = luaL_checkinteger(L, 3);
	char key[8];
	char *key_end = mp_encode_array(key, 0);
	uint32_t fields[] = {field_no};
	uint32_t field_count = lengthof(fields);
	box_scanner_t *scanner = box_index_scanner(space_id, index_id,
						   field_count, fields,
						   key, key_end, NULL);
	if (scanner == NULL)
		return luaT_error(L);
	int rc = 0;
	uint64_t sum = 0;
	size_t region_svp = box_region_used();
	while (true) {
		box_scanner_result_t result;
		rc = box_scanner_next(scanner, 4096, &result);
		if (rc != 0 || result.row_count == 0)
			break;
		if (unlikely(result.columns[0].type != SCANNER_COLUMN_UINT64)) {
			rc = box_error_raise(ER_PROC_LUA, "unexpected result");
			break;
		}
		uint64_t *values = result.columns[0].data;
		for (int i = 0; i < (int)result.row_count; i++)
			sum += values[i];
		box_region_truncate(region_svp);
	}
	box_region_truncate(region_svp);
	box_scanner_free(scanner);
	if (rc != 0)
		return luaT_error(L);
	luaL_pushuint64(L, sum);
	return 1;
}
#endif /* defined(ENABLE_SCANNER) */

#if defined(ENABLE_SCANNER) && defined(ENABLE_READ_VIEW)
static int
sum_scanner_rv_lua_func(struct lua_State *L)
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
		box_raw_read_view_index_by_id(space, 0);
	if (index == NULL)
		return luaT_error(L);
	char key[8];
	char *key_end = mp_encode_array(key, 0);
	uint32_t fields[] = {field_no};
	uint32_t field_count = lengthof(fields);
	box_raw_read_view_scanner_t *scanner = box_raw_read_view_scanner_new(
		index, field_count, fields, key, key_end, NULL);
	if (scanner == NULL)
		return luaT_error(L);
	int rc = 0;
	uint64_t sum = 0;
	size_t region_svp = box_region_used();
	while (true) {
		box_scanner_result_t result;
		rc = box_raw_read_view_scanner_next(scanner, 4096, &result);
		if (rc != 0 || result.row_count == 0)
			break;
		if (unlikely(result.columns[0].type != SCANNER_COLUMN_UINT64)) {
			rc = box_error_raise(ER_PROC_LUA, "unexpected result");
			break;
		}
		uint64_t *values = result.columns[0].data;
		for (int i = 0; i < (int)result.row_count; i++)
			sum += values[i];
		box_region_truncate(region_svp);
	}
	box_region_truncate(region_svp);
	box_raw_read_view_scanner_delete(scanner);
	if (rc != 0)
		return luaT_error(L);
	luaL_pushuint64(L, sum);
	return 1;
}
#endif /* defined(ENABLE_SCANNER) && defined(ENABLE_READ_VIEW) */

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
#if defined(ENABLE_SCANNER)
		{"sum_scanner", sum_scanner_lua_func},
#endif /* defined(ENABLE_SCANNER) */
#if defined(ENABLE_SCANNER) && defined(ENABLE_READ_VIEW)
		{"sum_scanner_rv", sum_scanner_rv_lua_func},
#endif /* defined(ENABLE_SCANNER) && defined(ENABLE_READ_VIEW) */
		{NULL, NULL},
	};
	luaL_register(L, "column_scan_module", lib);
	return 1;
}
