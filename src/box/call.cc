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
#include "schema.h"
#include "session.h"
#include "func.h"
#include "port.h"
#include "scoped_guard.h"
#include "box.h"
#include "txn.h"
#include "xrow.h"
#include "iproto_constants.h"
#include "rmean.h"
#include "small/obuf.h"

static inline struct func *
access_check_func(const char *name, uint32_t name_len)
{
	struct func *func = func_by_name(name, name_len);
	struct credentials *credentials = current_user();
	/*
	 * If the user has universal access, don't bother with checks.
	 * No special check for ADMIN user is necessary
	 * since ADMIN has universal access.
	 */
	if ((credentials->universal_access & PRIV_ALL) == PRIV_ALL)
		return func;
	uint8_t access = PRIV_X & ~credentials->universal_access;
	if (func == NULL || (func->def->uid != credentials->uid &&
	     access & ~func->access[credentials->auth_token].effective)) {
		/* Access violation, report error. */
		struct user *user = user_find_xc(credentials->uid);
		tnt_raise(ClientError, ER_FUNCTION_ACCESS_DENIED,
			  priv_name(access), user->def->name,
			  tt_cstr(name, name_len));
	}

	return func;
}

static int
func_call(struct func *func, struct request *request, struct obuf *out)
{
	assert(func != NULL && func->def->language == FUNC_LANGUAGE_C);
	if (func->func == NULL)
		func_load(func);

	/* Create a call context */
	struct port port;
	port_create(&port);
	auto port_guard = make_scoped_guard([&](){ port_destroy(&port); });
	box_function_ctx_t ctx = { request, &port };

	/* Clear all previous errors */
	diag_clear(&fiber()->diag);
	assert(!in_txn()); /* transaction is not started */
	/* Call function from the shared library */
	int rc = func->func(&ctx, request->tuple, request->tuple_end);
	if (rc != 0) {
		if (diag_last_error(&fiber()->diag) == NULL) {
			/* Stored procedure forget to set diag  */
			diag_set(ClientError, ER_PROC_C, "unknown error");
		}
		goto error;
	}

	/* Push results to obuf */
	struct obuf_svp svp;
	if (iproto_prepare_select(out, &svp) != 0)
		goto error;

	if (request->type == IPROTO_CALL_16) {
		/* Tarantool < 1.7.1 compatibility */
		if (port_dump(&port, out) != 0) {
			obuf_rollback_to_svp(out, &svp);
			goto error;
		}
		iproto_reply_select(out, &svp, request->header->sync,
				    ::schema_version, port.size);
	} else {
		assert(request->type == IPROTO_CALL);
		char *size_buf = (char *)
			obuf_alloc(out, mp_sizeof_array(port.size));
		if (size_buf == NULL)
			goto error;
		mp_encode_array(size_buf, port.size);
		if (port_dump(&port, out) != 0) {
			obuf_rollback_to_svp(out, &svp);
			goto error;
		}
		iproto_reply_select(out, &svp, request->header->sync,
				    ::schema_version, 1);
	}

	return 0;

error:
	txn_rollback();
	return -1;
}

void
box_process_call(struct request *request, struct obuf *out)
{
	rmean_collect(rmean_box, IPROTO_CALL, 1);
	/**
	 * Find the function definition and check access.
	 */
	const char *name = request->key;
	uint32_t name_len = mp_decode_strl(&name);
	struct func *func = access_check_func(name, name_len);
	/*
	 * Sic: func == NULL means that perhaps the user has a global
	 * "EXECUTE" privilege, so no specific grant to a function.
	 */

	/**
	 * Change the current user id if the function is
	 * a set-definer-uid one. If the function is not
	 * defined, it's obviously not a setuid one.
	 */
	struct credentials *orig_credentials = NULL;
	if (func && func->def->setuid) {
		orig_credentials = current_user();
		/* Remember and change the current user id. */
		if (func->owner_credentials.auth_token >= BOX_USER_MAX) {
			/*
			 * Fill the cache upon first access, since
			 * when func is created, no user may
			 * be around to fill it (recovery of
			 * system spaces from a snapshot).
			 */
			struct user *owner = user_find_xc(func->def->uid);
			credentials_init(&func->owner_credentials,
					 owner->auth_token,
					 owner->def->uid);
		}
		fiber_set_user(fiber(), &func->owner_credentials);
	}

	int rc;
	if (func && func->def->language == FUNC_LANGUAGE_C) {
		rc = func_call(func, request, out);
	} else {
		rc = box_lua_call(request, out);
	}
	/* Restore the original user */
	if (orig_credentials)
		fiber_set_user(fiber(), orig_credentials);

	if (rc != 0) {
		txn_rollback();
		diag_raise();
	}

	if (in_txn()) {
		/* The procedure forgot to call box.commit() */
		say_warn("a transaction is active at return from '%.*s'",
			name_len, name);
		txn_rollback();
	}
}

void
box_process_eval(struct request *request, struct obuf *out)
{
	rmean_collect(rmean_box, IPROTO_EVAL, 1);
	/* Check permissions */
	access_check_universe(PRIV_X);
	if (box_lua_eval(request, out) != 0) {
		txn_rollback();
		diag_raise();
	}

	if (in_txn()) {
		/* The procedure forgot to call box.commit() */
		const char *expr = request->key;
		uint32_t expr_len = mp_decode_strl(&expr);
		say_warn("a transaction is active at return from EVAL '%.*s'",
			expr_len, expr);
		txn_rollback();
	}
}
