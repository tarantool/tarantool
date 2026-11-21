#include <lua.h>
#include <lauxlib.h>
#include <module.h>
#include <msgpuck.h>
#include <stddef.h>
#include <stdint.h>

#include "trivia/config.h"
#include "trivia/util.h"

/** Template tuple of a load fiber: one per PK msgpack field size. */
struct base_tuple {
	/** The tuple data. */
	char *data;
	/** The end of the tuple data. */
	char *data_end;
};

/** The PK values that produce the PK field sizes the templates cover. */
static const uint32_t base_tuple_pk_values[] = {
	0, UINT8_MAX, UINT16_MAX, UINT32_MAX
};

static bool
find_base_tuple(struct base_tuple *tuples, uint32_t pk_value,
		struct base_tuple **tuple)
{
	size_t sizeof_pk_value = mp_sizeof_uint(pk_value);
	for (size_t i = 0; i < lengthof(base_tuple_pk_values); i++) {
		if (mp_sizeof_uint(base_tuple_pk_values[i]) != sizeof_pk_value)
			continue;
		*tuple = &tuples[i];
		return true;
	}
	/*
	 * Unreachable for a uint32_t key, all of whose encoded sizes are
	 * covered. Kept as a guard for a table that gains or loses a size
	 * class.
	 */
	box_error_raise(ER_UNSUPPORTED, "No tuple for PK value of size %zu, "
			"value: %u", sizeof_pk_value, pk_value);
	return false;
}

static bool
test_tuple(struct base_tuple *tuples, uint32_t pk_value,
	   char **tuple, char **tuple_end)
{
	struct base_tuple *base_tuple;
	if (!find_base_tuple(tuples, pk_value, &base_tuple))
		return false;
	/*
	 * The template is chosen by the encoded size of the new PK value,
	 * so the field is rewritten in place.
	 */
	const char *data = base_tuple->data;
	mp_decode_array(&data);
	mp_encode_uint((char *)data, pk_value);
	*tuple = base_tuple->data;
	*tuple_end = base_tuple->data_end;
	return true;
}

static char *
encode_base_tuple(char *data, uint32_t pk_value, uint32_t num_columns)
{
	data = mp_encode_array(data, num_columns);
	data = mp_encode_uint(data, pk_value);
	for (uint32_t i = 1; i < num_columns; i++)
		data = mp_encode_uint(data, 0);
	return data;
}

/** Allocate one template tuple per PK field size. */
static void
create_base_tuples(struct base_tuple *tuples, uint32_t num_columns)
{
	for (size_t i = 0; i < lengthof(base_tuple_pk_values); i++) {
		uint32_t pk_value = base_tuple_pk_values[i];
		ptrdiff_t sizeof_tuple = mp_sizeof_array(num_columns) +
					 mp_sizeof_uint(pk_value) +
					 (num_columns - 1) * mp_sizeof_uint(0);
		tuples[i].data = xcalloc(1, sizeof_tuple);
		tuples[i].data_end = encode_base_tuple(tuples[i].data, pk_value,
						       num_columns);
	}
}

/** Release the template tuples. */
static void
destroy_base_tuples(struct base_tuple *tuples)
{
	for (size_t i = 0; i < lengthof(base_tuple_pk_values); i++)
		free(tuples[i].data);
}

static bool
do_transaction(struct base_tuple *tuples, uint32_t space_id,
	       uint32_t ops_per_txn, uint32_t *start,
	       uint32_t max_space_size)
{
	if (box_txn_begin() != 0)
		return false;
	for (uint32_t j = 0; j < ops_per_txn; j++) {
		char *tuple, *tuple_end;
		if (!test_tuple(tuples, *start, &tuple, &tuple_end))
			goto fail;
		if (box_replace(space_id, tuple, tuple_end, NULL) != 0)
			goto fail;
		++*start;
		if (*start == max_space_size)
			*start = 0;
	}
	return box_txn_commit() == 0;

fail:
	box_txn_rollback();
	return false;
}

static int
load_fiber_func(struct lua_State *L)
{
	uint32_t space_id = luaL_checkinteger(L, 1);
	uint32_t trans_per_fiber = luaL_checkinteger(L, 2);
	uint32_t ops_per_txn = luaL_checkinteger(L, 3);
	uint32_t num_columns = luaL_checkinteger(L, 4);
	uint32_t start = luaL_checkinteger(L, 5);
	uint32_t max_space_size = luaL_checkinteger(L, 6);
	struct base_tuple tuples[lengthof(base_tuple_pk_values)];
	create_base_tuples(tuples, num_columns);
	bool success = true;
	for (uint32_t i = 0; success && i < trans_per_fiber; i++) {
		success = do_transaction(tuples, space_id, ops_per_txn,
					 &start, max_space_size);
		fiber_sleep(0);
	}
	destroy_base_tuples(tuples);
	if (!success)
		return luaT_error(L);
	return 0;
}

LUA_API int
luaopen_1mops_write_module(struct lua_State *L)
{
	static const struct luaL_Reg lib[] = {
		{"fiber", load_fiber_func},
		{NULL, NULL},
	};
	luaL_register(L, "1mops_write_module", lib);
	return 1;
}
