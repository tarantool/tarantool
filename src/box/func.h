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
#include "func_cache.h"
#include "user_def.h"
#include "module_cache.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct func;
struct func_adapter;

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
	/** List of func holders. This member is a property of func cache. */
	struct rlist func_cache_pin_list;
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
void
schema_module_init(void);

/**
 * Cleanup schema modules subsystem.
 */
void
schema_module_free(void);

/**
 * Allocates and initializes a function, given a function definition.
 * Returns the new function on success. On error, returns NULL and sets diag.
 * Note, this function copies the given function definition so it may be safely
 * freed after calling this function.
 */
struct func *
func_new(const struct func_def *def);

/** Frees a function object. */
void
func_delete(struct func *func);

/** Check "EXECUTE" permissions for a given function. */
int
func_access_check(struct func *func);

/**
 * Call function @a func with arguments @a args, put return value to @a ret.
 * Return 0 on success and nonzero on failure.
 * The port @a args must be initialized by the caller while @a ret is
 * initialized by func_call, and only in case of success.
 * Thus the caller must not initialize @a ret by himself and must destroy it
 * if and only if func_call returns 0;
 */
int
func_call_no_access_check(struct func *func, struct port *args,
			  struct port *ret);

static inline int
func_call(struct func *func, struct port *args, struct port *ret)
{
	if (func_access_check(func) != 0)
		return -1;
	return func_call_no_access_check(func, args, ret);
}

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

/**
 * Creates func adapter for persistent function, never fails.
 * Underlying function is pinned with holder of passed type,
 * so it must be in func_cache while the func_adapter is alive.
 */
struct func_adapter *
func_adapter_func_create(struct func *func, enum func_holder_type type);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_FUNC_H_INCLUDED */
