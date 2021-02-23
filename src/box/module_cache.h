/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include <stdint.h>

#include "trivia/config.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * API of C stored function.
 */

struct port;

struct box_function_ctx {
	struct port *port;
};

typedef struct box_function_ctx box_function_ctx_t;
typedef int (*box_function_t)(box_function_ctx_t *ctx,
			      const char *args,
			      const char *args_end);

/**
 * Shared library file attributes for
 * module cache invalidation.
 */
struct module_attr {
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_size;
	uint64_t tv_sec;
	uint64_t tv_nsec;
};

/**
 * Dynamic shared module.
 */
struct module {
	/**
	 * Module handle, dlopen() result.
	 */
	void *handle;
	/**
	 * File attributes.
	 */
	struct module_attr attr;
	/**
	 * Count of active references.
	 */
	int64_t refs;
	/**
	 * Length of @a package.
	 */
	size_t package_len;
	/**
	 * Module's name without file extension.
	 */
	char package[0];
};

/**
 * Module function.
 */
struct module_func {
	/**
	 * Function's address, iow dlsym() result.
	 */
	 box_function_t func;
	/**
	 * Function's module.
	 */
	struct module *module;
};

/**
 * Load a module.
 *
 * Lookup for a module instance in cache and if not found
 * the module is loaded from a storage device. In case if
 * the module is present in cache but modified on a storage
 * device it will be reread as a new and cache entry get
 * updated.
 *
 * @param package module package (without file extension).
 * @param package_len length of @a package.
 *
 * Possible errors:
 * ClientError: the package is not found on a storage device.
 * ClientError: an error happened when been loading the package.
 * SystemError: a system error happened during procedure.
 * OutOfMemory: unable to allocate new memory for module instance.
 *
 * @return a module instance on success, NULL otherwise (diag is set)
 */
struct module *
module_load(const char *package, size_t package_len);

/**
 * Force load a module.
 *
 * Load a module from a storage device in a force way
 * and update an associated cache entry.
 *
 * @param package module package (without file extension).
 * @param package_len length of @a package.
 *
 * Possible errors:
 * ClientError: the package is not found on a storage device.
 * ClientError: an error happened when been loading the package.
 * SystemError: a system error happened during procedure.
 * OutOfMemory: unable to allocate new memory for module instance.
 *
 * @return a module instance on success, NULL otherwise (diag is set)
 */
struct module *
module_load_force(const char *package, size_t package_len);

/**
 * Unload a module instance.
 *
 * @param m a module to unload.
 */
void
module_unload(struct module *m);

/** Test if module function is empty. */
static inline bool
module_func_is_empty(struct module_func *mf)
{
	return mf->module == NULL;
}

/** Create new empty module function. */
static inline void
module_func_create(struct module_func *mf)
{
	mf->module = NULL;
	mf->func = NULL;
}

/**
 * Load a new function.
 *
 * @param m a module to load a function from.
 * @param func_name function name.
 * @param mf[out] function instance.
 *
 * Possible errors:
 * ClientError: no such function in a module.
 *
 * @return 0 on success, -1 otherwise (diag is set).
 */
int
module_func_load(struct module *m, const char *func_name,
		 struct module_func *mf);

/**
 * Unload a function.
 *
 * @param mf module function.
 */
void
module_func_unload(struct module_func *mf);

/**
 * Execute a function.
 *
 * @param mf a function to execute.
 * @param args function arguments.
 * @param ret[out] execution results.
 *
 * @return 0 on success, -1 otherwise (diag is set).
 */
int
module_func_call(struct module_func *mf, struct port *args,
		 struct port *ret);

/** Increment reference to a module. */
void
module_ref(struct module *m);

/** Decrement reference of a module. */
void
module_unref(struct module *m);

/** Initialize modules subsystem. */
int
module_init(void);

/** Free modules subsystem. */
void
module_free(void);

#if defined(__cplusplus)
}
#endif /* defined(__plusplus) */
