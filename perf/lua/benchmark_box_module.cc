/*
 * Copyright 2024, Tarantool AUTHORS, please see AUTHORS file.
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

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <module.h>
#include <msgpuck.h>
#include <stddef.h>
#include <stdint.h>

#include <random>

#include "trivia/config.h"
#include "trivia/util.h"

enum {
	MAX_STRING_SIZE = 1024,
	MAX_PAYLOAD_SIZE = 1024 * 1024,
};

enum distribution {
	INCREMENTAL,      /* 0, 1, 2, etc. */
	LINEAR,           /* Random, linear distribution. */
	distribution_MAX,
};

const char *distribution_strs[] = {
	"incremental",
	"linear",
};

static_assert(lengthof(distribution_strs) == distribution_MAX);

const char *field_type_strs[] = {
	/* [FIELD_TYPE_ANY]      = */ "any",
	/* [FIELD_TYPE_UNSIGNED] = */ "unsigned",
	/* [FIELD_TYPE_STRING]   = */ "string",
	/* [FIELD_TYPE_NUMBER]   = */ "number",
	/* [FIELD_TYPE_DOUBLE]   = */ "double",
	/* [FIELD_TYPE_INTEGER]  = */ "integer",
	/* [FIELD_TYPE_BOOLEAN]  = */ "boolean",
	/* [FIELD_TYPE_VARBINARY] = */"varbinary",
	/* [FIELD_TYPE_SCALAR]   = */ "scalar",
	/* [FIELD_TYPE_DECIMAL]  = */ "decimal",
	/* [FIELD_TYPE_UUID]     = */ "uuid",
	/* [FIELD_TYPE_DATETIME] = */ "datetime",
	/* [FIELD_TYPE_INTERVAL] = */ "interval",
	/* [FIELD_TYPE_ARRAY]    = */ "array",
	/* [FIELD_TYPE_MAP]      = */ "map",
	/* [FIELD_TYPE_INT8]     = */ "int8",
	/* [FIELD_TYPE_UINT8]    = */ "uint8",
	/* [FIELD_TYPE_INT16]    = */ "int16",
	/* [FIELD_TYPE_UINT16]   = */ "uint16",
	/* [FIELD_TYPE_INT32]    = */ "int32",
	/* [FIELD_TYPE_UINT32]   = */ "uint32",
	/* [FIELD_TYPE_INT64]    = */ "int64",
	/* [FIELD_TYPE_UINT64]   = */ "uint64",
	/* [FIELD_TYPE_FLOAT32]  = */ "float32",
	/* [FIELD_TYPE_FLOAT64]  = */ "float64",
};

static_assert(lengthof(field_type_strs) == field_type_MAX,
	       "Each field type must be present in field_type_strs");

struct Payload {
	enum field_type type;
	enum distribution distribution;
};

struct Options {
	int request_count;
	int payload_size;
	struct Payload *payload;
};

struct Benchmark {
	struct Options *options;
	union State {
		uint64_t u64;
		struct {
			size_t len;
			char *buf;
		} str;
	} *payload_states;
	char *payload_buf;
	std::minstd_rand rng; /* Generates unique and reproducible values. */
};

uint32_t
strindex(const char *const *haystack, const char *needle, uint32_t hmax)
{
	for (unsigned index = 0; index != hmax && haystack[index]; index++)
		if (strcasecmp(haystack[index], needle) == 0)
			return index;
	return hmax;
}

static int
benchmark_init_payload_incremental(struct Benchmark *benchmark, int i)
{
	struct Options *options = benchmark->options;
	switch (options->payload[i].type) {
	case FIELD_TYPE_UNSIGNED:
		benchmark->payload_states[i].u64 = 0;
		break;
	case FIELD_TYPE_STRING:
		benchmark->payload_states[i].str.buf =
			(char *)box_region_alloc(MAX_STRING_SIZE);
		strcpy(benchmark->payload_states[i].str.buf, "0");
		benchmark->payload_states[i].str.len = 1;
		break;
	default:
		box_error_raise(ER_PROC_LUA, "unsupported type (inc.init)");
		return -1;
	}
	return 0;
}

static int
benchmark_init_payload_linear(struct Benchmark *benchmark, int i)
{
	struct Options *options = benchmark->options;
	if (options->payload[i].type != FIELD_TYPE_UNSIGNED &&
	    options->payload[i].type != FIELD_TYPE_STRING) {
		box_error_raise(ER_PROC_LUA, "unsupported type (lin.init)");
		return -1;
	}
	return 0;
}

