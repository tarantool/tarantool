/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "xrow_update_field.h"
#include "msgpuck.h"
#include "fiber.h"
#include "tuple_format.h"

/**
 * Make sure @a op contains a valid field number to where the
 * operation should be applied next. Field number may be not
 * known, if the array's parent didn't propagate operation's
 * lexer. In fact, the parent fills fieldno only in some rare
 * cases like branching. Generally, an array should care about
 * fieldno by itself.
 */
static inline int
xrow_update_op_prepare_num_token(struct xrow_update_op *op)
{
	if (op->is_token_consumed && xrow_update_op_next_token(op) != 0)
		return -1;
	if (op->token_type != JSON_TOKEN_NUM) {
		return xrow_update_err(op, "can't update an array by a "\
				       "non-numeric index");
	}
	return 0;
}

/**
 * Make field index non-negative and check for the field
 * existence.
 */
static inline int
xrow_update_op_adjust_field_no(struct xrow_update_op *op, int32_t field_count)
{
	assert(op->token_type == JSON_TOKEN_NUM && !op->is_token_consumed);
	if (op->field_no >= 0) {
		if (op->field_no < field_count)
			return 0;
	} else if (op->field_no + field_count >= 0) {
		op->field_no += field_count;
		return 0;
	}
	return xrow_update_err_no_such_field(op);
}

/**
 * Updated array is divided into array items. Each item is a range
 * of fields. Item updates first field of the range and stores
 * size of others to save them with no changes into a new tuple
 * later. It allows on update of a single field in an array store
 * at most 2 objects - item for the previous fields, and item for
 * this one + its tail. This is how rope data structure works - a
 * binary tree designed for big contiguous object updates.
 */
struct xrow_update_array_item {
	/** First field in the range, contains an update. */
	struct xrow_update_field field;
	/** Size of other fields in the range. */
	uint32_t tail_size;
};

/** Initialize an array item. */
static inline void
xrow_update_array_item_create(struct xrow_update_array_item *item,
			      enum xrow_update_type type, const char *data,
			      uint32_t size, uint32_t tail_size)
{
	item->field.type = type;
	item->field.data = data;
	item->field.size = size;
	item->tail_size = tail_size;
}

/** Rope allocator for nodes, paths, items etc. */
static inline void *
xrow_update_alloc(struct region *region, size_t size)
{
	void *ptr = region_aligned_alloc(region, size, alignof(uint64_t));
	if (ptr == NULL) {
		diag_set(OutOfMemory, size, "region_aligned_alloc",
			 "xrow update internals");
	}
	return ptr;
}

/** Split a range of fields in two. */
static struct xrow_update_array_item *
xrow_update_array_item_split(struct region *region,
			     struct xrow_update_array_item *prev, size_t size,
			     size_t offset)
{
	(void) size;
	struct xrow_update_array_item *next = (struct xrow_update_array_item *)
		xrow_update_alloc(region, sizeof(*next));
	if (next == NULL)
		return NULL;
	assert(offset > 0 && prev->tail_size > 0);

	const char *tail = prev->field.data + prev->field.size;
	const char *field = tail;
	const char *range_end = tail + prev->tail_size;

	for (uint32_t i = 1; i < offset; ++i)
		mp_next(&field);

	prev->tail_size = field - tail;
	const char *field_end = field;
	mp_next(&field_end);
	xrow_update_array_item_create(next, XUPDATE_NOP, field,
				      field_end - field, range_end - field_end);
	return next;
}

#define ROPE_SPLIT_F xrow_update_array_item_split
#define ROPE_ALLOC_F xrow_update_alloc
#define rope_data_t struct xrow_update_array_item *
#define rope_ctx_t struct region *
#define rope_name xrow_update

#include "salad/rope.h"

/**
 * Extract from the array an item whose range starts from the
 * field affected by @a op.
 */
static inline struct xrow_update_array_item *
xrow_update_array_extract_item(struct xrow_update_field *field,
			       struct xrow_update_op *op)
{
	assert(field->type == XUPDATE_ARRAY);
	struct xrow_update_rope *rope = field->array.rope;
	uint32_t size = xrow_update_rope_size(rope);
	if (xrow_update_op_adjust_field_no(op, size) == 0)
		return xrow_update_rope_extract(rope, op->field_no);
	return NULL;
}

int
xrow_update_array_create(struct xrow_update_field *field, const char *header,
			 const char *data, const char *data_end,
			 uint32_t field_count)
{
	field->type = XUPDATE_ARRAY;
	field->data = header;
	field->size = data_end - header;
	struct region *region = &fiber()->gc;
	field->array.rope = xrow_update_rope_new(region);
	if (field->array.rope == NULL)
		return -1;
	struct xrow_update_array_item *item = (struct xrow_update_array_item *)
		xrow_update_alloc(region, sizeof(*item));
	if (item == NULL)
		return -1;
	if (data == data_end)
		return 0;
	/*
	 * Initial item consists of one range - the whole array.
	 */
	const char *begin = data;
	mp_next(&data);
	xrow_update_array_item_create(item, XUPDATE_NOP, begin, data - begin,
				      data_end - data);
	return xrow_update_rope_append(field->array.rope, item, field_count);
}

