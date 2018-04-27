#ifndef TARANTOOL_BOX_TUPLE_UPDATE_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_UPDATE_H_INCLUDED
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
#include <stdbool.h>
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	/** A limit on how many operations a single UPDATE can have. */
	BOX_UPDATE_OP_CNT_MAX = 4000,
};

int
tuple_update_check_ops(const char *expr, const char *expr_end, int index_base);

const char *
tuple_update_execute(const char *expr,const char *expr_end,
		     const char *old_data, const char *old_data_end,
		     uint32_t *p_new_size, int index_base,
		     uint64_t *column_mask);

const char *
tuple_upsert_execute(const char *expr, const char *expr_end,
		     const char *old_data, const char *old_data_end,
		     uint32_t *p_new_size, int index_base, bool suppress_error,
		     uint64_t *column_mask);

/**
 * Try to merge two update/upsert expressions to an equivalent one.
 * Resulting expression is allocated on given allocator.
 * Due to optimization reasons resulting expression
 * is located inside a bigger allocation. There also some hidden
 * internal allocations are made in this function.
 * Thus the only allocator that can be used in this function
 * is region allocator.
 * If it isn't possible to merge expressions NULL is returned.
 */
const char *
tuple_upsert_squash(const char *expr1, const char *expr1_end,
		    const char *expr2, const char *expr2_end,
		    size_t *result_size, int index_base);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_TUPLE_UPDATE_H_INCLUDED */
