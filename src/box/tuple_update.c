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

#include "tuple_update.h"

#include <stdio.h>
#include <stdbool.h>

#include "say.h"
#include "error.h"
#include "diag.h"
#include "trivia/util.h"
#include "third_party/queue.h"
#include <msgpuck/msgpuck.h>
#include <bit/int96.h>
#include <salad/rope.h>
#include "column_mask.h"


/** UPDATE request implementation.
 * UPDATE request is represented by a sequence of operations, each
 * working with a single field. There also are operations which
 * add or remove fields. Only one operation on the same field
 * is allowed.
 *
 * Supported field change operations are: SET, ADD, SUBTRACT;
 * bitwise AND, XOR and OR; SPLICE.
 *
 * Supported tuple change operations are: SET, DELETE, INSERT,
 * PUSH and POP.
 * If the number of fields in a tuple is altered by an operation,
 * field index of all following operations is evaluated against the
 * new tuple.
 *
 * Despite the allowed complexity, a typical use case for UPDATE
 * is when the operation count is much less than field count in
 * a tuple.
 *
 * With the common case in mind, UPDATE tries to minimize
 * the amount of unnecessary temporary tuple copies.
 *
 * First, operations are parsed and initialized. Then, the
 * resulting tuple length is calculated. A new tuple is allocated.
 * Finally, operations are applied sequentially, each copying data
 * from the old tuple to the new tuple.
 *
 * With this approach, cost of UPDATE is proportional to O(tuple
 * length) + O(C * log C), where C is the number of operations in
 * the request, and data is copied from the old tuple to the new
 * one only once.
 *
 * As long as INSERT, DELETE, PUSH and POP change the relative
 * field order, an auxiliary data structure is necessary to look
 * up fields in the "old" tuple by field number. Such field
 * index is built on demand, using "rope" data structure.
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
struct tuple_update
{
	tuple_update_alloc_func alloc;
	void *alloc_ctx;
	struct rope *rope;
	struct update_op *ops;
	uint32_t op_count;
	int index_base; /* 0 for C and 1 for Lua */
	/** A bitmask of all columns modified by this update */
	uint64_t column_mask;
};

/** Argument of SET (and INSERT) operation. */
struct op_set_arg {
	uint32_t length;
	const char *value;
};

/** Argument of DELETE operation. */
struct op_del_arg {
	uint32_t count;
};

/**
 * MsgPack format code of an arithmetic argument or result.
 * MsgPack codes are not used to simplify type calculation.
 */
enum arith_type {
	AT_DOUBLE = 0, /* MP_DOUBLE */
	AT_FLOAT = 1, /* MP_FLOAT */
	AT_INT = 2 /* MP_INT/MP_UINT */
};

/**
 * Argument (left and right) and result of ADD, SUBTRACT.
 *
 * To perform an arithmetic operation, update first loads
 * left and right arguments into corresponding value objects,
 * then performs arithmetics on types of arguments, thus
 * calculating the type of the result, and then
 * performs the requested operation according to the calculated
 * type rules.
 *
 * The rules are as follows:
 * - when one of the argument types is double, the result is
 *   double
 * - when one of the argument types is float, the result is
 *   float
 * - for integer arguments, the result type code depends on
 *   the range in which falls the result of the operation.
 *   If the result is in negative range, it's MP_INT, otherwise
 *   it's MP_UINT. If the result is out of bounds of (-2^63,
 *   2^64), and exception is raised for overflow.
 */
struct op_arith_arg {
	enum arith_type type;
	union {
		double dbl;
		float flt;
		struct int96_num int96;
	};
};

/** Argument of AND, XOR, OR operations. */
struct op_bit_arg {
	uint64_t val;
};

/** Argument of SPLICE. */
struct op_splice_arg {
	int32_t offset;	   /** splice position */
	int32_t cut_length;    /** cut this many bytes. */
	const char *paste; /** paste what? */
	uint32_t paste_length;  /** paste this many bytes. */

	/** Offset of the tail in the old field */
	int32_t tail_offset;
	/** Size of the tail. */
	int32_t tail_length;
};

union update_op_arg {
	struct op_set_arg set;
	struct op_del_arg del;
	struct op_arith_arg arith;
	struct op_bit_arg bit;
	struct op_splice_arg splice;
};

struct update_field;
struct update_op;

typedef int (*do_op_func)(struct tuple_update *update, struct update_op *op);
typedef int (*read_arg_func)(int index_base, struct update_op *op,
			     const char **expr);
typedef void (*store_op_func)(union update_op_arg *arg, const char *in,
			      char *out);

