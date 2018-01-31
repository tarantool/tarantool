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
#include "box.h"
#include "txn.h"
#include "xrow.h"
#include "iproto_constants.h"
#include "rmean.h"
#include "small/obuf.h"

/**
 * Find a function by name and check "EXECUTE" permissions.
 *
 * @param name function name
 * @param name_len length of @a name
 * @param[out] funcp function object
 * Sic: *pfunc == NULL means that perhaps the user has a global
 * "EXECUTE" privilege, so no specific grant to a function.
 *
 * @retval -1 on access denied
 * @retval  0 on success
 */
static inline int
access_check_func(const char *name, uint32_t name_len, struct func **funcp)
{
	struct func *func = func_by_name(name, name_len);
	struct credentials *credentials = effective_user();
	/*
	 * If the user has universal access, don't bother with checks.
	 * No special check for ADMIN user is necessary
	 * since ADMIN has universal access.
	 */
	if ((credentials->universal_access & (PRIV_X | PRIV_U)) ==
	    (PRIV_X | PRIV_U)) {

		*funcp = func;
		return 0;
	}
	user_access_t access = PRIV_X | PRIV_U;
	user_access_t func_access = access & ~credentials->universal_access;
	if (func == NULL ||
	    /* Check for missing Usage access, ignore owner rights. */
	    func_access & PRIV_U ||
	    /* Check for missing specific access, respect owner rights. */
	    (func->def->uid != credentials->uid &&
	    func_access & ~func->access[credentials->auth_token].effective)) {

		/* Access violation, report error. */
		struct user *user = user_find(credentials->uid);
		if (user != NULL) {
			if (!(access & credentials->universal_access)) {
				diag_set(AccessDeniedError,
					 priv_name(PRIV_U),
					 schema_object_name(SC_UNIVERSE), "",
					 user->def->name);
			} else {
				diag_set(AccessDeniedError,
					 priv_name(PRIV_X),
					 schema_object_name(SC_FUNCTION),
					 tt_cstr(name, name_len),
					 user->def->name);
			}
		}
		return -1;
	}

	*funcp = func;
	return 0;
}

static int
box_c_call(struct func *func, struct call_request *request, struct port *port)
{
	assert(func != NULL && func->def->language == FUNC_LANGUAGE_C);

	/* Create a call context */
	port_tuple_create(port);
	box_function_ctx_t ctx = { port };

	/* Clear all previous errors */
	diag_clear(&fiber()->diag);
	assert(!in_txn()); /* transaction is not started */

	/* Call function from the shared library */
	int rc = func_call(func, &ctx, request->args, request->args_end);
	func = NULL; /* May be deleted by DDL */
	if (rc != 0) {
		if (diag_last_error(&fiber()->diag) == NULL) {
			/* Stored procedure forget to set diag  */
			diag_set(ClientError, ER_PROC_C, "unknown error");
		}
		port_destroy(port);
		return -1;
	}
	return 0;
}

int
box_func_reload(const char *name)
{
	size_t name_len = strlen(name);
	struct func *func = NULL;
	if ((access_check_func(name, name_len, &func)) != 0)
		return -1;
	if (func == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FUNCTION, name);
		return -1;
	}
	if (func->def->language != FUNC_LANGUAGE_C || func->func == NULL)
		return 0; /* Nothing to do */
	if (func_reload(func) == 0)
		return 0;
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

	struct func *func = NULL;
	/**
	 * Sic: func == NULL means that perhaps the user has a global
	 * "EXECUTE" privilege, so no specific grant to a function.
	 */
	if (access_check_func(name, name_len, &func) != 0)
		return -1; /* permission denied */

	/**
	 * Change the current user id if the function is
	 * a set-definer-uid one. If the function is not
	 * defined, it's obviously not a setuid one.
	 */
	struct credentials *orig_credentials = NULL;
	if (func && func->def->setuid) {
		orig_credentials = effective_user();
		/* Remember and change the current user id. */
		if (func->owner_credentials.auth_token >= BOX_USER_MAX) {
			/*
			 * Fill the cache upon first access, since
			 * when func is created, no user may
			 * be around to fill it (recovery of
			 * system spaces from a snapshot).
			 */
			struct user *owner = user_find(func->def->uid);
			if (owner == NULL)
				return -1;
			credentials_init(&func->owner_credentials,
					 owner->auth_token,
					 owner->def->uid);
		}
		fiber_set_user(fiber(), &func->owner_credentials);
	}

	int rc;
	if (func && func->def->language == FUNC_LANGUAGE_C) {
		rc = box_c_call(func, request, port);
	} else {
		rc = box_lua_call(request, port);
	}
	/* Restore the original user */
	if (orig_credentials)
		fiber_set_user(fiber(), orig_credentials);

	if (rc != 0) {
		txn_rollback();
		return -1;
	}

	if (in_txn()) {
		diag_set(ClientError, ER_FUNCTION_TX_ACTIVE);
		txn_rollback();
		return -1;
	}

	return 0;
}

int
box_process_eval(struct call_request *request, struct port *port)
{
	rmean_collect(rmean_box, IPROTO_EVAL, 1);
	/* Check permissions */
	if (access_check_universe(PRIV_X) != 0)
		return -1;
	if (box_lua_eval(request, port) != 0) {
		txn_rollback();
		return -1;
	}

	if (in_txn()) {
		diag_set(ClientError, ER_FUNCTION_TX_ACTIVE);
		txn_rollback();
		return -1;
	}

	return 0;
}
