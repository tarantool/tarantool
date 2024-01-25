/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/port.h"
#include "box/func.h"
#include "box/func_cache.h"

#include "core/func_adapter.h"

/**
 * Specialization of func_adapter for persistent functions.
 */
struct func_adapter_func {
	/**
	 * Virtual table.
	 */
	const struct func_adapter_vtab *vtab;
	/**
	 * Reference to the function itself. Must be in func_cache. Is pinned.
	 */
	struct func *func;
	/**
	 * A holder for underlying func.
	 */
	struct func_cache_holder holder;
};

/**
 * Call the function with ports. Access check is not performed.
 */
static int
func_adapter_func_call(struct func_adapter *base, struct port *args, struct port *ret)
{
	struct func_adapter_func *func = (struct func_adapter_func *)base;

	/* Create and use local ports if passed ones are NULL. */
	struct port args_local, ret_local;
	if (args == NULL) {
		port_c_create(&args_local);
		args = &args_local;
	}
	if (ret == NULL)
		ret = &ret_local;

	int rc = func_call_no_access_check(func->func, args, ret);

	/* Destroy local ports if they were used. */
	if (args == &args_local)
		port_destroy(args);
	/* Port with returned values is initialized only on success. */
	if (rc == 0 && ret == &ret_local)
		port_destroy(ret);
	return rc;
}

/**
 * Virtual destructor.
 */
static void
func_adapter_func_destroy(struct func_adapter *func_base)
{
	struct func_adapter_func *func = (struct func_adapter_func *)func_base;
	func_unpin(&func->holder);
	free(func);
}

struct func_adapter *
func_adapter_func_create(struct func *pfunc, enum func_holder_type type)
{
	static const struct func_adapter_vtab vtab = {
		.call = func_adapter_func_call,
		.destroy = func_adapter_func_destroy,
	};
	struct func_adapter_func *func = xmalloc(sizeof(*func));
	func->func = pfunc;
	func->vtab = &vtab;
	func_pin(pfunc, &func->holder, type);
	return (struct func_adapter *)func;
}