/** A set of functions and properties to initialize and do an op. */
struct update_op_meta {
	read_arg_func read_arg;
	do_op_func do_op;
	store_op_func store;
	/* Argument count */
	uint32_t args;
};

/** A single UPDATE operation. */
struct update_op {
	const struct update_op_meta *meta;
	union update_op_arg arg;
	/* Subject field no. */
	int32_t field_no;
	uint32_t new_field_len;
	uint8_t opcode;
};

/**
 * We can have more than one operation on the same field.
 * A descriptor of one changed field.
 */
struct update_field {
	/** UPDATE operation against the first field in the range. */
	struct update_op *op;
	/** Points at start of field *data* in the old tuple. */
	const char *old;
	/** End of the old field. */
	const char *tail;
	/**
	 * Length of the "tail" in the old tuple from end
	 * of old data to the beginning of the field in the
	 * next update_field structure.
	 */
	uint32_t tail_len;
};

static void
update_field_init(struct update_field *field,
		  const char *old, uint32_t old_len, uint32_t tail_len)
{
	field->op = NULL;
	field->old = old;
	field->tail = old + old_len;
	field->tail_len = tail_len;
}

/* {{{ read_arg helpers */

/** Read a field index or any other integer field. */
static inline int
mp_read_i32(int index_base, struct update_op *op,
	    const char **expr, int32_t *ret)
{
	if (mp_read_int32(expr, ret) == 0)
		return 0;
	diag_set(ClientError, ER_UPDATE_ARG_TYPE, (char)op->opcode,
		 index_base + op->field_no, "an integer");
	return -1;
}

static inline int
mp_read_uint(int index_base, struct update_op *op,
	     const char **expr, uint64_t *ret)
{
	if (mp_typeof(**expr) == MP_UINT) {
		*ret = mp_decode_uint(expr);
		return 0;
	} else {
		diag_set(ClientError, ER_UPDATE_ARG_TYPE, (char)op->opcode,
			 index_base + op->field_no, "a positive integer");
		return -1;
	}
}

/**
 * Load an argument of an arithmetic operation either from tuple
 * or from the UPDATE command.
 */
static inline int
mp_read_arith_arg(int index_base, struct update_op *op,
		  const char **expr, struct op_arith_arg *ret)
{
	if (mp_typeof(**expr) == MP_UINT) {
		ret->type = AT_INT;
		int96_set_unsigned(&ret->int96, mp_decode_uint(expr));
	} else if (mp_typeof(**expr) == MP_INT) {
		ret->type = AT_INT;
		int96_set_signed(&ret->int96, mp_decode_int(expr));
	} else if (mp_typeof(**expr) == MP_DOUBLE) {
		ret->type = AT_DOUBLE;
		ret->dbl = mp_decode_double(expr);
	} else if (mp_typeof(**expr) == MP_FLOAT) {
		ret->type = AT_FLOAT;
		ret->flt = mp_decode_float(expr);
	} else {
		diag_set(ClientError, ER_UPDATE_ARG_TYPE, (char)op->opcode,
			 index_base + op->field_no, "a number");
		return -1;
	}
	return 0;
}

static inline int
mp_read_str(int index_base, struct update_op *op,
	    const char **expr, uint32_t *len, const char **ret)
{
	if (mp_typeof(**expr) != MP_STR) {
		diag_set(ClientError, ER_UPDATE_ARG_TYPE, (char) op->opcode,
			 index_base + op->field_no, "a string");
		return -1;
	}
	*ret = mp_decode_str(expr, len); /* value */
	return 0;
}

/* }}} read_arg helpers */

/* {{{ read_arg */

static int
read_arg_set(int index_base, struct update_op *op,
	     const char **expr)
{
	(void)index_base;
	op->arg.set.value = *expr;
	mp_next(expr);
	op->arg.set.length = (uint32_t) (*expr - op->arg.set.value);
	return 0;
}

static int
read_arg_insert(int index_base, struct update_op *op,
		const char **expr)
{
	return read_arg_set(index_base, op, expr);
}

static int
read_arg_delete(int index_base, struct update_op *op,
		const char **expr)
{
	if (mp_typeof(**expr) == MP_UINT) {
		op->arg.del.count = (uint32_t) mp_decode_uint(expr);
		return 0;
	} else {
		diag_set(ClientError, ER_UPDATE_ARG_TYPE, (char)op->opcode,
			 index_base + op->field_no,
			 "a number of fields to delete");
		return -1;
	}
}

static int
read_arg_arith(int index_base, struct update_op *op,
	       const char **expr)
{
	return mp_read_arith_arg(index_base, op, expr, &op->arg.arith);
}

