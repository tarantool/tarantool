#ifndef INCLUDES_TARANTOOL_BOX_INFO_H
#define INCLUDES_TARANTOOL_BOX_INFO_H
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
 * { -- INFO_BEGIN
 *    section = { -- INFO_TABLE_BEGIN
 *        key1 = u32; -- INFO_U32
 *        key2 = u64; -- INFO_U64
 *        key3 = str;  -- INFO_STRING;
 *    };          -- INFO_TABLE_END
 *
 *    section2 = {
 *        ...
 *    };
 *    ...
 * } -- INFO_END
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
 * Tree element type
 */
enum info_type {
	/** The begin of document. */
	INFO_BEGIN,
	/** The end of document. */
	INFO_END,
	/* The begin of associative array (a map). */
	INFO_TABLE_BEGIN,
	/* The end of associative array (a map). */
	INFO_TABLE_END,
	/* Null-terminated string value. */
	INFO_STRING,
	/* uint32_t value. */
	INFO_U32,
	/* uint64_t value. */
	INFO_U64,
};

/**
 * Represents an element in box.info() tree.
 */
struct info_node {
	/** Element type. */
	enum info_type type;
	/** Key in associative array. */
	const char *key;
	union {
		/** Value of INFO_STRING type. */
		const char *str;
		/** Value of INFO_U32 type. */
		uint32_t u32;
		/** Value of INFO_U64 type. */
		uint64_t u64;
	} value;
};

/**
 * Adapter for Lua/C API to generate box.info() sections from engines.
 */
struct info_handler {
	/** A callback called by info_XXX() methods. */
	void (*fn)(struct info_node *node, void *ctx);
	/** Context for this callback. */
	void *ctx;
};

/**
 * Starts a new document and creates root-level associative array.
 * @param info box.info() adapter.
 * @throws C++ exception on OOM, see info.h comments.
 * @pre must be called once before any other functions.
 */
static inline void
info_begin(struct info_handler *info)
{
	struct info_node node = {
		.type = INFO_BEGIN,
		.key = NULL,
		.value = {}
	};
	info->fn(&node, info->ctx);
}

/**
 * Finishes the document and closes root-level associative array.
 * @param info box.info() adapter.
 * @throws C++ exception on OOM, see info.h comments.
 * @pre must be called at the end.
 */
static inline void
info_end(struct info_handler *info)
{
	struct info_node node = {
		.type = INFO_END,
		.key = NULL,
		.value = {}
	};
	info->fn(&node, info->ctx);
}

/**
 * Associates uint32_t value with @a key in the current associative array.
 * @param info box.info() adapter.
 * @param key key.
 * @param value value.
 * @throws C++ exception on OOM, see info.h comments.
 * @pre associative array is started.
 */
static inline void
info_append_u32(struct info_handler *info, const char *key, uint32_t value)
{
	struct info_node node = {
		.type = INFO_U32,
		.key = key,
		.value = {
			.u32 = value,
		}
	};
	info->fn(&node, info->ctx);
}

/**
 * Associates uint64_t value with @a key in the current associative array.
 * @param info box.info() adapter.
 * @param key key.
 * @param value value.
 * @throws C++ exception on OOM, see info.h comments.
 * @pre associative array is started.
 */
static inline void
info_append_u64(struct info_handler *info, const char *key, uint64_t value)
{
	struct info_node node = {
		.type = INFO_U64,
		.key = key,
		.value = {
			 .u64 = value,
		},
	};
	info->fn(&node, info->ctx);
}

/**
 * Associates zero-terminated string with @a key in the current associative
 * array.
 * @param info box.info() adapter.
 * @param key key.
 * @param value value.
 * @throws C++ exception on OOM, see info.h comments.
 */
static inline void
info_append_str(struct info_handler *info, const char *key,
		   const char *value)
{
	struct info_node node = {
		.type = INFO_STRING,
		.key = key,
		.value = {
			 .str = value,
		},
	};
	info->fn(&node, info->ctx);
}

/*
 * Associates a new associative array with @a key.
 * @param info box.info() adapter.
 * @param key key.
 * @throws C++ exception on OOM, see info.h comments.
 */
static inline void
info_table_begin(struct info_handler *info, const char *key)
{
	struct info_node node = {
		.type = INFO_TABLE_BEGIN,
		.key = key,
		.value = {}
	};
	info->fn(&node, info->ctx);
}

/*
 * Finishes the current active associative array.
 * @param info box.info() adapter
 * @throws C++ exception on OOM, see info.h comments.
 */
static inline void
info_table_end(struct info_handler *info)
{
	struct info_node node = {
		.type = INFO_TABLE_END,
		.key = NULL,
		.value = {}
	};
	info->fn(&node, info->ctx);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_INFO_H */