static int
benchmark_init(struct Benchmark *benchmark, struct Options *options)
{
	benchmark->options = options;
	benchmark->payload_buf = (char *)box_region_alloc(MAX_PAYLOAD_SIZE);
	benchmark->payload_states = (union Benchmark::State *)
		box_region_aligned_alloc(options->payload_size,
					 alignof(Benchmark::State));
	int rc;
	for (int i = 0; i < options->payload_size; i++) {
		switch (options->payload[i].distribution) {
		case INCREMENTAL:
			rc = benchmark_init_payload_incremental(benchmark, i);
			break;
		case LINEAR:
			rc = benchmark_init_payload_linear(benchmark, i);
			break;
		case distribution_MAX:
			unreachable();
		}
		if (rc != 0)
			return -1;
	}
	return 0;
}

static int
benchmark_next_payload_incremental(struct Benchmark *benchmark,
				   int i, char **data)
{
	struct Options *options = benchmark->options;
	union Benchmark::State *payload_state = &benchmark->payload_states[i];
	switch (options->payload[i].type) {
	case FIELD_TYPE_UNSIGNED:
		*data = mp_encode_uint(*data, payload_state->u64++);
		break;
	default:
		box_error_raise(ER_PROC_LUA, "unsupported type (inc.gen)");
		return -1;
	}
	return 0;
}

static int
benchmark_next_payload_linear(struct Benchmark *benchmark, int i, char **data)
{
	struct Options *options = benchmark->options;
	switch (options->payload[i].type) {
	case FIELD_TYPE_UNSIGNED:
		*data = mp_encode_uint(*data, benchmark->rng());
		break;
	default:
		box_error_raise(ER_PROC_LUA, "unsupported type (lin.gen)");
		return -1;
	}
	return 0;
}

static int
benchmark_next_payload(struct Benchmark *benchmark, char **begin, char **end)
{
	struct Options *options = benchmark->options;
	char *data = benchmark->payload_buf;
	*begin = data;
	data = mp_encode_array(data, options->payload_size);
	int rc;
	for (int i = 0; i < options->payload_size; i++) {
		switch (options->payload[i].distribution) {
		case INCREMENTAL:
			rc = benchmark_next_payload_incremental(benchmark,
								i, &data);
			break;
		case LINEAR:
			rc = benchmark_next_payload_linear(benchmark, i, &data);
			break;
		case distribution_MAX:
			unreachable();
		}
		if (rc != 0)
			return -1;
	}
	*end = data;
	return 0;
}

static int
benchmark_parse_options(struct lua_State *L, int idx, struct Options *options)
{
	/* Get the request count. */
	lua_getfield(L, idx, "request_count");
	options->request_count =
		lua_isnil(L, -1) ? 1000000 : lua_tonumber(L, -1);
	lua_pop(L, 1);

	/* Get the test data format. */
	lua_getfield(L, idx, "payload");
	if (!lua_isnil(L, -1)) {
		options->payload_size = lua_objlen(L, -1);
		options->payload = (struct Payload *)box_region_aligned_alloc(
			alignof(struct Payload), options->payload_size);
		for (int i = 0; i < options->payload_size; i++) {
			/* payload[i] */
			lua_rawgeti(L, -1, i + 1);

			/* payload[i].type */
			lua_getfield(L, -1, "type");
			if (lua_isnil(L, -1)) {
				box_error_raise(ER_PROC_LUA,
						"field type must be specified");
				return -1;
			}
			const char *str = lua_tostring(L, -1);
			options->payload[i].type = STR2ENUM(field_type, str);
			if (options->payload[i].type == field_type_MAX) {
				box_error_raise(ER_PROC_LUA,
						"unknown field type");
				return -1;
			}
			lua_pop(L, 1);

			/* payload[i].distribution */
			lua_getfield(L, -1, "distribution");
			if (!lua_isnil(L, -1)) {
				const char *str = lua_tostring(L, -1);
				options->payload[i].distribution =
					STR2ENUM(distribution, str);
				if (options->payload[i].distribution ==
				    distribution_MAX) {
					box_error_raise(ER_PROC_LUA, "unknown"
							" distribution type");
					return -1;
				}
			} else {
				options->payload[i].distribution = LINEAR;
			}
			lua_pop(L, 1);

			/* payload[i] */
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	return 0;
}

static int
benchmark_replace_lua_func(struct lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1) ||
	    (lua_type(L, 2) != LUA_TTABLE))
		return luaL_error(L, "Usage replace(space_id, opts)");

	/* Get the space ID. */
	uint32_t space_id = lua_tonumber(L, 1);

	/* Create and fill the Options structure. */
	size_t region_svp = box_region_used();
	struct Options options;
	if (benchmark_parse_options(L, 2, &options) == -1) {
		box_region_truncate(region_svp);
		return luaT_error(L);
	}

	/* Initialize the payload generator. */
	struct Benchmark benchmark;
	if (benchmark_init(&benchmark, &options) != 0) {
		box_region_truncate(region_svp);
		return luaT_error(L);
	}

	/* Do the benchmark. */
	for (int i = 0; i < options.request_count; i++) {
		struct tuple *unused;
		char *data, *data_end;
		if (benchmark_next_payload(&benchmark, &data, &data_end) != 0 ||
		    box_replace(space_id, data, data_end, &unused) != 0) {
			box_region_truncate(region_svp);
			return luaT_error(L);
		}
	}

	/* Clean-up. */
	box_region_truncate(region_svp);
	return 0;
}

