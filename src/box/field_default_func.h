/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "func_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Function that generates a tuple field default value.
 */
struct field_default_func {
	/** Function id. */
	uint32_t id;
	/** Data of pinned function in func cache. */
	struct func_cache_holder holder;
	/** Call function with given argument. */
	int (*call)(struct field_default_func *default_func,
		    const char *arg_data, uint32_t arg_size,
		    const char **ret_data, uint32_t *ret_size);
	/** Destructor. */
	void (*destroy)(struct field_default_func *func);
};

/**
 * Call field default function `default_func`.
 * `arg_data` points to msgpack data with the function argument.
 * `ret_data` is pointed to msgpack data with the function return value,
 * allocated on current fiber's region.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 */
static inline int
field_default_func_call(struct field_default_func *default_func,
			const char *arg_data, uint32_t arg_size,
			const char **ret_data, uint32_t *ret_size)
{
	return default_func->call(default_func, arg_data, arg_size, ret_data,
				  ret_size);
}

static inline void
field_default_func_destroy(struct field_default_func *default_func)
{
	if (default_func->destroy != NULL)
		default_func->destroy(default_func);
}

/**
 * Unpin function from func cache if it has not been unpinned before.
 */
void
field_default_func_unpin(struct field_default_func *default_func);

/**
 * Pin function to func cache.
 */
void
field_default_func_pin(struct field_default_func *default_func);

/**
 * Initialize the field default function.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 */
int
field_default_func_init(struct field_default_func *default_func);

#ifdef __cplusplus
} /* extern "C" */
#endif
