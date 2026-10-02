/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

#include "lua/tracer_opentelemetry.h"
#include "lua/utils.h"
#include "lua/fiber.h"

#include <fiber.h>
#include <tracer_opentelemetry.h>

#include <stdlib.h>
#include <string.h>

/** Generate a new random span id, 16 hex characters. */
static int
generate_span_id(char span_id[16])
{
	static char hex[] = "0000000000000000";
	int i;
	for (i = 15; i >= 0; i--) {
		if (hex[i] != 'f')
			break;
	}
	if (i < 0)
		return -1; /** The sequences have ended. */
	hex[i] += 1;
	if (hex[i] == '9')
		hex[i] = 'a';
	for (int j = i + 1; j < 16; j++)
		hex[j] = '0';
	memcpy(span_id, hex, 16);
	return 0;
}

/**
 * Start a span around a function call, running the function on a
 * new fiber.
 * Called as tracer:call_span(name, function, ...): takes the tracer
 * itself (`self`), the span name, the function to call, and the
 * arguments to pass to it.
 */
static int
lbox_tracer_opentelemetry_call_span(struct lua_State *L)
{
	int top = lua_gettop(L);
	if (top < 3 || !lua_isstring(L, 2) || !lua_isfunction(L, 3)) {
		diag_set(IllegalParams,
			 "tracer.call_span(name, function, "
			 "...): bad arguments");
		luaT_error(L);
	}
	const char *name = lua_tostring(L, 2);
	lua_remove(L, 1); /* Drop tracer: not an argument of `function`. */
	lua_remove(L, 1); /* Drop name: the rest is `function, ...`. */

	if (trace_opentelemetry_get_enabled()) {
		struct fiber *f = fiber();
		bool has_parent =
			!f->storage.propagation_context.is_traceparent_null;

		char span_id[16];
		generate_span_id(span_id);
		char parent_traceparent[55];
		if (has_parent) {
			memcpy(parent_traceparent,
			       f->storage.propagation_context.traceparent, 55);
			memcpy(parent_traceparent + 36,
			       f->storage.propagation_context.span_id, 16);
		} else {
			/* No parent span: this call starts a new trace. */
			memset(parent_traceparent, '0',
			       sizeof(parent_traceparent));
			parent_traceparent[2] = '-';
			parent_traceparent[35] = '-';
			parent_traceparent[52] = '-';
			parent_traceparent[53] = '0';
			parent_traceparent[54] = '1';
		}
		struct fiber *f_new = fiber_create(L);
		if (f_new == NULL) {
			return -1;
		}
		fiber_set_joinable(f_new, true);
		f_new->storage.propagation_context.is_traceparent_null = false;
		memcpy(f_new->storage.propagation_context.span_id, span_id, 16);
		memcpy(f_new->storage.propagation_context.traceparent,
		       parent_traceparent, 55);
		struct span_opentelemetry *span = xmalloc(sizeof(*span));
		span_start(span, name, span_id, parent_traceparent, INTERNAL);
		fiber_start(f_new);
		fiber_join(f_new);
		span_end(span);
		free(span);
	} else {
		struct fiber *f_new = fiber_create(L);
		fiber_set_joinable(f_new, true);
		fiber_start(f_new);
		fiber_join(f_new);
	}
	return 0;
}

static const struct luaL_Reg tracer_lib[] = {
	{"call_span", lbox_tracer_opentelemetry_call_span},
	{NULL, NULL}
};

void
tarantool_lua_tracer_opentelemetry_init(struct lua_State *L)
{
	luaT_newmodule(L, "tracer", tracer_lib);
	lua_pop(L, 1);
}