int
xrow_update_array_create_with_child(struct xrow_update_field *field,
				    const char *header,
				    const struct xrow_update_field *child,
				    int32_t field_no)
{
	const char *data = header;
	uint32_t field_count = mp_decode_array(&data);
	const char *first_field = data;
	const char *first_field_end = first_field;
	mp_next(&first_field_end);
	struct region *region = &fiber()->gc;
	struct xrow_update_rope *rope = xrow_update_rope_new(region);
	if (rope == NULL)
		return -1;
	struct xrow_update_array_item *item = (struct xrow_update_array_item *)
		xrow_update_alloc(region, sizeof(*item));
	if (item == NULL)
		return -1;
	const char *end = first_field_end;
	if (field_no > 0) {
		for (int32_t i = 1; i < field_no; ++i)
			mp_next(&end);
		xrow_update_array_item_create(item, XUPDATE_NOP, first_field,
					      first_field_end - first_field,
					      end - first_field_end);
		if (xrow_update_rope_append(rope, item, field_no) != 0)
			return -1;
		item = (struct xrow_update_array_item *)
			xrow_update_alloc(region, sizeof(*item));
		if (item == NULL)
			return -1;
		first_field = end;
		first_field_end = first_field;
		mp_next(&first_field_end);
		end = first_field_end;
	}
	for (uint32_t i = field_no + 1; i < field_count; ++i)
		mp_next(&end);
	item->field = *child;
	xrow_update_array_item_create(item, child->type, first_field,
				      first_field_end - first_field,
				      end - first_field_end);
	field->type = XUPDATE_ARRAY;
	field->data = header;
	field->size = end - header;
	field->array.rope = rope;
	return xrow_update_rope_append(rope, item, field_count - field_no);
}

uint32_t
xrow_update_array_sizeof(struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_ARRAY);
	struct xrow_update_rope_iter it;
	xrow_update_rope_iter_create(&it, field->array.rope);

	uint32_t size = xrow_update_rope_size(field->array.rope);
	uint32_t res = mp_sizeof_array(size);
	struct xrow_update_rope_node *node = xrow_update_rope_iter_start(&it);
	for (; node != NULL; node = xrow_update_rope_iter_next(&it)) {
		struct xrow_update_array_item *item =
			xrow_update_rope_leaf_data(node);
		res += xrow_update_field_sizeof(&item->field) + item->tail_size;
	}
	return res;
}

uint32_t
xrow_update_array_store(struct xrow_update_field *field,
			struct json_tree *format_tree,
			struct json_token *this_node, char *out, char *out_end)
{
	assert(field->type == XUPDATE_ARRAY);
	char *out_begin = out;
	out = mp_encode_array(out, xrow_update_rope_size(field->array.rope));
	struct xrow_update_rope_iter it;
	xrow_update_rope_iter_create(&it, field->array.rope);
	struct xrow_update_rope_node *node = xrow_update_rope_iter_start(&it);
	uint32_t total_field_count = 0;
	if (this_node == NULL) {
		for (; node != NULL; node = xrow_update_rope_iter_next(&it)) {
			struct xrow_update_array_item *item =
				xrow_update_rope_leaf_data(node);
			uint32_t field_count = xrow_update_rope_leaf_size(node);
			out += xrow_update_field_store(&item->field, NULL, NULL,
						       out, out_end);
			assert(item->tail_size == 0 || field_count > 1);
			memcpy(out, item->field.data + item->field.size,
			       item->tail_size);
			out += item->tail_size;
			total_field_count += field_count;
		}
	} else {
		struct json_token token;
		token.type = JSON_TOKEN_NUM;
		token.num = 0;
		struct json_token *next_node;
		for (; node != NULL; node = xrow_update_rope_iter_next(&it)) {
			struct xrow_update_array_item *item =
				xrow_update_rope_leaf_data(node);
			next_node = json_tree_lookup(format_tree, this_node, &token);
			uint32_t field_count = xrow_update_rope_leaf_size(node);
			out += xrow_update_field_store(&item->field, format_tree,
						       next_node, out, out_end);
			assert(item->tail_size == 0 || field_count > 1);
			memcpy(out, item->field.data + item->field.size,
			       item->tail_size);
			out += item->tail_size;
			token.num += field_count;
			total_field_count += field_count;
		}
	}
	(void) total_field_count;
	assert(xrow_update_rope_size(field->array.rope) == total_field_count);
	assert(out <= out_end);
	return out - out_begin;
}

/**
 * Helper function that appends nils in the end so that op will insert
 * without gaps
 */