static int
read_arg_bit(int index_base, struct update_op *op,
	     const char **expr)
{
	struct op_bit_arg *arg = &op->arg.bit;
	return mp_read_uint(index_base, op, expr, &arg->val);
}

static int
read_arg_splice(int index_base, struct update_op *op,
		const char **expr)
{
	struct op_splice_arg *arg = &op->arg.splice;
	if (mp_read_i32(index_base, op, expr, &arg->offset))
		return -1;
	/* cut length */
	if (mp_read_i32(index_base, op, expr, &arg->cut_length))
		return -1;
	 /* value */
	return mp_read_str(index_base, op, expr, &arg->paste_length,
			   &arg->paste);
}

/* }}} read_arg */

/* {{{ do_op helpers */

static inline int
op_adjust_field_no(struct tuple_update *update, struct update_op *op,
		   int32_t field_max)
{
	if (op->field_no >= 0) {
		if (op->field_no < field_max)
			return 0;
		diag_set(ClientError, ER_NO_SUCH_FIELD, update->index_base +
			 op->field_no);
		return -1;
	} else {
		if (op->field_no + field_max >= 0) {
			op->field_no += field_max;
			return 0;
		}
		diag_set(ClientError, ER_NO_SUCH_FIELD, op->field_no);
		return -1;
	}
}

static inline double
cast_arith_arg_to_double(struct op_arith_arg arg)
{
	if (arg.type == AT_DOUBLE) {
		return arg.dbl;
	} else if (arg.type == AT_FLOAT) {
		return arg.flt;
	} else {
		assert(arg.type == AT_INT);
		if (int96_is_uint64(&arg.int96)) {
			return int96_extract_uint64(&arg.int96);
		} else {
			assert(int96_is_neg_int64(&arg.int96));
			return int96_extract_neg_int64(&arg.int96);
		}
	}
}

/** Return the MsgPack size of an arithmetic operation result. */
static inline uint32_t
mp_sizeof_op_arith_arg(struct op_arith_arg arg)
{
	if (arg.type == AT_INT) {
		if (int96_is_uint64(&arg.int96)) {
			uint64_t val = int96_extract_uint64(&arg.int96);
			return mp_sizeof_uint(val);
		} else {
			int64_t val = int96_extract_neg_int64(&arg.int96);
			return mp_sizeof_int(val);
		}
	} else if (arg.type == AT_DOUBLE) {
		return mp_sizeof_double(arg.dbl);
	} else {
		assert(arg.type == AT_FLOAT);
		return mp_sizeof_float(arg.flt);
	}
}

static inline int
make_arith_operation(struct op_arith_arg arg1, struct op_arith_arg arg2,
		     char opcode, uint32_t err_fieldno, struct op_arith_arg *ret)
{
	enum arith_type lowest_type = arg1.type;
	if (arg1.type > arg2.type)
		lowest_type = arg2.type;

	if (lowest_type == AT_INT) {
		switch(opcode) {
		case '+':
			int96_add(&arg1.int96, &arg2.int96);
			break;
		case '-':
			int96_invert(&arg2.int96);
			int96_add(&arg1.int96, &arg2.int96);
			break;
		default:
			unreachable(); /* checked by update_read_ops */
			break;
		}
		if (!int96_is_uint64(&arg1.int96) &&
		    !int96_is_neg_int64(&arg1.int96)) {
			diag_set(ClientError,
				 ER_UPDATE_INTEGER_OVERFLOW,
				 opcode, err_fieldno);
			return -1;
		}
		*ret = arg1;
		return 0;
	} else {
		/* At least one of operands is double or float */
		double a = cast_arith_arg_to_double(arg1);
		double b = cast_arith_arg_to_double(arg2);
		double c;
		switch(opcode) {
		case '+': c = a + b; break;
		case '-': c = a - b; break;
		default:
			diag_set(ClientError, ER_UPDATE_ARG_TYPE, (char)opcode,
				 err_fieldno, "a positive integer");
			return -1;
		}
		if (lowest_type == AT_DOUBLE) {
			/* result is DOUBLE */
			ret->type = AT_DOUBLE;
			ret->dbl = c;
		} else {
			/* result is FLOAT */
			assert(lowest_type == AT_FLOAT);
			ret->type = AT_FLOAT;
			ret->flt = (float)c;
		}
	}
	return 0;
}

/* }}} do_op helpers */

/* {{{ do_op */

static int
do_op_insert(struct tuple_update *update, struct update_op *op)
{
	if (op_adjust_field_no(update, op, rope_size(update->rope) + 1))
		return -1;
	struct update_field *field = (struct update_field *)
		update->alloc(update->alloc_ctx, sizeof(*field));
	if (field == NULL)
		return -1;
	update_field_init(field, op->arg.set.value, op->arg.set.length, 0);
	return rope_insert(update->rope, op->field_no, field, 1);
}

