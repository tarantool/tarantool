#ifndef INCLUDES_TARANTOOL_MOD_BOX_CALL_H
#define INCLUDES_TARANTOOL_MOD_BOX_CALL_H
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

#include <stdbool.h>

#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct port;
struct call_request;

/** Context passed to box_on_call trigger callback. */
struct box_on_call_ctx {
	/** True for EVAL, false for CALL. */
	bool is_eval;
	/** CALL function name or EVAL expression. */
	const char *expr;
	/** Length of the expr string. */
	int expr_len;
	/** Arguments (MsgPack array). */
	const char *args;
};

/**
 * Triggers invoked by box_process_call and box_process_eval.
 * Trigger callback is passed on_box_call_ctx.
 */
extern struct rlist box_on_call;

/**
 * Reload loadable module by name.
 *
 * @param name of the module to reload.
 * @retval -1 on error.
 * @retval 0 on success.
 */
int
box_module_reload(const char *name);

int
box_process_call(struct call_request *request, struct port *port);

int
box_process_eval(struct call_request *request, struct port *port);

/** Checks if the current user may execute a global Lua function. */
int
access_check_lua_call(const char *name, uint32_t name_len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_MOD_BOX_CALL_H */
