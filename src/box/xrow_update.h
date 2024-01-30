#ifndef TARANTOOL_BOX_XROW_UPDATE_H_INCLUDED
#define TARANTOOL_BOX_XROW_UPDATE_H_INCLUDED
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
#include "xrow_update_field.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	/** A limit on how many operations a single UPDATE can have. */
	BOX_UPDATE_OP_CNT_MAX = 4000,
};

struct tuple_format;
struct tuple_dictionary;

/** Update internal state */
struct xrow_update {
	/** Operations array. */
	struct xrow_update_op *ops;
	/** Length of ops. */
	uint32_t op_count;
	/**
	 * Index base for MessagePack update operations. If update
	 * is from Lua, then the base is 1. Otherwise 0. That
	 * field exists because Lua uses 1-based array indexing,
	 * and Lua-to-MessagePack encoder keeps this indexing when
	 * encodes operations array. Index base allows not to
	 * re-encode each Lua update with 0-based indexes.
	 */
	int index_base;
	/**
	 * A bitmask of all columns modified by this update. Only
	 * the first level of a tuple is accounted here. I.e. if
	 * a field [1][2][3] was updated, then only [1] is
	 * reflected.
	 */
	uint64_t column_mask;
	/** First level of update tree. It is always array. */
	struct xrow_update_field root;
};

/**
 * Initialize `xrow_update' structure.
 */
void
xrow_update_init(struct xrow_update *update, int index_base);

/**
 * Read and check update operations and fill column mask.
 *
 * @param[out] update Update meta.
 * @param expr MessagePack array of operations.
 * @param expr_end End of the @a expr.
 * @param dict Dictionary to lookup field number by a name.
 * @param field_count_hint Field count in the updated tuple. If
 *        there is no tuple at hand (for example, when we are
 *        reading UPSERT operations), then 0 for field count will
 *        do as a hint: the only effect of a wrong hint is
 *        a possibly incorrect column_mask.
 *        A correct field count results in an accurate
 *        column mask calculation.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
int
xrow_update_read_ops(struct xrow_update *update, const char *expr,
		     const char *expr_end, struct tuple_dictionary *dict,
		     int32_t field_count_hint);

int
xrow_update_check_ops(const char *expr, const char *expr_end,
		      struct tuple_format *format, int index_base);

const char *
xrow_update_execute(const char *expr,const char *expr_end,
		    const char *old_data, const char *old_data_end,
		    struct tuple_format *format, uint32_t *p_new_size,
		    int index_base, uint64_t *column_mask);

const char *
xrow_upsert_execute(const char *expr, const char *expr_end,
		    const char *old_data, const char *old_data_end,
		    struct tuple_format *format, uint32_t *p_new_size,
		    int index_base, bool suppress_error,
		    uint64_t *column_mask);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_XROW_UPDATE_H_INCLUDED */