static int
do_op_set(struct tuple_update *update, struct update_op *op)
{
	/* intepret '=' for n +1 field as insert */
	if (op->field_no == (int32_t) rope_size(update->rope))
		return do_op_insert(update, op);
	if (op_adjust_field_no(update, op, rope_size(update->rope)))
		return -1;
	struct update_field *field = (struct update_field *)
		rope_extract(update->rope, op->field_no);
	if (field == NULL)
		return -1;
	/* Ignore the previous op, if any. */
	field->op = op;
	op->new_field_len = op->arg.set.length;
	return 0;
}

static int
do_op_delete(struct tuple_update *update, struct update_op *op)
{
	if (op_adjust_field_no(update, op, rope_size(update->rope)))
		return -1;
	uint32_t delete_count = op->arg.del.count;

	if ((uint64_t) op->field_no + delete_count > rope_size(update->rope))
		delete_count = rope_size(update->rope) - op->field_no;

	if (delete_count == 0) {
		diag_set(ClientError, ER_UPDATE_FIELD,
			 update->index_base + op->field_no,
			 "cannot delete 0 fields");
		return -1;
	}

	for (uint32_t u = 0; u < delete_count; u++)
		rope_erase(update->rope, op->field_no);
	return 0;
}

static int
do_op_arith(struct tuple_update *update, struct update_op *op)
{
	if (op_adjust_field_no(update, op, rope_size(update->rope)))
		return -1;

	struct update_field *field = (struct update_field *)
		rope_extract(update->rope, op->field_no);
	if (field == NULL)
		return -1;
	if (field->op) {
		diag_set(ClientError, ER_UPDATE_FIELD,
			 update->index_base + op->field_no,
			 "double update of the same field");
		return -1;
	}
	const char *old = field->old;
	struct op_arith_arg left_arg;
	if (mp_read_arith_arg(update->index_base, op, &old, &left_arg))
		return -1;

	struct op_arith_arg right_arg = op->arg.arith;
	if (make_arith_operation(left_arg, right_arg, op->opcode,
				 update->index_base + op->field_no,
				 &op->arg.arith))
		return -1;
	field->op = op;
	op->new_field_len = mp_sizeof_op_arith_arg(op->arg.arith);
	return 0;
}

static int
do_op_bit(struct tuple_update *update, struct update_op *op)
{
	if (op_adjust_field_no(update, op, rope_size(update->rope)))
		return -1;
	struct update_field *field = (struct update_field *)
		rope_extract(update->rope, op->field_no);
	if (field == NULL)
		return -1;
	struct op_bit_arg *arg = &op->arg.bit;
	if (field->op) {
		diag_set(ClientError, ER_UPDATE_FIELD,
			 update->index_base + op->field_no,
			 "double update of the same field");
		return -1;
	}
	const char *old = field->old;
	uint64_t val;
	if (mp_read_uint(update->index_base, op, &old, &val))
		return -1;
	switch (op->opcode) {
	case '&':
		arg->val &= val;
		break;
	case '^':
		arg->val ^= val;
		break;
	case '|':
		arg->val |= val;
		break;
	default:
		unreachable(); /* checked by update_read_ops */
	}
	field->op = op;
	op->new_field_len = mp_sizeof_uint(arg->val);
	return 0;
}

static int
do_op_splice(struct tuple_update *update, struct update_op *op)
{
	if (op_adjust_field_no(update, op, rope_size(update->rope)))
		return -1;
	struct update_field *field = (struct update_field *)
		rope_extract(update->rope, op->field_no);
	if (field == NULL)
		return -1;
	if (field->op) {
		diag_set(ClientError, ER_UPDATE_FIELD,
			 update->index_base + op->field_no,
			 "double update of the same field");
		return -1;
	}

	struct op_splice_arg *arg = &op->arg.splice;

	const char *in = field->old;
	int32_t str_len;
	if (mp_read_str(update->index_base, op, &in, (uint32_t *) &str_len, &in))
		return -1;

	if (arg->offset < 0) {
		if (-arg->offset > str_len + 1) {
			diag_set(ClientError, ER_SPLICE,
				 update->index_base + op->field_no,
				 "offset is out of bound");
			return -1;
		}
		arg->offset = arg->offset + str_len + 1;
	} else if (arg->offset - update->index_base >= 0) {
		arg->offset -= update->index_base;
		if (arg->offset > str_len)
			arg->offset = str_len;
	} else /* (offset <= 0) */ {
		diag_set(ClientError, ER_SPLICE,
			 update->index_base + op->field_no,
			 "offset is out of bound");
		return -1;
	}

	assert(arg->offset >= 0 && arg->offset <= str_len);

	if (arg->cut_length < 0) {
		if (-arg->cut_length > (str_len - arg->offset))
			arg->cut_length = 0;
		else
			arg->cut_length += str_len - arg->offset;
	} else if (arg->cut_length > str_len - arg->offset) {
		arg->cut_length = str_len - arg->offset;
	}

	assert(arg->offset <= str_len);

	/* Fill tail part */
	arg->tail_offset = arg->offset + arg->cut_length;
	arg->tail_length = str_len - arg->tail_offset;


	field->op = op;
	/* Record the new field length (maximal). */
	op->new_field_len = mp_sizeof_str(arg->offset + arg->paste_length +
					  arg->tail_length);
	return 0;
}

