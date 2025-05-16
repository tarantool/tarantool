#include <lua.h>
#include <lauxlib.h>
#include <module.h>
#include <msgpuck.h>
#include <stddef.h>
#include <stdint.h>

#include "trivia/config.h"
#include "trivia/util.h"
#include "arrow/abi.h"

#if defined(ENABLE_MEMCS_ENGINE) || defined(ENABLE_QUIVER_ENGINE)
# define ENABLE_ARROW 1
#endif /* ENABLE_MEMCS_ENGINE || ENABLE_QUIVER_ENGINE */

#if defined(ENABLE_READ_VIEW)
static box_raw_read_view_t *rv;

static void
init_rv(struct lua_State *L)
{
	if (rv == NULL) {
		rv = box_raw_read_view_new("test");
		if (rv == NULL)
			luaT_error(L);
	}
}
#endif /* ENABLE_READ_VIEW */

#if defined(ENABLE_ARROW)
static void
arrow_schema_destroy(struct ArrowSchema *schema)
{
	for (int i = 0; i < schema->n_children; i++) {
		struct ArrowSchema *child = schema->children[i];
		child->release(child);
		free(child);
	}
	free(schema->children);
	free((void *)schema->name);
	schema->release = NULL;
}

static void
arrow_schema_create(struct ArrowSchema *schema, int column_count)
{
	*schema = (struct ArrowSchema) {
		.format = "+s",
		.n_children = column_count,
		.children = xmalloc(sizeof(struct ArrowSchema *) *
				    column_count),
		.release = arrow_schema_destroy,
	};
	for (int i = 0; i < column_count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "field_%d", i + 1);
		schema->children[i] = xmalloc(sizeof(*schema->children[i]));
		*schema->children[i] = (struct ArrowSchema) {
			.format = "L",
			.name = xstrdup(name),
			.release = arrow_schema_destroy,
		};
	};
}

static void
arrow_array_destroy(struct ArrowArray *array)
{
	for (int i = 0; i < array->n_children; i++) {
		struct ArrowArray *child = array->children[i];
		child->release(child);
		free(child);
	}
	free(array->children);
	for (int i = 0; i < array->n_buffers; i++)
		free((void *)array->buffers[i]);
	free(array->buffers);
	array->release = NULL;
}

static void
arrow_array_create(struct ArrowArray *array, int column_count,
		   int row_count, int row_offset, int total_row_count)
{
	*array = (struct ArrowArray) {
		.length = row_count,
		.n_buffers = 1,
		.n_children = column_count,
		.buffers = xcalloc(1, sizeof(void *)),
		.children = xmalloc(sizeof(struct ArrowArray *) * column_count),
		.release = arrow_array_destroy,
	};
	for (int i = 0; i < column_count; i++) {
		array->children[i] = xmalloc(sizeof(*array->children[i]));
		*array->children[i] = (struct ArrowArray) {
			.length = row_count,
			.n_buffers = 2,
			.buffers = xcalloc(2, sizeof(void *)),
			.release = arrow_array_destroy,
		};
		uint64_t *data = xaligned_alloc(row_count * 8, 64);
		for (int j = 0; j < row_count; j++) {
			int val = row_offset + j + 1;
			if (i % 2 == 1)
				val = total_row_count - val + 1;
			assert(val > 0);
			data[j] = (uint64_t)val;
		}
		array->children[i]->buffers[1] = data;
	};
}

static int
gen_arrow_lua_func(struct lua_State *L)
{
	uint32_t space_id = luaL_checkinteger(L, 1);
	int column_count = luaL_checkinteger(L, 2);
	int row_count = luaL_checkinteger(L, 3);
	int pct_complete = 0;
	say_info("Generating the test data set...");
	for (int row_offset = 0; row_offset < row_count; ) {
		int batch_row_count = MIN(1000, row_count - row_offset);
		struct ArrowSchema schema;
		arrow_schema_create(&schema, column_count);
		struct ArrowArray array;
		arrow_array_create(&array, column_count, batch_row_count,
				   row_offset, row_count);
		if (box_insert_arrow(space_id, &array, &schema) != 0)
			return luaT_error(L);
		row_offset += batch_row_count;
		int pct = 100 * row_offset / row_count;
		if (pct != pct_complete) {
			say_info("%d%% complete", pct);
			pct_complete = pct;
		}
	}
	return 0;
}
#endif /* defined(ENABLE_ARROW) */

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
	init_rv(L);
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
	init_rv(L);
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

LUA_API int
luaopen_column_scan_module(struct lua_State *L)
{
	static const struct luaL_Reg lib[] = {
#if defined(ENABLE_ARROW)
		{"gen_arrow", gen_arrow_lua_func},
#endif /* defined(ENABLE_ARROW) */
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
