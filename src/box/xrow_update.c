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
#include "xrow_update.h"
#include <stdbool.h>

#include "say.h"
#include "error.h"
#include "diag.h"
#include <msgpuck/msgpuck.h>
#include "column_mask.h"
#include "fiber.h"
#include "xrow_update_field.h"
#include "tuple_format.h"

/**
 * UPDATE is represented by a sequence of operations, each
 * working with a single field. There also are operations which
 * add or remove fields. Only one operation on the same field
 * is allowed.
 * Field is any part of a tuple: top-level array's field, leaf of
 * a complex tuple with lots of maps and arrays inside, a whole
 * map/array inside a tuple.
 *
 * Supported field change operations are: SET, ADD, SUBTRACT;
 * bitwise AND, XOR and OR; SPLICE.
 * Supported tuple change operations are: SET, DELETE, INSERT.
 *
 * If the number of fields in a tuple is altered by an operation,
 * field index of all following operations is evaluated against the
 * new tuple. It applies to internal tuple's arrays too.
 *
 * Despite the allowed complexity, a typical use case for UPDATE
 * is when the operation count is much less than field count in
 * a tuple.
 *
 * With the common case in mind, UPDATE tries to minimize
 * the amount of unnecessary temporary tuple copies.
 *
 * First, operations are parsed and initialized. Then they are
 * applied one by one to a tuple. Each operation may change an
 * already located field in a tuple, or may split parent of the
 * field into subtrees. After all operations are applied, the
 * result is a tree of updated, new, and non-changed fields.
 * The tree needs to be flattened into MessagePack format. For
 * that a resulting tuple length is calculated. Memory for the new
 * tuple is allocated in one contiguous chunk. Then the update
 * tree is stored into the chunk as a result tuple.
 *
 * Note, that the result tree didn't allocate anything until a
 * result was stored. It was referencing old tuple's memory.
 * With this approach, cost of UPDATE is proportional to O(tuple
 * length) + O(C * log C), where C is the number of operations in
 * the request, and data is copied from the old tuple to the new
 * one only once.
 *
 * As long as INSERT and DELETE change the relative field order in
 * arrays and maps, these fields are represented as special
 * structures optimized for updates to provide fast search and
 * don't realloc anything. It is 'rope' data structure for array,
 * and a simpler key-value list sorted by update time for map.
 *
 * A rope is a binary tree designed to store long strings built
 * from pieces. Each tree node points to a substring of a large
 * string. In our case, each rope node points at a range of
 * fields, initially in the old tuple, and then, as fields are
 * added and deleted by UPDATE, in the "current" tuple.
 * Note, that the tuple itself is not materialized: when
 * operations which affect field count are initialized, the rope
 * is updated to reflect the new field order.
 * In particular, if a field is deleted by an operation,
 * it disappears from the rope and all subsequent operations
 * on this field number instead affect the field following the
 * deleted one.
 */

/** Update internal state */
struct xrow_update
{
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
 * Read and check update operations and fill column mask.
 *
 * @param[out] update Update meta.
 * @param expr MessagePack array of operations.
 * @param expr_end End of the @expr.
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
static int
xrow_update_read_ops(struct xrow_update *update, const char *expr,
		     const char *expr_end, struct tuple_dictionary *dict,
		     int32_t field_count_hint)
{
	if (mp_typeof(*expr) != MP_ARRAY) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "update operations must be an "
			 "array {{op,..}, {op,..}}");
		return -1;
	}
	uint64_t column_mask = 0;
	/* number of operations */
	update->op_count = mp_decode_array(&expr);

	if (update->op_count > BOX_UPDATE_OP_CNT_MAX) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "too many operations for update");
		return -1;
	}

	int size = update->op_count * sizeof(update->ops[0]);
	update->ops = (struct xrow_update_op *)
		region_aligned_alloc(&fiber()->gc, size,
				     alignof(struct xrow_update_op));
	if (update->ops == NULL) {
		diag_set(OutOfMemory, size, "region_aligned_alloc",
			 "update->ops");
		return -1;
	}
	struct xrow_update_op *op = update->ops;
	struct xrow_update_op *ops_end = op + update->op_count;
	for (int i = 1; op < ops_end; op++, i++) {
		if (xrow_update_op_decode(op, i, update->index_base, dict,
					  &expr) != 0)
			return -1;
		/*
		 * Continue collecting the changed columns
		 * only if there are unset bits in the mask.
		 */
		if (column_mask != COLUMN_MASK_FULL) {
			int32_t field_no;
			char opcode;
			if (xrow_update_op_is_term(op)) {
				opcode = op->opcode;
			} else {
				/*
				 * When a field is not terminal,
				 * on the first level it for sure
				 * changes only one field and in
				 * terms of column mask is
				 * equivalent to any scalar
				 * operation. Even if it was '!'
				 * or '#'. Zero means, that it
				 * won't match any checks with
				 * non-scalar operations below.
				 */
				opcode = 0;
			}
			if (op->field_no >= 0)
				field_no = op->field_no;
			else if (opcode != '!')
				field_no = field_count_hint + op->field_no;
			else
				/*
				 * '!' with a negative number
				 * inserts a new value after the
				 * position, specified in the
				 * field_no. Example:
				 * tuple: [1, 2, 3]
				 *
				 * update1: {'#', -1, 1}
				 * update2: {'!', -1, 4}
				 *
				 * result1: [1, 2, * ]
				 * result2: [1, 2, 3, *4]
				 * As you can see, both operations
				 * have field_no -1, but '!' actually
				 * creates a new field. So
				 * set field_no to insert position + 1.
				 */
				field_no = field_count_hint + op->field_no + 1;
			/*
			 * Here field_no can be < 0 only if update
			 * operation encounters a negative field
			 * number N and abs(N) > field_count_hint.
			 * For example, the tuple is: {1, 2, 3},
			 * and the update operation is
			 * {'#', -4, 1}.
			 */
			if (field_no < 0) {
				/*
				 * Turn off column mask for this
				 * incorrect UPDATE.
				 */
				column_mask_set_range(&column_mask, 0);
				continue;
			}

			/*
			 * Update result statement's field count
			 * hint. It is used to translate negative
			 * field numbers into positive ones.
			 */
			if (opcode == '!')
				++field_count_hint;
			else if (opcode == '#')
				field_count_hint -= (int32_t) op->arg.del.count;

			if (opcode == '!' || opcode == '#')
				/*
				 * If the operation is insertion
				 * or deletion then it potentially
				 * changes a range of columns by
				 * moving them, so need to set a
				 * range of bits.
				 */
				column_mask_set_range(&column_mask, field_no);
			else
				column_mask_set_fieldno(&column_mask, field_no);
		}
	}

	/* Check the remainder length, the request must be fully read. */
	if (expr != expr_end) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "can't unpack update operations");
		return -1;
	}
	update->column_mask = column_mask;
	return 0;
}