/* }}} do_op */

/* {{{ store_op */

static void
store_op_set(struct op_set_arg *arg, const char *in, char *out)
{
	(void)in;
	memcpy(out, arg->value, arg->length);
}

static void
store_op_insert(struct op_set_arg *arg, const char *in, char *out)
{
	(void)in;
	memcpy(out, arg->value, arg->length);
}

static void
store_op_arith(struct op_arith_arg *arg, const char *in, char *out)
{
	(void)in;
	if (arg->type == AT_INT) {
		if (int96_is_uint64(&arg->int96)) {
			mp_encode_uint(out, int96_extract_uint64(&arg->int96));
		} else {
			assert(int96_is_neg_int64(&arg->int96));
			mp_encode_int(out, int96_extract_neg_int64(&arg->int96));
		}
	} else if (arg->type == AT_DOUBLE) {
		mp_encode_double(out, arg->dbl);
	} else {
		assert(arg->type == AT_FLOAT);
		mp_encode_float(out, arg->flt);
	}
}

static void
store_op_bit(struct op_bit_arg *arg, const char *in, char *out)
{
	(void)in;
	mp_encode_uint(out, arg->val);
}

static void
store_op_splice(struct op_splice_arg *arg, const char *in, char *out)
{
	uint32_t new_str_len = arg->offset + arg->paste_length
		+ arg->tail_length;

	(void) mp_decode_strl(&in);

	out = mp_encode_strl(out, new_str_len);
	memcpy(out, in, arg->offset);               /* copy field head. */
	out = out + arg->offset;
	memcpy(out, arg->paste, arg->paste_length); /* copy the paste */
	out = out + arg->paste_length;
	memcpy(out, in + arg->tail_offset, arg->tail_length); /* copy tail */
}

/* }}} store_op */

static const struct update_op_meta op_set =
	{ read_arg_set, do_op_set, (store_op_func) store_op_set, 3 };
static const struct update_op_meta op_insert =
	{ read_arg_insert, do_op_insert, (store_op_func) store_op_insert, 3 };
static const struct update_op_meta op_arith =
	{ read_arg_arith, do_op_arith, (store_op_func) store_op_arith, 3 };
static const struct update_op_meta op_bit =
	{ read_arg_bit, do_op_bit, (store_op_func) store_op_bit, 3 };
static const struct update_op_meta op_splice =
	{ read_arg_splice, do_op_splice, (store_op_func) store_op_splice, 5 };
static const struct update_op_meta op_delete =
	{ read_arg_delete, do_op_delete, (store_op_func) NULL, 3 };

/** Split a range of fields in two, allocating update_field
 * context for the new range.
 */
static void *
update_field_split(void *split_ctx, void *data, size_t size, size_t offset)
{
	(void)size;
	struct tuple_update *update = (struct tuple_update *) split_ctx;

	struct update_field *prev = (struct update_field *) data;

	struct update_field *next = (struct update_field *)
			update->alloc(update->alloc_ctx, sizeof(*next));
	if (next == NULL)
		return NULL;
	assert(offset > 0 && prev->tail_len > 0);

	const char *field = prev->tail;
	const char *end = field + prev->tail_len;

	for (uint32_t i = 1; i < offset; i++) {
		mp_next(&field);
	}

	prev->tail_len = field - prev->tail;
	const char *f = field;
	mp_next(&f);
	uint32_t field_len = f - field;

	update_field_init(next, field, field_len, end - field - field_len);
	return next;
}

/** Free rope node - do nothing, since we use a pool allocator. */
static void
region_alloc_free_stub(void *ctx, void *mem)
{
	(void) ctx;
	(void) mem;
}