static int
benchmark_insert_lua_func(struct lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1) ||
	    (lua_type(L, 2) != LUA_TTABLE))
		return luaL_error(L, "Usage insert(space_id, opts)");

	/* Get the space ID. */
	uint32_t space_id = lua_tonumber(L, 1);

	/* Create and fill the Options structure. */
	size_t region_svp = box_region_used();
	struct Options options;
	if (benchmark_parse_options(L, 2, &options) != 0) {
		box_region_truncate(region_svp);
		return luaT_error(L);
	}

	/* Initialize the payload generator. */
	struct Benchmark benchmark;
	if (benchmark_init(&benchmark, &options) != 0) {
		box_region_truncate(region_svp);
		return luaT_error(L);
	}

	/* Do the benchmark. */
	for (int i = 0; i < options.request_count; i++) {
		struct tuple *unused;
		char *data, *data_end;
		if (benchmark_next_payload(&benchmark, &data, &data_end) != 0 ||
		    box_insert(space_id, data, data_end, &unused) != 0) {
			box_region_truncate(region_svp);
			return luaT_error(L);
		}
	}

	/* Clean-up. */
	box_region_truncate(region_svp);
	return 0;
}

static int
benchmark_delete_lua_func(struct lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    (lua_type(L, 3) != LUA_TTABLE))
		return luaL_error(L, "Usage delete(space_id, index_id, opts)");

	/* Get the space ID. */
	uint32_t space_id = lua_tonumber(L, 1);

	/* Get the index ID. */
	uint32_t index_id = lua_tonumber(L, 2);

	/* Create and fill the Options structure. */
	size_t region_svp = box_region_used();
	struct Options options;
	if (benchmark_parse_options(L, 3, &options) == -1) {
		box_region_truncate(region_svp);
		return luaT_error(L);
	}

	/* Initialize the payload generator. */
	struct Benchmark benchmark;
	if (benchmark_init(&benchmark, &options) != 0) {
		box_region_truncate(region_svp);
		return luaT_error(L);
	}

	/* Do the benchmark. */
	for (int i = 0; i < options.request_count; i++) {
		struct tuple *unused;
		char *data, *data_end;
		if (benchmark_next_payload(&benchmark, &data, &data_end) != 0 ||
		    box_delete(space_id, index_id, data,
			       data_end, &unused) != 0) {
			box_region_truncate(region_svp);
			return luaT_error(L);
		}
	}

	/* Clean-up. */
	box_region_truncate(region_svp);
	return 0;
}

static int
benchmark_get_lua_func(struct lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
	    (lua_type(L, 3) != LUA_TTABLE))
		return luaL_error(L, "Usage get(space_id, index_id, opts)");

	/* Get the space ID. */
	uint32_t space_id = lua_tonumber(L, 1);

	/* Get the index ID. */
	uint32_t index_id = lua_tonumber(L, 2);

	/* Create and fill the Options structure. */
	size_t region_svp = box_region_used();
	struct Options options;
	if (benchmark_parse_options(L, 3, &options) == -1) {
		box_region_truncate(region_svp);
		return luaT_error(L);
	}

	/* Initialize the payload generator. */
	struct Benchmark benchmark;
	if (benchmark_init(&benchmark, &options) != 0) {
		box_region_truncate(region_svp);
		return luaT_error(L);
	}

	/* Do the benchmark. */
	for (int i = 0; i < options.request_count; i++) {
		struct tuple *unused;
		char *key, *key_end;
		if (benchmark_next_payload(&benchmark, &key, &key_end) != 0) {
			box_region_truncate(region_svp);
			return luaT_error(L);
		}
		box_iterator_t *it = box_index_iterator(space_id, index_id,
							ITER_EQ, key, key_end);
		if (it == NULL) {
			box_region_truncate(region_svp);
			return luaT_error(L);
		}
		box_iterator_free(it);
	}

	/* Clean-up. */
	box_region_truncate(region_svp);
	return 0;
}

extern "C" int
luaopen_benchmark_box_module(struct lua_State *L)
{
	static const struct luaL_Reg lib[] = {
		{"replace", benchmark_replace_lua_func},
		{"insert", benchmark_insert_lua_func},
		{"delete", benchmark_delete_lua_func},
		{"get", benchmark_get_lua_func},
		{NULL, NULL}
	};
	luaL_register(L, "benchmark", lib);
	return 1;
}