/**
 * Apply update operations to the concrete tuple.
 *
 * @param update Update meta.
 * @param old_data MessagePack array of tuple fields without the
 *        array header.
 * @param old_data_end End of the @old_data.
 * @param part_count Field count in the @old_data.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
static int
xrow_update_do_ops(struct xrow_update *update, const char *header,
		   const char *old_data, const char *old_data_end,
		   uint32_t part_count)
{
	if (xrow_update_array_create(&update->root, header, old_data,
				     old_data_end, part_count) != 0)
		return -1;
	struct xrow_update_op *op = update->ops;
	struct xrow_update_op *ops_end = op + update->op_count;
	for (; op < ops_end; op++) {
		if (op->meta->do_op(op, &update->root) != 0)
			return -1;
	}
	return 0;
}

/*
 * Same as update_do_ops but for upsert.
 * @param suppress_error True, if an upsert error is not critical
 *        and it is enough to simply write the error to the log.
 */
static int
xrow_upsert_do_ops(struct xrow_update *update, const char *header,
		   const char *old_data, const char *old_data_end,
		   uint32_t part_count, bool suppress_error)
{
	if (xrow_update_array_create(&update->root, header, old_data,
				     old_data_end, part_count) != 0)
		return -1;
	struct xrow_update_op *op = update->ops;
	struct xrow_update_op *ops_end = op + update->op_count;
	for (; op < ops_end; op++) {
		if (op->meta->do_op(op, &update->root) == 0)
			continue;
		struct error *e = diag_last_error(diag_get());
		if (e->type != &type_ClientError)
			return -1;
		if (!suppress_error) {
			say_error("UPSERT operation failed:");
			error_log(e);
		}
	}
	return 0;
}

static void
xrow_update_init(struct xrow_update *update, int index_base)
{
	memset(update, 0, sizeof(*update));
	update->index_base = index_base;
}

static const char *
xrow_update_finish(struct xrow_update *update, struct tuple_format *format,
		   uint32_t *p_tuple_len)
{
	uint32_t tuple_len = xrow_update_array_sizeof(&update->root);
	char *buffer = (char *) region_alloc(&fiber()->gc, tuple_len);
	if (buffer == NULL) {
		diag_set(OutOfMemory, tuple_len, "region_alloc", "buffer");
		return NULL;
	}
	*p_tuple_len = xrow_update_array_store(&update->root, &format->fields,
					       &format->fields.root, buffer,
					       buffer + tuple_len);
	assert(*p_tuple_len <= tuple_len);
	return buffer;
}

int
xrow_update_check_ops(const char *expr, const char *expr_end,
		      struct tuple_format *format, int index_base)
{
	struct xrow_update update;
	xrow_update_init(&update, index_base);
	return xrow_update_read_ops(&update, expr, expr_end, format->dict, 0);
}

const char *
xrow_update_execute(const char *expr,const char *expr_end,
		    const char *old_data, const char *old_data_end,
		    struct tuple_format *format, uint32_t *p_tuple_len,
		    int index_base, uint64_t *column_mask)
{
	struct xrow_update update;
	xrow_update_init(&update, index_base);
	const char *header = old_data;
	uint32_t field_count = mp_decode_array(&old_data);

	if (xrow_update_read_ops(&update, expr, expr_end, format->dict,
				 field_count) != 0)
		return NULL;
	if (xrow_update_do_ops(&update, header, old_data, old_data_end,
			       field_count) != 0)
		return NULL;
	if (column_mask)
		*column_mask = update.column_mask;

	return xrow_update_finish(&update, format, p_tuple_len);
}

const char *
xrow_upsert_execute(const char *expr,const char *expr_end,
		    const char *old_data, const char *old_data_end,
		    struct tuple_format *format, uint32_t *p_tuple_len,
		    int index_base, bool suppress_error, uint64_t *column_mask)
{
	struct xrow_update update;
	xrow_update_init(&update, index_base);
	const char *header = old_data;
	uint32_t field_count = mp_decode_array(&old_data);

	if (xrow_update_read_ops(&update, expr, expr_end, format->dict,
				 field_count) != 0)
		return NULL;
	if (xrow_upsert_do_ops(&update, header, old_data, old_data_end,
			       field_count, suppress_error) != 0)
		return NULL;
	if (column_mask)
		*column_mask = update.column_mask;

	return xrow_update_finish(&update, format, p_tuple_len);
}
