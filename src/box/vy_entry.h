#ifndef INCLUDES_TARANTOOL_BOX_VY_ENTRY_H
#define INCLUDES_TARANTOOL_BOX_VY_ENTRY_H
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdbool.h>
#include <stddef.h>

#include "tuple_compare.h" /* hint_t */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;

/**
 * A helper struct that encapsulates a tuple with a comparison hint.
 * It is used for storing statements in vinyl containers, e.g. cache
 * or memory tree. Passed around by value.
 */
struct vy_entry {
	struct tuple *stmt;
	hint_t hint;
};

/**
 * Return an entry that doesn't point to any statement.
 */
static inline struct vy_entry
vy_entry_none(void)
{
	struct vy_entry entry;
	entry.stmt = NULL;
	entry.hint = HINT_NONE;
	return entry;
}

/**
 * Return true if two entries point to the same statements.
 */
static inline bool
vy_entry_is_equal(struct vy_entry a, struct vy_entry b)
{
	return a.stmt == b.stmt && a.hint == b.hint;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_ENTRY_H */
