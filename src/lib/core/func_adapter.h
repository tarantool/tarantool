/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "trivia/util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct port;
struct func_adapter;

/**
 * Virtual table for func_adapter class.
 */
struct func_adapter_vtab {
	/**
	 * Calls the underlying function.
	 * Both `args` and `ret` ports can be NULL.
	 * If port `args` is not NULL, it is dumped and the values
	 * are passed to the function as arguments. Otherwise, the
	 * function is called without arguments.
	 * If port `ret` is not NULL, it is guaranteed to be initialized
	 * in the case of success, even if the function returned nothing,
	 * so in this case caller must destroy it. If the port is not
	 * NULL, but the function returned an error, the port is not
	 * initialized. If the port is NULL, all returned values of
	 * the function are ignored.
	 */
	int (*call)(struct func_adapter *func, struct port *args,
		    struct port *ret);
	/**
	 * Virtual destructor of the class.
	 */
	void (*destroy)(struct func_adapter *func);
};

/**
 * Base class for all function adapters. Instance of this class should not
 * be created.
 */
struct func_adapter {
	/**
	 * Virtual table.
	 */
	const struct func_adapter_vtab *vtab;
};

static inline int
func_adapter_call(struct func_adapter *func, struct port *args, struct port *ret)
{
	return func->vtab->call(func, args, ret);
}

static inline void
func_adapter_destroy(struct func_adapter *func)
{
	func->vtab->destroy(func);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
