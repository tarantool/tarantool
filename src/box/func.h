#ifndef TARANTOOL_BOX_FUNC_H_INCLUDED
#define TARANTOOL_BOX_FUNC_H_INCLUDED
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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "small/rlist.h"
#include "func_def.h"
#include "user_def.h"
#include "module_cache.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct func;

/** Virtual method table for func object. */
struct func_vtab {
	/** Call function with given arguments. */
	int (*call)(struct func *func, struct port *args, struct port *ret);
	/** Release implementation-specific function context. */
	void (*destroy)(struct func *func);
};

/**
 * Stored function.
 */
struct func {
	struct func_def *def;
	/** Virtual method table. */
	const struct func_vtab *vtab;
	/**
	 * Authentication id of the owner of the function,
	 * used for set-user-id functions.
	 */
	struct credentials owner_credentials;
	/**
	 * Cached runtime access information.
	 */
	struct access access[BOX_USER_MAX];
};

/**
 * Initialize schema modules subsystem.
 */
int
schema_module_init(void);

/**
 * Cleanup schema modules subsystem.
 */
void
schema_module_free(void);

struct func *
func_new(struct func_def *def);

void
func_delete(struct func *func);

/**
 * Call function with arguments represented with given args.
 */
int
func_call(struct func *func, struct port *args, struct port *ret);

/**
 * Reload dynamically loadable schema module.
 *
 * @param package name begin pointer.
 * @param package_end package_end name end pointer.
 * @retval -1 on error.
 * @retval 0 on success.
 */
int
schema_module_reload(const char *package, const char *package_end);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_FUNC_H_INCLUDED */
