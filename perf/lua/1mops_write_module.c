#include <lua.h>
#include <lauxlib.h>
#include <module.h>
#include <msgpuck.h>
#include <stddef.h>
#include <stdint.h>

#include "trivia/config.h"
#include "trivia/util.h"

/** Template tuples to update and insert: one per PK msgpack field size. */
static char *base_tuple_pk1;
static char *base_tuple_pk1_end;
static char *base_tuple_pk2;
static char *base_tuple_pk2_end;
static char *base_tuple_pk3;
static char *base_tuple_pk3_end;
static char *base_tuple_pk5;
static char *base_tuple_pk5_end;

static bool
find_base_tuple(uint32_t pk_value, char **tuple, char **tuple_end)
{
	size_t sizeof_pk_value = mp_sizeof_uint(pk_value);
	switch (sizeof_pk_value) {
	case 1:
		*tuple = base_tuple_pk1;
		*tuple_end = base_tuple_pk1_end;
		return true;
	case 2:
		*tuple = base_tuple_pk2;
		*tuple_end = base_tuple_pk2_end;
		return true;
	case 3:
		*tuple = base_tuple_pk3;
		*tuple_end = base_tuple_pk3_end;
		return true;
	case 5:
		*tuple = base_tuple_pk5;
		*tuple_end = base_tuple_pk5_end;
		return true;
	}
	box_error_raise(ER_UNSUPPORTED, "No tuple for PK value of size %lu, "
			"value: %u", sizeof_pk_value, pk_value);
	return false;
}

static bool
test_tuple(uint32_t pk_value, char **tuple, char **tuple_end)
{
	/* Get the tuple to update. */
	if (!find_base_tuple(pk_value, tuple, tuple_end))
		return false;

	/* Check if the new PK value is of exact size. */
	const char *data = *tuple;
	mp_decode_array(&data);
	char *pk_value_ptr = (char *)data;
	uint32_t old_pk_value = mp_decode_uint(&data);
	if (mp_sizeof_uint(pk_value) != mp_sizeof_uint(old_pk_value)) {
		box_error_raise(ER_UNSUPPORTED, "Wrong base tuple, "
				"PK value %u", pk_value);
		return false;
	}

	/* Write the new PK value. */
	mp_encode_uint(pk_value_ptr, pk_value);
	return true;
}

static char *
encode_base_tuple(char *data, ptrdiff_t *data_sz,
		  uint32_t pk_value, uint32_t num_columns)
{
	data = mp_encode_array_safe(data, data_sz, num_columns);
	data = mp_encode_uint_safe(data, data_sz, pk_value);
	for (uint32_t i = 1; i < num_columns; i++)
		data = mp_encode_uint_safe(data, data_sz, 0);
	return data;
}

static bool
create_base_tuples(uint32_t num_columns)
{
	uint32_t uint_1 = 0;
	uint32_t uint_2 = UINT8_MAX;
	uint32_t uint_3 = UINT16_MAX;
	uint32_t uint_5 = UINT32_MAX;
	if (mp_sizeof_uint(uint_1) != 1 ||
	    mp_sizeof_uint(uint_2) != 2 ||
	    mp_sizeof_uint(uint_3) != 3 ||
	    mp_sizeof_uint(uint_5) != 5) {
		box_error_raise(ER_UNKNOWN, "PK value size assertion failed");
		return false;
	}

	struct {
		uint32_t pk_value;
		char **ptr;
		char **end;
	} base_tuples[] = {
		{ uint_1, &base_tuple_pk1, &base_tuple_pk1_end },
		{ uint_2, &base_tuple_pk2, &base_tuple_pk2_end },
		{ uint_3, &base_tuple_pk3, &base_tuple_pk3_end },
		{ uint_5, &base_tuple_pk5, &base_tuple_pk5_end },
	};

	for (size_t i = 0; i < lengthof(base_tuples); i++) {
		uint32_t pk_value = base_tuples[i].pk_value;
		char **base_tuple_ptr = base_tuples[i].ptr;
		char **base_tuple_end_ptr = base_tuples[i].end;

		ptrdiff_t sizeof_tuple = 0;
		encode_base_tuple(NULL, &sizeof_tuple, pk_value, num_columns);
		*base_tuple_ptr = xcalloc((uint32_t)sizeof_tuple, 1);
		*base_tuple_end_ptr = encode_base_tuple(*base_tuple_ptr, NULL,
							pk_value, num_columns);
	}
	return true;
}

static bool
do_transaction(uint32_t space_id, uint32_t ops_per_txn, uint32_t *start)
{
	if (box_txn_begin() != 0)
		return false;
	for (uint32_t j = 0; j < ops_per_txn; j++) {
		char *tuple, *tuple_end;
		if (!test_tuple(*start, &tuple, &tuple_end))
			goto fail;
		box_tuple_t *result;
		if (box_replace(space_id, tuple,
				tuple_end, &result) != 0)
			goto fail;
		++*start;
	}
	return box_txn_commit() == 0;

fail:
	box_txn_commit();
	return false;
}

static int
fiber_lua_func(struct lua_State *L)
{
	uint32_t space_id = luaL_checkinteger(L, 1);
	uint32_t trans_per_fiber = luaL_checkinteger(L, 2);
	uint32_t ops_per_txn = luaL_checkinteger(L, 3);
	uint32_t num_columns = luaL_checkinteger(L, 4);
	uint32_t start = luaL_checkinteger(L, 5);
	if (!create_base_tuples(num_columns))
		return luaT_error(L);
	bool success = true;
	for (uint32_t i = 0; success && i < trans_per_fiber; i++) {
		success = do_transaction(space_id, ops_per_txn, &start);
		fiber_sleep(0);
	}
	if (!success)
		return luaT_error(L);
	return 0;
}

LUA_API int
luaopen_1mops_write_module(struct lua_State *L)
{
	static const struct luaL_Reg lib[] = {
		{"fiber", fiber_lua_func},
		{NULL, NULL},
	};
	luaL_register(L, "1mops_write_module", lib);
	return 1;
}
