#include <lua.h>
#include <lauxlib.h>
#include <module.h>
#include <msgpuck.h>
#include <stddef.h>
#include <stdint.h>

#include "trivia/config.h"
#include "trivia/util.h"
#include "arrow/abi.h"

#ifdef ENABLE_MEMCS_ENGINE
# define ENABLE_BATCH_INSERT 1
#endif

static struct {
	int64_t row_count;
	int64_t column_count;
	struct {
		char *name;
		const char *type;
		uint64_t *data;
	} *columns;
} dataset;

static int
insert_serial_lua_func(struct lua_State *L)
{
	uint32_t space_id = luaL_checkinteger(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	lua_getfield(L, 2, "row_count_batch");
	int batch_row_count = luaL_checkinteger(L, -1);
	lua_getfield(L, 2, "column_count_total");
	int total_column_count = luaL_checkinteger(L, -1);
	lua_pop(L, 2);
	static char tuple_data[1000 * 1000];

	VERIFY(box_txn_begin() == 0);
	for (int64_t i = 0; i < dataset.row_count; i++) {
		char *data_end = tuple_data;
		data_end = mp_encode_array(data_end, total_column_count);
		int j;
		for (j = 0; j < dataset.column_count; j++) {
			uint64_t val = dataset.columns[j].data[i];
			data_end = mp_encode_uint(data_end, val);
		}
		for (; j < total_column_count; j++)
			data_end = mp_encode_nil(data_end);
		size_t tuple_size = data_end - tuple_data;
		if (tuple_size > sizeof(tuple_data))
			abort();
		if (box_insert(space_id, tuple_data, data_end, NULL) != 0)
			return luaT_error(L);
		if (i % batch_row_count == 0) {
			VERIFY(box_txn_commit() == 0);
			VERIFY(box_txn_begin() == 0);
		}
	}
	VERIFY(box_txn_commit() == 0);
	return 0;
}

#if defined(ENABLE_BATCH_INSERT)
static void
arrow_schema_destroy(struct ArrowSchema *schema)
{
	for (int i = 0; i < schema->n_children; i++) {
		struct ArrowSchema *child = schema->children[i];
		if (child->release != NULL)
			child->release(child);
		free(child);
	}
	free(schema->children);
	schema->release = NULL;
}

static void
arrow_schema_init(struct ArrowSchema *schema)
{
	*schema = (struct ArrowSchema) {
		.format = "+s",
		.name = NULL,
		.metadata = NULL,
		.flags = 0,
		.n_children = dataset.column_count,
		.children = xmalloc(sizeof(struct ArrowSchema *) *
				    dataset.column_count),
		.dictionary = NULL,
		.release = arrow_schema_destroy,
		.private_data = NULL,
	};
	for (int i = 0; i < dataset.column_count; i++) {
		schema->children[i] = xmalloc(sizeof(*schema->children[i]));
		*schema->children[i] = (struct ArrowSchema) {
			.format = dataset.columns[i].type,
			.name = dataset.columns[i].name,
			.metadata = NULL,
			.flags = 0,
			.n_children = 0,
			.children = NULL,
			.dictionary = NULL,
			.release = arrow_schema_destroy,
			.private_data = NULL,
		};
	};
}

static void
arrow_array_destroy(struct ArrowArray *array)
{
	for (int i = 0; i < array->n_children; i++) {
		struct ArrowArray *child = array->children[i];
		if (child != NULL) {
			if (child->release != NULL)
				child->release(child);
			free(child);
		}
	}
	free(array->children);
	free(array->buffers);
	array->release = NULL;
}

static void
arrow_array_init(struct ArrowArray *array, int row_count)
{
	*array = (struct ArrowArray) {
		.length = row_count,
		.null_count = 0,
		.offset = 0,
		.n_buffers = 1,
		.n_children = dataset.column_count,
		.buffers = xcalloc(1, sizeof(void *)),
		.children = xmalloc(sizeof(struct ArrowArray *)
				    * dataset.column_count),
		.dictionary = NULL,
		.release = arrow_array_destroy,
		.private_data = NULL,
	};
	for (int i = 0; i < dataset.column_count; i++) {
		array->children[i] = xmalloc(sizeof(*array->children[i]));
		*array->children[i] = (struct ArrowArray) {
			.length = row_count,
			.null_count = 0,
			.offset = 0,
			.n_buffers = 2,
			.n_children = 0,
			.buffers = xcalloc(2, sizeof(void *)),
			.children = NULL,
			.dictionary = NULL,
			.release = arrow_array_destroy,
			.private_data = NULL,
		};
	};
}

static int
insert_batch_lua_func(struct lua_State *L)
{
	uint32_t space_id = luaL_checkinteger(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	lua_getfield(L, 2, "row_count_batch");
	int batch_row_count = luaL_checkinteger(L, -1);
	lua_pop(L, 1);

	struct ArrowSchema schema;
	arrow_schema_init(&schema);
	struct ArrowArray array;
	arrow_array_init(&array, batch_row_count);

	assert(dataset.row_count % batch_row_count == 0);
	for (int i = 0; i < dataset.row_count / batch_row_count; i++) {
		for (int j = 0; j < dataset.column_count; j++) {
			array.children[j]->buffers[1] =
				&dataset.columns[j].data[i * batch_row_count];
		}
		if (box_insert_arrow(space_id, &array, &schema) != 0)
			return luaT_error(L);
	}
	schema.release(&schema);
	array.release(&array);
	return 0;
}
#endif /* defined(ENABLE_BATCH_INSERT) */

static int
init_lua_func(struct lua_State *L)
{
	say_info("Generating the test data set...");
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "row_count_total");
	dataset.row_count = luaL_checkinteger(L, -1);
	lua_getfield(L, 1, "column_count_batch");
	dataset.column_count = luaL_checkinteger(L, -1);
	lua_pop(L, 2);
	dataset.columns = xmalloc(dataset.column_count *
				  sizeof(*dataset.columns));
	size_t data_size = dataset.row_count * sizeof(*dataset.columns->data);
	for (int i = 0; i < dataset.column_count; i++) {
		uint64_t *data = xaligned_alloc(data_size, 64);
		size_t name_size = 20;
		char *name = xmalloc(name_size);
		snprintf(name, name_size, "field_%d", i + 1);
		dataset.columns[i].name = name;
		dataset.columns[i].type = "L";
		dataset.columns[i].data = data;
		for (int j = 0; j < dataset.row_count; j++) {
			if (i % 2 == 0)
				data[j] = j;
			else
				data[j] = dataset.row_count - j;
		}
	}
	return 0;
}

static int
fini_lua_func(struct lua_State *L)
{
	(void)L;
	for (int i = 0; i < dataset.column_count; i++) {
		free(dataset.columns[i].name);
		free(dataset.columns[i].data);
	}
	free(dataset.columns);
	dataset.columns = NULL;
	dataset.column_count = 0;
	dataset.row_count = 0;
	return 0;
}

LUA_API int
luaopen_column_insert_module(struct lua_State *L)
{
	static const struct luaL_Reg lib[] = {
		{"init", init_lua_func},
		{"fini", fini_lua_func},
		{"insert_serial", insert_serial_lua_func},
#ifdef ENABLE_BATCH_INSERT
		{"insert_batch", insert_batch_lua_func},
#endif
		{NULL, NULL},
	};
	luaL_register(L, "column_insert_module", lib);
	return 1;
}
