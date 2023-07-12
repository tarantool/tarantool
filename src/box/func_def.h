#ifndef TARANTOOL_BOX_FUNC_DEF_H_INCLUDED
#define TARANTOOL_BOX_FUNC_DEF_H_INCLUDED
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

#include "trivia/util.h"
#include "field_def.h"
#include "opt_def.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The supported language of the stored function.
 */
enum func_language {
	FUNC_LANGUAGE_LUA,
	FUNC_LANGUAGE_C,
	FUNC_LANGUAGE_SQL,
	FUNC_LANGUAGE_SQL_BUILTIN,
	FUNC_LANGUAGE_SQL_EXPR,
	func_language_MAX,
};

extern const char *func_language_strs[];

enum func_aggregate {
	FUNC_AGGREGATE_NONE,
	FUNC_AGGREGATE_GROUP,
	func_aggregate_MAX,
};

extern const char *func_aggregate_strs[];

/** Function options. */
struct func_opts {
	/**
	 * True when a function returns multiple values
	 * packed in array.
	 */
	bool is_multikey;
	/**
	 * True if the function expects a msgpack object for args.
	 */
	bool takes_raw_args;
};

extern const struct func_opts func_opts_default;
extern const struct opt_def func_opts_reg[];

/** Create index options using default values. */
static inline void
func_opts_create(struct func_opts *opts)
{
	*opts = func_opts_default;
}

/**
 * Definition of a function. Function body is not stored
 * or replicated (yet).
 */
struct func_def {
	/** Function id. */
	uint32_t fid;
	/** Owner of the function. */
	uint32_t uid;
	/** Function name. */
	const char *name;
	/** The length of the function name. */
	uint32_t name_len;
	/** Definition of the persistent function. */
	char *body;
	/** User-defined comment for a function. */
	char *comment;
	/**
	 * MsgPack array of strings - names of events in trigger registry.
	 * If it is set, underlying array is not empty.
	 */
	const char *triggers;
	/**
	 * True if the function requires change of user id before
	 * invocation.
	 */
	bool setuid;
	/**
	 * Whether this function is deterministic (can produce
	 * only one result for a given list of parameters).
	 */
	bool is_deterministic;
	/**
	 * Whether the routine must be initialized with isolated
	 * sandbox where only a limited number if functions is
	 * available.
	 */
	bool is_sandboxed;
	/** The count of function's input arguments. */
	int param_count;
	/** The type of the value returned by function. */
	enum field_type returns;
	/** Function aggregate option. */
	enum func_aggregate aggregate;
	/**
	 * The language of the stored function.
	 */
	enum func_language language;
	/** Frontends where function must be available. */
	union {
		struct {
			bool lua : 1;
			bool sql : 1;
		};
		uint8_t all;
	} exports;
	/** The function options. */
	struct func_opts opts;
};

/**
 * Allocates and initializes a function definition.
 * Fields unspecified in the arguments are set to their default values.
 * This function never fails (never returns NULL).
 */
struct func_def *
func_def_new(uint32_t fid, uint32_t uid, const char *name, uint32_t name_len,
	     enum func_language language, const char *body, uint32_t body_len,
	     const char *comment, uint32_t comment_len);

/** Frees a function definition object. */
void
func_def_delete(struct func_def *def);

/**
 * Compare two given function definitions.
 * Two triggers fields (which are pointers to a MsgPack array) are considered
 * equal only if they contain the same amount of elements and all the elements
 * are equal and in the same order.
 */
int
func_def_cmp(const struct func_def *def1, const struct func_def *def2);

/**
 * Duplicates a given function definition object.
 * This function never fails (never returns NULL).
 */
struct func_def *
func_def_dup(const struct func_def *def);

/** Check if a non-empty function body is correct. */
int
func_def_check(const struct func_def *def);

#ifdef __cplusplus
}
#endif

#endif /* TARANTOOL_BOX_FUNC_DEF_H_INCLUDED */