/**
 * We found a tuple to do the update on. Prepare a rope
 * to perform operations on.
 * @param update Update meta.
 * @param tuple_data MessagePack array without the array header.
 * @param tuple_data_end End of the @tuple_data.
 * @param field_count Field count in @tuple_data.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
int
update_create_rope(struct tuple_update *update, const char *tuple_data,
		   const char *tuple_data_end, uint32_t field_count)
{
	update->rope = rope_new(update_field_split, update, update->alloc,
				region_alloc_free_stub, update->alloc_ctx);
	if (update->rope == NULL)
		return -1;
	/* Initialize the rope with the old tuple. */

	struct update_field *first = (struct update_field *)
			update->alloc(update->alloc_ctx, sizeof(*first));
	if (first == NULL)
		return -1;
	const char *field = tuple_data;
	const char *end = tuple_data_end;
	if (field == end)
		return 0;

	/* Add first field to rope */
	mp_next(&tuple_data);
	uint32_t field_len = tuple_data - field;
	update_field_init(first, field, field_len,
			  end - field - field_len);

	return rope_append(update->rope, first, field_count);
}

static uint32_t
update_calc_tuple_length(struct tuple_update *update)
{
	uint32_t res = mp_sizeof_array(rope_size(update->rope));
	struct rope_iter it;
	struct rope_node *node;

	rope_iter_create(&it, update->rope);
	for (node = rope_iter_start(&it); node; node = rope_iter_next(&it)) {
		struct update_field *field =
				(struct update_field *) rope_leaf_data(node);
		uint32_t field_len = (field->op ? field->op->new_field_len :
				      (uint32_t)(field->tail - field->old));
		res += field_len + field->tail_len;
	}

	return res;
}

static uint32_t
update_write_tuple(struct tuple_update *update, char *buffer, char *buffer_end)
{
	char *new_data = buffer;
	new_data = mp_encode_array(new_data, rope_size(update->rope));

	(void) buffer_end;

	uint32_t total_field_count = 0;

	struct rope_iter it;
	struct rope_node *node;

	rope_iter_create(&it, update->rope);
	for (node = rope_iter_start(&it); node; node = rope_iter_next(&it)) {
		struct update_field *field = (struct update_field *)
				rope_leaf_data(node);
		uint32_t field_count = rope_leaf_size(node);
		const char *old_field = field->old;
		struct update_op *op = field->op;
		if (op) {
			op->meta->store(&op->arg, old_field, new_data);
			new_data += op->new_field_len;
		} else {
			uint32_t field_len = field->tail - field->old;
			memcpy(new_data, old_field, field_len);
			new_data += field_len;
		}
		/* Copy tail_len from the old tuple. */
		assert(field->tail_len == 0 || field_count > 1);
		if (field_count > 1) {
			memcpy(new_data, field->tail, field->tail_len);
			new_data += field->tail_len;
		}
		total_field_count += field_count;
	}

	assert(rope_size(update->rope) == total_field_count);
	assert(new_data <= buffer_end);
	return new_data - buffer; /* real_tuple_size */
}

static const struct update_op_meta *
update_op_by(char opcode)
{
	switch (opcode) {
	case '=':
		return &op_set;
	case '+':
	case '-':
		return &op_arith;
	case '&':
	case '|':
	case '^':
		return &op_bit;
	case ':':
		return &op_splice;
	case '#':
		return &op_delete;
	case '!':
		return &op_insert;
	default:
		diag_set(ClientError, ER_UNKNOWN_UPDATE_OP);
		return NULL;
	}
}

