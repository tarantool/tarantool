#ifndef TARANTOOL_BOX_TUPLE_UPDATE_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_UPDATE_H_INCLUDED
/*
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
#include "tarantool/util.h"

enum {
	/** A limit on how many operations a single UPDATE can have. */
	BOX_UPDATE_OP_CNT_MAX = 4000,
};

/** UPDATE operation codes. */
#define UPDATE_OP_CODES(_)			\
	_(UPDATE_OP_SET, 0)			\
	_(UPDATE_OP_ADD, 1)			\
	_(UPDATE_OP_AND, 2)			\
	_(UPDATE_OP_XOR, 3)			\
	_(UPDATE_OP_OR, 4)			\
	_(UPDATE_OP_SPLICE, 5)			\
	_(UPDATE_OP_DELETE, 6)			\
	_(UPDATE_OP_INSERT, 7)			\
	_(UPDATE_OP_SUBTRACT, 8)		\
	_(UPDATE_OP_MAX, 10)			\

ENUM(update_op_codes, UPDATE_OP_CODES);

typedef void *(*region_alloc_func)(void *, size_t);

struct tuple_update *
tuple_update_prepare(region_alloc_func alloc, void *alloc_ctx,
		     const char *expr,const char *expr_end,
		     const char *old_data, const char *old_data_end,
		     uint32_t old_fcount, uint32_t *p_new_size,
		     uint32_t *p_new_fcount);

void
tuple_update_execute(struct tuple_update *update, char *new_data);

#endif /* TARANTOOL_BOX_TUPLE_UPDATE_H_INCLUDED */
