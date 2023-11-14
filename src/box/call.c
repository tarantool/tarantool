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

#include "box/call.h"
#include "lua/call.h"
#include "../lua/init.h" /* tarantool_lua_is_builtin_global */
#include "schema.h"
#include "session.h"
#include "func.h"
#include "port.h"
#include "box.h"
#include "txn.h"
#include "xrow.h"
#include "iproto_constants.h"
#include "rmean.h"
#include "small/obuf.h"
#include "small/rlist.h"
#include "tt_static.h"
#include "box/mp_box_ctx.h"

struct rlist box_on_call = RLIST_HEAD_INITIALIZER(box_on_call);

static const struct port_vtab port_msgpack_vtab;

void
port_msgpack_create_with_ctx(struct port *base, const char *data,
			     uint32_t data_sz, struct mp_ctx *ctx)
{
	struct port_msgpack *port_msgpack = (struct port_msgpack *) base;
	memset(port_msgpack, 0, sizeof(*port_msgpack));
	port_msgpack->vtab = &port_msgpack_vtab;
	port_msgpack->data = data;
	port_msgpack->data_sz = data_sz;
	port_msgpack->ctx = ctx;
}

static const char *
port_msgpack_get_msgpack(struct port *base, uint32_t *size)
{
	struct port_msgpack *port = (struct port_msgpack *) base;
	assert(port->vtab == &port_msgpack_vtab);
	*size = port->data_sz;
	return port->data;
}

static int
port_msgpack_dump_msgpack(struct port *base, struct obuf *out,
			  struct mp_ctx *ctx)
{
	(void)ctx;
	struct port_msgpack *port = (struct port_msgpack *)base;
	assert(port->vtab == &port_msgpack_vtab);
	size_t size = port->data_sz;
	if (obuf_dup(out, port->data, size) == size)
		return 0;
	diag_set(OutOfMemory, size, "obuf_dup", "port->data");
	return -1;
}

extern void
port_msgpack_dump_lua(struct port *base, struct lua_State *L,
		      enum port_dump_lua_mode mode);

extern const char *
port_msgpack_dump_plain(struct port *base, uint32_t *size);

void
port_msgpack_destroy(struct port *base)
{
	struct port_msgpack *port = (struct port_msgpack *)base;
	assert(port->vtab == &port_msgpack_vtab);
	free(port->plain);
	if (port->ctx != NULL)
		mp_ctx_destroy(port->ctx);
}

int
port_msgpack_set_plain(struct port *base, const char *plain, uint32_t len)
{
	struct port_msgpack *port = (struct port_msgpack *)base;
	assert(port->plain == NULL);
	port->plain = (char *)malloc(len + 1);
	if (port->plain == NULL) {
		diag_set(OutOfMemory, len + 1, "malloc", "port->plain");
		return -1;
	}
	memcpy(port->plain, plain, len);
	port->plain[len] = 0;
	return 0;
}

static const struct port_vtab port_msgpack_vtab = {
	.dump_msgpack = port_msgpack_dump_msgpack,
	.dump_msgpack_16 = NULL,
	.dump_lua = port_msgpack_dump_lua,
	.dump_plain = port_msgpack_dump_plain,
	.get_msgpack = port_msgpack_get_msgpack,
	.get_vdbemem = NULL,
	.destroy = port_msgpack_destroy,
};

int
box_module_reload(const char *name)
{
	struct credentials *credentials = effective_user();
	if ((credentials->universal_access & (PRIV_X | PRIV_U)) !=
	    (PRIV_X | PRIV_U)) {
		struct user *user = user_find(credentials->uid);
		if (user != NULL)
			diag_set(AccessDeniedError, priv_name(PRIV_U),
				 schema_object_name(SC_UNIVERSE), "",
				 user->def->name);
		return -1;
	}
	return schema_module_reload(name, name + strlen(name));
}

