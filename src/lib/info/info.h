#ifndef INCLUDES_TARANTOOL_LIB_INFO_INFO_H
#define INCLUDES_TARANTOOL_LIB_INFO_INFO_H
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdint.h>

/**
 * @file
 * This module provides an adapter for Lua/C API to generate box.info()
 * and index:info() introspection trees. The primary purpose of this
 * adapter is to eliminate Engine <-> Lua interdependency.
 *
 * TREE STRUCTURE
 *
 * { -- info_begin
 *    section = { -- info_begin_table
 *        key1 = int;    -- info_append_int
 *        key2 = double; -- info_append_double
 *        key3 = str;    -- info_append_str
 *    };          -- info_end_table
 *
 *    section2 = {
 *        ...
 *    };
 *    ...
 * } -- info_end
 *
 *
 * IMPLEMENTATION DETAILS
 *
 * Current implementation calls Lua/C API under the hood without any
 * pcall() wrapping. As you may now, idiosyncratic Lua/C API unwinds
 * C stacks on errors in a way you can't handle in C. Please ensure that
 * all blocks of code which call info_append_XXX() functions are
 * exception/longjmp safe.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Virtual method table for struct info_handler.
 */
struct info_handler_vtab {
	/** The begin of document. */
	void (*begin)(struct info_handler *);
	/** The end of document. */
	void (*end)(struct info_handler *);
	/** The begin of associative array (a map). */
	void (*begin_table)(struct info_handler *, const char *key);
	/** The end of associative array (a map). */
	void (*end_table)(struct info_handler *);
	/** Set string value. */
	void (*append_str)(struct info_handler *, const char *key,
			   const char *value);
	/** Set int64_t value. */
	void (*append_int)(struct info_handler *, const char *key,
			   int64_t value);
	/** Set double value. */
	void (*append_double)(struct info_handler *,
			      const char *key, double value);
};

/**
 * Adapter for Lua/C API to generate box.info() sections from engines.
 */
struct info_handler {
	struct info_handler_vtab *vtab;
	/** Context for this callback. */
	void *ctx;
};

/**
 * Starts a new document and creates root-level associative array.
 * @param info box.info() adapter.
 * @pre must be called once before any other functions.
 */
static inline void
info_begin(struct info_handler *info)
{
	return info->vtab->begin(info);
}

/**
 * Finishes the document and closes root-level associative array.
 * @param info box.info() adapter.
 * @pre must be called at the end.
 */
static inline void
info_end(struct info_handler *info)
{
	return info->vtab->end(info);
}

/**
 * Associates int64_t value with @a key in the current associative array.
 * @param info box.info() adapter.
 * @param key key.
 * @param value value.
 * @pre associative array is started.
 */
static inline void
info_append_int(struct info_handler *info, const char *key, int64_t value)
{
	return info->vtab->append_int(info, key, value);
}

/**
 * Associates zero-terminated string with @a key in the current associative
 * array.
 * @param info box.info() adapter.
 * @param key key.
 * @param value value.
 */
static inline void
info_append_str(struct info_handler *info, const char *key,
		   const char *value)
{
	return info->vtab->append_str(info, key, value);
}

/**
 * Associates double value with @a key in the current associative
 * array.
 * @param info box.info() adapter.
 * @param key key.
 * @param value value.
 */
static inline void
info_append_double(struct info_handler *info, const char *key,
		   double value)
{
	return info->vtab->append_double(info, key, value);
}

/*
 * Associates a new associative array with @a key.
 * @param info box.info() adapter.
 * @param key key.
 */
static inline void
info_table_begin(struct info_handler *info, const char *key)
{
	return info->vtab->begin_table(info, key);
}

/*
 * Finishes the current active associative array.
 * @param info box.info() adapter
 */
static inline void
info_table_end(struct info_handler *info)
{
	return info->vtab->end_table(info);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_LIB_INFO_INFO_H */