/**
 * Read and check update operations and fill column mask.
 *
 * @param[out] update Update meta.
 * @param expr MessagePack array of operations.
 * @param expr_end End of the @expr.
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
update_read_ops(struct tuple_update *update, const char *expr,
		const char *expr_end, int32_t field_count_hint)
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

	/* Read update operations.  */
	update->ops = (struct update_op *) update->alloc(update->alloc_ctx,
				update->op_count * sizeof(struct update_op));
	if (update->ops == NULL)
		return -1;
	struct update_op *op = update->ops;
	struct update_op *ops_end = op + update->op_count;
	for (; op < ops_end; op++) {
		if (mp_typeof(*expr) != MP_ARRAY) {
			diag_set(ClientError, ER_ILLEGAL_PARAMS,
				 "update operation"
				 " must be an array {op,..}");
			return -1;
		}
		/* Read operation */
		uint32_t args, len;
		args = mp_decode_array(&expr);
		if (args < 1) {
			diag_set(ClientError, ER_ILLEGAL_PARAMS,
				 "update operation must be an "
				 "array {op,..}, got empty array");
			return -1;
		}
		if (mp_typeof(*expr) != MP_STR) {
			diag_set(ClientError, ER_ILLEGAL_PARAMS,
				 "update operation name must be a string");
			return -1;
		}

		op->opcode = *mp_decode_str(&expr, &len);
		op->meta = update_op_by(op->opcode);
		if (op->meta == NULL)
			return -1;
		if (args != op->meta->args) {
			diag_set(ClientError, ER_UNKNOWN_UPDATE_OP);
			return -1;
		}
		if (mp_typeof(*expr) != MP_INT && mp_typeof(*expr) != MP_UINT) {
			diag_set(ClientError, ER_ILLEGAL_PARAMS,
				 "field id must be a number");
			return -1;
		}
		int32_t field_no;
		if (mp_read_i32(update->index_base, op, &expr, &field_no))
			return -1;
		if (field_no - update->index_base >= 0) {
			op->field_no = field_no - update->index_base;
		} else if (field_no < 0) {
			op->field_no = field_no;
		} else {
			diag_set(ClientError, ER_NO_SUCH_FIELD, field_no);
			return -1;
		}
		if (op->meta->read_arg(update->index_base, op, &expr))
			return -1;

		/*
		 * Continue collecting the changed columns
		 * only if there are unset bits in the mask.
		 */
		if (column_mask != COLUMN_MASK_FULL) {
			if (op->field_no >= 0)
				field_no = op->field_no;
			else if (op->opcode != '!')
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
			if (op->opcode == '!')
				++field_count_hint;
			else if (op->opcode == '#')
				field_count_hint -= (int32_t) op->arg.del.count;

			if (op->opcode == '!' || op->opcode == '#')
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
update_do_ops(struct tuple_update *update, const char *old_data,
	      const char *old_data_end, uint32_t part_count)
{
	if (update_create_rope(update, old_data, old_data_end, part_count) != 0)
		return -1;
	struct update_op *op = update->ops;
	struct update_op *ops_end = op + update->op_count;
	for (; op < ops_end; op++) {
		if (op->meta->do_op(update, op))
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
upsert_do_ops(struct tuple_update *update, const char *old_data,
	      const char *old_data_end, uint32_t part_count,
	      bool suppress_error)
{
	if (update_create_rope(update, old_data, old_data_end, part_count) != 0)
		return -1;
	struct update_op *op = update->ops;
	struct update_op *ops_end = op + update->op_count;
	for (; op < ops_end; op++) {
		if (op->meta->do_op(update, op) == 0)
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
update_init(struct tuple_update *update,
	    tuple_update_alloc_func alloc, void *alloc_ctx,
	    int index_base)
{
	memset(update, 0, sizeof(*update));
	update->alloc = alloc;
	update->alloc_ctx = alloc_ctx;
	/*
	 * Base field offset, e.g. 0 for C and 1 for Lua. Used only for
	 * error messages. All fields numbers must be zero-based!
	 */
	update->index_base = index_base;
}

const char *
update_finish(struct tuple_update *update, uint32_t *p_tuple_len)
{
	uint32_t tuple_len = update_calc_tuple_length(update);
	char *buffer = (char *) update->alloc(update->alloc_ctx, tuple_len);
	if (buffer == NULL)
		return NULL;
	*p_tuple_len = update_write_tuple(update, buffer, buffer + tuple_len);
	return buffer;
}

int
tuple_update_check_ops(tuple_update_alloc_func alloc, void *alloc_ctx,
		       const char *expr, const char *expr_end, int index_base)
{
	struct tuple_update update;
	update_init(&update, alloc, alloc_ctx, index_base);
	return update_read_ops(&update, expr, expr_end, 0);
}

const char *
tuple_update_execute(tuple_update_alloc_func alloc, void *alloc_ctx,
		     const char *expr,const char *expr_end,
		     const char *old_data, const char *old_data_end,
		     uint32_t *p_tuple_len, int index_base,
		     uint64_t *column_mask)
{
	struct tuple_update update;
	update_init(&update, alloc, alloc_ctx, index_base);
	uint32_t field_count = mp_decode_array(&old_data);

	if (update_read_ops(&update, expr, expr_end, field_count) != 0)
		return NULL;
	if (update_do_ops(&update, old_data, old_data_end, field_count))
		return NULL;
	if (column_mask)
		*column_mask = update.column_mask;

	return update_finish(&update, p_tuple_len);
}

const char *
tuple_upsert_execute(tuple_update_alloc_func alloc, void *alloc_ctx,
		     const char *expr,const char *expr_end,
		     const char *old_data, const char *old_data_end,
		     uint32_t *p_tuple_len, int index_base, bool suppress_error,
		     uint64_t *column_mask)
{
	struct tuple_update update;
	update_init(&update, alloc, alloc_ctx, index_base);
	uint32_t field_count = mp_decode_array(&old_data);

	if (update_read_ops(&update, expr, expr_end, field_count) != 0)
		return NULL;
	if (upsert_do_ops(&update, old_data, old_data_end, field_count,
			  suppress_error))
		return NULL;
	if (column_mask)
		*column_mask = update.column_mask;

	return update_finish(&update, p_tuple_len);
}

const char *
tuple_upsert_squash(tuple_update_alloc_func alloc, void *alloc_ctx,
		    const char *expr1, const char *expr1_end,
		    const char *expr2, const char *expr2_end,
		    size_t *result_size, int index_base)
{
	const char *expr[2] = {expr1, expr2};
	const char *expr_end[2] = {expr1_end, expr2_end};
	struct tuple_update update[2];
	for (int j = 0; j < 2; j++) {
		update_init(&update[j], alloc, alloc_ctx, index_base);
		if (update_read_ops(&update[j], expr[j], expr_end[j], 0))
			return NULL;
		mp_decode_array(&expr[j]);
		int32_t prev_field_no = index_base - 1;
		for (uint32_t i = 0; i < update[j].op_count; i++) {
			struct update_op *op = &update[j].ops[i];
			if (op->opcode != '+' && op->opcode != '-' &&
			    op->opcode != '=')
				return NULL;
			if (op->field_no <= prev_field_no)
				return NULL;
			prev_field_no = op->field_no;
		}
	}
	size_t possible_size = expr1_end - expr1 + expr2_end - expr2;
	const uint32_t space_for_arr_tag = 5;
	char *buf = (char *)alloc(alloc_ctx,
				  possible_size + space_for_arr_tag);
	if (buf == NULL)
		return NULL;
	/* reserve some space for mp array header */
	char *res_ops = buf + space_for_arr_tag;
	uint32_t res_count = 0; /* number of resulting operations */

	uint32_t op_count[2] = {update[0].op_count, update[1].op_count};
	uint32_t op_no[2] = {0, 0};
	while (op_no[0] < op_count[0] || op_no[1] < op_count[1]) {
		res_count++;
		struct update_op *op[2] = {update[0].ops + op_no[0],
					   update[1].ops + op_no[1]};
		/*
		 * from:
		 * 0 - take op from first update,
		 * 1 - take op from second update,
		 * 2 - merge both ops
		 */
		uint32_t from;
		uint32_t has[2] = {op_no[0] < op_count[0], op_no[1] < op_count[1]};
		assert(has[0] || has[1]);
		if (has[0] && has[1]) {
			from = op[0]->field_no < op[1]->field_no ? 0 :
			       op[0]->field_no > op[1]->field_no ? 1 : 2;
		} else {
			assert(has[0] != has[1]);
			from = has[1];
		}
		if (from == 2 && op[1]->opcode == '=') {
			/*
			 * If an operation from the second upsert is '='
			 * it is just overwrites any op from the first upsert.
			 * So we just skip op from the first upsert and
			 * copy op from the second
			 */
			mp_next(&expr[0]);
			op_no[0]++;
			from = 1;
		}
		if (from < 2) {
			/* take op from one of upserts */
			const char *copy = expr[from];
			mp_next(&expr[from]);
			size_t copy_size = expr[from] - copy;
			memcpy(res_ops, copy, copy_size);
			res_ops += copy_size;
			op_no[from]++;
			continue;
		}
		/* merge: apply second '+' or '-' */
		assert(op[1]->opcode == '+' || op[1]->opcode == '-');
		if (op[0]->opcode == '-') {
			op[0]->opcode = '+';
			int96_invert(&op[0]->arg.arith.int96);
		}
		struct op_arith_arg res;
		if (make_arith_operation(op[0]->arg.arith, op[1]->arg.arith,
					 op[1]->opcode,
					 update[0].index_base +
					 op[0]->field_no, &res))
			return NULL;
		res_ops = mp_encode_array(res_ops, 3);
		res_ops = mp_encode_str(res_ops,
					(const char *)&op[0]->opcode, 1);
		res_ops = mp_encode_uint(res_ops,
					 op[0]->field_no +
						 update[0].index_base);
		store_op_arith(&res, NULL, res_ops);
		res_ops += mp_sizeof_op_arith_arg(res);
		mp_next(&expr[0]);
		mp_next(&expr[1]);
		op_no[0]++;
		op_no[1]++;
	}
	assert(op_no[0] == op_count[0] && op_no[1] == op_count[1]);
	assert(expr[0] == expr_end[0] && expr[1] == expr_end[1]);
	char *arr_start = buf + space_for_arr_tag -
		mp_sizeof_array(res_count);
	mp_encode_array(arr_start, res_count);
	*result_size = res_ops - arr_start;
	return arr_start;
}
