/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "lua/tracer.h"

#include <string.h>

#include "lua/utils.h"
#include "trivia/util.h"
#include "tt_uuid.h"
#include <tracer.h>

/** Generate a new random span id, 16 hex characters. */
static void
generate_span_id(char span_id[SPAN_ID_LEN])
{
	struct tt_uuid uuid;
	tt_uuid_create(&uuid);
	static const char hex[] = "0123456789abcdef";
	const unsigned char *bytes = (const unsigned char *)&uuid;
	for (int i = 0; i < (int)(SPAN_ID_LEN / 2); i++) {
		span_id[i * 2] = hex[bytes[i] >> 4];
		span_id[i * 2 + 1] = hex[bytes[i] & 0xf];
	}
}

/** Create new span
 * name - name of span
 * traceparent - information about parent span
 */
static int
lbox_tracer_start_span(struct lua_State *L)
{
	const char *name = luaT_checkstring(L, 1);
	size_t traceparent_len;
	const char *traceparent_arg = luaT_checklstring(L, 2, &traceparent_len);
	char traceparent[TRACEPARENT_LEN];
	if (traceparent_len != sizeof(traceparent))
		return luaL_error(L, "invalid traceparent length");
	memcpy(traceparent, traceparent_arg, sizeof(traceparent));
	char span_id[SPAN_ID_LEN];
	generate_span_id(span_id);

	struct span *span = xmalloc(sizeof(*span));
	start_span(span, name, span_id, traceparent);
	lua_pushlightuserdata(L, span);
	return 1;
}

/** Set finish time to span. */
static int
lbox_tracer_end_span(struct lua_State *L)
{
	struct span *span = (struct span *)lua_touserdata(L, 1);
	end_span(span);
	return 0;
}

/** Record information about span. */
static int
lbox_tracer_write_span(struct lua_State *L)
{
	struct span *span = (struct span *)lua_touserdata(L, 1);
	write_span(span);
	return 0;
}

/** Free memory of span. */
static int
lbox_tracer_destroy_span(struct lua_State *L)
{
	struct span *span = (struct span *)lua_touserdata(L, 1);
	free(span->name);
	free(span);
	return 0;
}

/** Get id of span. */
static int
lbox_tracer_span_id(struct lua_State *L)
{
	struct span *span = (struct span *)lua_touserdata(L, 1);
	lua_pushlstring(L, span->span_id, sizeof(span->span_id));
	return 1;
}

static const struct luaL_Reg tracer_lib[] = {
	{"start_span", lbox_tracer_start_span},
	{"end_span", lbox_tracer_end_span},
	{"get_id", lbox_tracer_span_id},
	{"write_span", lbox_tracer_write_span},
	{"destroy_span", lbox_tracer_destroy_span},
	{NULL, NULL}
};

void
tarantool_lua_tracer_init(struct lua_State *L)
{
	luaT_newmodule(L, "tracer.lib", tracer_lib);
	lua_pop(L, 1);
}