static int
xrow_update_array_append_nils(struct xrow_update_field *field,
			      struct xrow_update_op *op)
{
	struct xrow_update_rope *rope = field->array.rope;
	uint32_t size = xrow_update_rope_size(rope);
	if (op->field_no < 0 || (uint32_t)op->field_no <= size)
		return 0;
	/*
	 * Do not allow autofill of nested arrays with nulls. It is not
	 * supported only because there is no an easy way how to apply that to
	 * bar updates which can also affect arrays.
	 */
	if (!op->is_for_root)
		return 0;
	uint32_t nil_count = op->field_no - size;
	struct xrow_update_array_item *item =
		(struct xrow_update_array_item *)
		xrow_update_alloc(rope->ctx, sizeof(*item));
	if (item == NULL)
		return -1;
	assert(mp_sizeof_nil() == 1);
	char *item_data = (char *)region_alloc(rope->ctx, nil_count);
	if (item_data == NULL) {
		diag_set(OutOfMemory, nil_count, "region", "item_data");
		return -1;
	}
	memset(item_data, 0xc0, nil_count);
	xrow_update_array_item_create(item, XUPDATE_NOP, item_data, 1,
				      nil_count - 1);
	return xrow_update_rope_insert(rope, op->field_no, item, nil_count);
}

int
xrow_update_op_do_array_insert(struct xrow_update_op *op,
			       struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_ARRAY);
	struct xrow_update_array_item *item;
	if (xrow_update_op_prepare_num_token(op) != 0)
		return -1;

	if (!xrow_update_op_is_term(op)) {
		item = xrow_update_array_extract_item(field, op);
		if (item == NULL)
			return -1;
		op->is_token_consumed = true;
		return xrow_update_op_do_field_insert(op, &item->field);
	}

	if (xrow_update_array_append_nils(field, op) != 0)
		return -1;

	struct xrow_update_rope *rope = field->array.rope;
	uint32_t size = xrow_update_rope_size(rope);
	if (xrow_update_op_adjust_field_no(op, size + 1) != 0)
		return -1;

	item = (struct xrow_update_array_item *)
		xrow_update_alloc(rope->ctx, sizeof(*item));
	if (item == NULL)
		return -1;
	xrow_update_array_item_create(item, XUPDATE_NOP, op->arg.set.value,
				      op->arg.set.length, 0);
	return xrow_update_rope_insert(rope, op->field_no, item, 1);
}

int
xrow_update_op_do_array_set(struct xrow_update_op *op,
			    struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_ARRAY);
	struct xrow_update_rope *rope = field->array.rope;
	if (xrow_update_op_prepare_num_token(op) != 0)
		return -1;

	/* Interpret '=' for n + 1 field as insert. */
	if (op->field_no >= (int32_t) xrow_update_rope_size(rope))
		return xrow_update_op_do_array_insert(op, field);

	struct xrow_update_array_item *item =
		xrow_update_array_extract_item(field, op);
	if (item == NULL)
		return -1;
	if (!xrow_update_op_is_term(op)) {
		op->is_token_consumed = true;
		return xrow_update_op_do_field_set(op, &item->field);
	}
	op->new_field_len = op->arg.set.length;
	/*
	 * Ignore the previous op, if any. It is not correct,
	 * strictly speaking, since only one update is allowed per
	 * field. But it was allowed since the beginning, and
	 * should be supported now for compatibility.
	 */
	item->field.type = XUPDATE_SCALAR;
	item->field.scalar.op = op;
	return 0;
}

int
xrow_update_op_do_array_delete(struct xrow_update_op *op,
			       struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_ARRAY);
	if (xrow_update_op_prepare_num_token(op) != 0)
		return -1;

	if (!xrow_update_op_is_term(op)) {
		struct xrow_update_array_item *item =
			xrow_update_array_extract_item(field, op);
		if (item == NULL)
			return -1;
		op->is_token_consumed = true;
		return xrow_update_op_do_field_delete(op, &item->field);
	}

	struct xrow_update_rope *rope = field->array.rope;
	uint32_t size = xrow_update_rope_size(rope);
	if (xrow_update_op_adjust_field_no(op, size) != 0) {
		if (op->field_no >= (int)size)
			return 0;
		return -1;
	}
	uint32_t delete_count = op->arg.del.count;
	if ((uint64_t) op->field_no + delete_count > size)
		delete_count = size - op->field_no;
	assert(delete_count > 0);
	for (uint32_t u = delete_count; u != 0; --u)
		xrow_update_rope_erase(rope, op->field_no);
	return 0;
}

#define DO_SCALAR_OP_GENERIC(op_type)						\
int										\
xrow_update_op_do_array_##op_type(struct xrow_update_op *op,			\
				  struct xrow_update_field *field)		\
{										\
	if (xrow_update_op_prepare_num_token(op) != 0)				\
		return -1;							\
	struct xrow_update_array_item *item =					\
		xrow_update_array_extract_item(field, op);			\
	if (item == NULL)							\
		return -1;							\
	if (!xrow_update_op_is_term(op)) {					\
		op->is_token_consumed = true;					\
		return xrow_update_op_do_field_##op_type(op, &item->field);	\
	}									\
	if (item->field.type != XUPDATE_NOP)					\
		return xrow_update_err_double(op);				\
	if (xrow_update_op_do_##op_type(op, item->field.data) != 0)		\
		return -1;							\
	item->field.type = XUPDATE_SCALAR;					\
	item->field.scalar.op = op;						\
	return 0;								\
}

DO_SCALAR_OP_GENERIC(arith)

DO_SCALAR_OP_GENERIC(bit)

DO_SCALAR_OP_GENERIC(splice)
