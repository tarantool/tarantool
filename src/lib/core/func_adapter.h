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

struct tuple;

#define FUNC_ADAPTER_CTX_SIZE 24

/**
 * Abstract func_adapter_ctx instance. It is supposed to be converted
 * to a concrete realization, which must not occupy more memory than this
 * instance.
 */
struct func_adapter_ctx {
	/**
	 * Padding to achieve required size.
	 */
	char pad[FUNC_ADAPTER_CTX_SIZE];
};

struct func_adapter;

/**
 * Virtual table for func_adapter class.
 * Usage of these methods is in the description of func_adapter class.
 */
struct func_adapter_vtab {
	/**
	 * Prepares for call of the function. One must allocate func_adapter_ctx
	 * (using stack or heap) and pass it to this function to initialize it.
	 * After this call, all the arguments must be pushed in direct order.
	 */
	void (*begin)(struct func_adapter *func, struct func_adapter_ctx *ctx);
	/**
	 * Calls the function. All the arguments must be pushed before.
	 */
	int (*call)(struct func_adapter_ctx *ctx);
	/**
	 * Releases all the resources occupied by context. It must not be used
	 * after this method was called, so all required arguments must be
	 * popped before.
	 * Must be called even in the case of fail.
	 */
	void (*end)(struct func_adapter_ctx *ctx);
	/**
	 * Pushes tuple argument.
	 */
	void (*push_tuple)(struct func_adapter_ctx *ctx, struct tuple *tuple);
	/**
	 * Pushes double argument.
	 */
	void (*push_double)(struct func_adapter_ctx *ctx, double value);
	/**
	 * Pushes string argument.
	 */
	void (*push_str)(struct func_adapter_ctx *ctx, const char *str,
			 size_t len);
	/**
	 * Pushes null argument.
	 */
	void (*push_null)(struct func_adapter_ctx *ctx);
	/**
	 * Checks if the next returned value is a tuple.
	 */
	bool (*is_tuple)(struct func_adapter_ctx *ctx);
	/**
	 * Pops tuple. Returned tuple is referenced by the function and caller
	 * must unreference it. Never returns NULL.
	 */
	void (*pop_tuple)(struct func_adapter_ctx *ctx, struct tuple **tuple);
	/**
	 * Checks if the next returned value is a number that can be represented
	 * by double without loss of precision.
	 */
	bool (*is_double)(struct func_adapter_ctx *ctx);
	/**
	 * Pops double value.
	 */
	void (*pop_double)(struct func_adapter_ctx *ctx, double *number);
	/**
	 * Checks if the next returned value is a string.
	 */
	bool (*is_str)(struct func_adapter_ctx *ctx);
	/**
	 * Pops string value. Argument len is allowed to be NULL.
	 * Never return NULL.
	 */
	void (*pop_str)(struct func_adapter_ctx *ctx, const char **str,
			size_t *len);
	/**
	 * Checks if the next returned value is null or nothing.
	 */
	bool (*is_null)(struct func_adapter_ctx *ctx);
	/**
	 * Pops null.
	 */
	void (*pop_null)(struct func_adapter_ctx *ctx);
	/**
	 * Virtual destructor of the class.
	 */
	void (*destroy)(struct func_adapter *func);
};

/**
 * Base class for all function adapters. Instance of this class should not
 * be created.
 * Function is called in several stages:
 * 1. Preparation - func_adapter_ctx instance is allocated and initialized by
 *    begin method. Then, all the arguments are pushed in direct order (the
 *    first argument is pushed first). Pop methods must not be called at this
 *    stage.
 * 2. Call - the actual function call. If the call was not successful, it sets
 *    diag and returns -1. In the case of error, one must stop the calling
 *    process and call method end to release occupied resources.
 * 3. Finalization - returned values are popped in direct order (first returned
 *    value will be popped first). When popping a value of a particular type,
 *    one must be sure that the next value has this type. It is not necessary to
 *    pop all the returned values. When all the returned values are popped, all
 *    the next values will be nulls. After all, method end must be called.
 */
struct func_adapter {
	/**
	 * Virtual table.
	 */
	const struct func_adapter_vtab *vtab;
};

static inline void
func_adapter_begin(struct func_adapter *func, struct func_adapter_ctx *ctx)
{
	func->vtab->begin(func, ctx);
}

static inline void
func_adapter_end(struct func_adapter *func, struct func_adapter_ctx *ctx)
{
	func->vtab->end(ctx);
	TRASH(ctx);
}

static inline int
func_adapter_call(struct func_adapter *func, struct func_adapter_ctx *ctx)
{
	return func->vtab->call(ctx);
}

static inline void
func_adapter_push_double(struct func_adapter *func,
			 struct func_adapter_ctx *ctx, double val)
{
	func->vtab->push_double(ctx, val);
}

static inline void
func_adapter_push_str(struct func_adapter *func, struct func_adapter_ctx *ctx,
		      const char *str, size_t len)
{
	func->vtab->push_str(ctx, str, len);
}

/**
 * Pushes zero-terimnated string.
 */
static inline void
func_adapter_push_str0(struct func_adapter *func, struct func_adapter_ctx *ctx,
		       const char *str)
{
	func->vtab->push_str(ctx, str, strlen(str));
}

static inline void
func_adapter_push_tuple(struct func_adapter *func,
			struct func_adapter_ctx *ctx, struct tuple *tuple)
{
	func->vtab->push_tuple(ctx, tuple);
}

static inline void
func_adapter_push_null(struct func_adapter *func, struct func_adapter_ctx *ctx)
{
	func->vtab->push_null(ctx);
}

static inline bool
func_adapter_is_double(struct func_adapter *func, struct func_adapter_ctx *ctx)
{
	return func->vtab->is_double(ctx);
}

static inline void
func_adapter_pop_double(struct func_adapter *func,
			struct func_adapter_ctx *ctx, double *out)
{
	assert(func_adapter_is_double(func, ctx));
	func->vtab->pop_double(ctx, out);
}

static inline bool
func_adapter_is_str(struct func_adapter *func, struct func_adapter_ctx *ctx)
{
	return func->vtab->is_str(ctx);
}

static inline void
func_adapter_pop_str(struct func_adapter *func, struct func_adapter_ctx *ctx,
		     const char **str, size_t *len)
{
	assert(func_adapter_is_str(func, ctx));
	func->vtab->pop_str(ctx, str, len);
}

static inline bool
func_adapter_is_tuple(struct func_adapter *func, struct func_adapter_ctx *ctx)
{
	return func->vtab->is_tuple(ctx);
}

static inline void
func_adapter_pop_tuple(struct func_adapter *func,
		       struct func_adapter_ctx *ctx, struct tuple **tuple)
{
	assert(func_adapter_is_tuple(func, ctx));
	func->vtab->pop_tuple(ctx, tuple);
}

static inline bool
func_adapter_is_null(struct func_adapter *func, struct func_adapter_ctx *ctx)
{
	return func->vtab->is_null(ctx);
}

static inline void
func_adapter_pop_null(struct func_adapter *func, struct func_adapter_ctx *ctx)
{
	assert(func_adapter_is_null(func, ctx));
	func->vtab->pop_null(ctx);
}

static inline void
func_adapter_destroy(struct func_adapter *func)
{
	func->vtab->destroy(func);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