/** Runs box_on_call triggers. */
static inline void
box_run_on_call(enum iproto_type type, const char *expr, int expr_len,
		const char *args)
{
	assert(type == IPROTO_CALL || type == IPROTO_EVAL);
	if (likely(rlist_empty(&box_on_call)))
		return;
	struct box_on_call_ctx ctx = {
		.is_eval = (type == IPROTO_EVAL),
		.expr = expr,
		.expr_len = expr_len,
		.args = args,
	};
	trigger_run(&box_on_call, &ctx);
}

int
access_check_lua_call(const char *name, uint32_t name_len)
{
	struct credentials *cr = effective_user();
	user_access_t access = PRIV_X | PRIV_U;
	access &= ~cr->universal_access;
	if (access == 0)
		return 0;
	access &= ~universe.access_lua_call[cr->auth_token].effective;
	if (access == 0 && !tarantool_lua_is_builtin_global(name, name_len))
		return 0;
	struct user *user = user_find(cr->uid);
	if (user != NULL)
		diag_set(AccessDeniedError, priv_name(PRIV_X),
			 schema_object_name(SC_FUNCTION),
			 tt_cstr(name, name_len), user->def->name);
	return -1;
}

/** Checks if the current user may execute an arbitrary Lua expression. */
static int
access_check_lua_eval(void)
{
	struct credentials *cr = effective_user();
	user_access_t access = PRIV_X | PRIV_U;
	access &= ~cr->universal_access;
	if (access == 0)
		return 0;
	access &= ~universe.access_lua_eval[cr->auth_token].effective;
	if (access == 0)
		return 0;
	struct user *user = user_find(cr->uid);
	if (user != NULL)
		diag_set(AccessDeniedError, priv_name(PRIV_X),
			 schema_object_name(SC_UNIVERSE), "", user->def->name);
	return -1;
}

int
box_process_call(struct call_request *request, struct port *port)
{
	rmean_collect(rmean_box, IPROTO_CALL, 1);
	/**
	 * Find the function definition and check access.
	 */
	const char *name = request->name;
	assert(name != NULL);
	uint32_t name_len = mp_decode_strl(&name);
	struct mp_box_ctx ctx;
	if (mp_box_ctx_create(&ctx, NULL, request->tuple_formats) != 0)
		return -1;
	struct port args;
	port_msgpack_create_with_ctx(&args, request->args,
				     request->args_end - request->args,
				     (struct mp_ctx *)&ctx);
	struct func *func = func_by_name(name, name_len);
	int rc = 0;
	if (func != NULL) {
		if (func_access_check(func) != 0) {
			rc = -1;
			goto cleanup;
		}
		box_run_on_call(IPROTO_CALL, name, name_len, request->args);
		if (func_call_no_access_check(func, &args, port) != 0) {
			rc = -1;
			goto cleanup;
		}
	} else {
		if (access_check_lua_call(name, name_len) != 0) {
			rc = -1;
			goto cleanup;
		}
		box_run_on_call(IPROTO_CALL, name, name_len, request->args);
		if (box_lua_call(name, name_len, &args, port) != 0) {
			rc = -1;
			goto cleanup;
		}
	}
cleanup:
	port_msgpack_destroy(&args);
	return rc;
}

int
box_process_eval(struct call_request *request, struct port *port)
{
	rmean_collect(rmean_box, IPROTO_EVAL, 1);
	/* Check permissions */
	if (access_check_lua_eval() != 0)
		return -1;
	struct mp_box_ctx ctx;
	if (mp_box_ctx_create(&ctx, NULL, request->tuple_formats) != 0)
		return -1;
	struct port args;
	port_msgpack_create_with_ctx(&args, request->args,
				     request->args_end - request->args,
				     (struct mp_ctx *)&ctx);
	const char *expr = request->expr;
	uint32_t expr_len = mp_decode_strl(&expr);
	box_run_on_call(IPROTO_EVAL, expr, expr_len, request->args);
	int rc = box_lua_eval(expr, expr_len, &args, port);
	port_msgpack_destroy(&args);
	return rc;
}
