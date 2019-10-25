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
#include <stdbool.h>

#include "say.h"
#include "error.h"
#include "diag.h"
#include "trivia/util.h"
#include "third_party/queue.h"
#include <msgpuck/msgpuck.h>
#include <bit/int96.h>
#include "column_mask.h"
#include "mp_extension_types.h"
#include "mp_decimal.h"
#include "fiber.h"
#include "tuple_dictionary.h"
#include "tuple_format.h"
#include "tt_static.h"

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

static inline void *
xrow_update_alloc(struct region *region, size_t size)
{
	void *ptr = region_aligned_alloc(region, size, alignof(uint64_t));
	if (ptr == NULL)
		diag_set(OutOfMemory, size, "region_aligned_alloc",
			 "update internals");
	return ptr;
}

/** Update internal state */
struct xrow_update
{
	struct xrow_update_rope *rope;
	struct xrow_update_op *ops;
	uint32_t op_count;
	int index_base; /* 0 for C and 1 for Lua */
	/** A bitmask of all columns modified by this update */
	uint64_t column_mask;
};

/** Argument of SET (and INSERT) operation. */
struct xrow_update_arg_set {
	uint32_t length;
	const char *value;
};

/** Argument of DELETE operation. */
struct xrow_update_arg_del {
	uint32_t count;
};

/**
 * MsgPack format code of an arithmetic argument or result.
 * MsgPack codes are not used to simplify type calculation.
 */
enum xrow_update_arith_type {
	XUPDATE_TYPE_DECIMAL = 0, /* MP_EXT + MP_DECIMAL */
	XUPDATE_TYPE_DOUBLE = 1, /* MP_DOUBLE */
	XUPDATE_TYPE_FLOAT = 2, /* MP_FLOAT */
	XUPDATE_TYPE_INT = 3 /* MP_INT/MP_UINT */
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
struct xrow_update_arg_arith {
	enum xrow_update_arith_type type;
	union {
		double dbl;
		float flt;
		struct int96_num int96;
		decimal_t dec;
	};
};

/** Argument of AND, XOR, OR operations. */
struct xrow_update_arg_bit {
	uint64_t val;
};

/** Argument of SPLICE. */
struct xrow_update_arg_splice {
	int32_t offset;	   /** splice position */
	int32_t cut_length;    /** cut this many bytes. */
	const char *paste; /** paste what? */
	uint32_t paste_length;  /** paste this many bytes. */

	/** Offset of the tail in the old field */
	int32_t tail_offset;
	/** Size of the tail. */
	int32_t tail_length;
};

union xrow_update_arg {
	struct xrow_update_arg_set set;
	struct xrow_update_arg_del del;
	struct xrow_update_arg_arith arith;
	struct xrow_update_arg_bit bit;
	struct xrow_update_arg_splice splice;
};

struct xrow_update_field;
struct xrow_update_op;

static struct xrow_update_field *
xrow_update_field_split(struct region *region, struct xrow_update_field *data,
			size_t size, size_t offset);

#define ROPE_SPLIT_F xrow_update_field_split
#define ROPE_ALLOC_F xrow_update_alloc
#define rope_data_t struct xrow_update_field *
#define rope_ctx_t struct region *
#define rope_name xrow_update

#include "salad/rope.h"

typedef int (*xrow_update_op_do_f)(struct xrow_update *update,
				   struct xrow_update_op *op);
typedef int (*xrow_update_op_read_arg_f)(int index_base,
					 struct xrow_update_op *op,
					 const char **expr);
typedef void (*xrow_update_op_store_f)(union xrow_update_arg *arg,
				       const char *in, char *out);

/** A set of functions and properties to initialize and do an op. */
struct xrow_update_op_meta {
	xrow_update_op_read_arg_f read_arg;
	xrow_update_op_do_f do_op;
	xrow_update_op_store_f store;
	/* Argument count */
	uint32_t args;
};

/** A single UPDATE operation. */
struct xrow_update_op {
	const struct xrow_update_op_meta *meta;
	union xrow_update_arg arg;
	/* Subject field no. */
	int32_t field_no;
	uint32_t new_field_len;
	uint8_t opcode;
};

static inline const char *
xrow_update_op_field_str(const struct xrow_update_op *op)
{
	if (op->field_no >= 0)
		return tt_sprintf("%d", op->field_no + TUPLE_INDEX_BASE);
	else
		return tt_sprintf("%d", op->field_no);
}

static inline int
xrow_update_err_arg_type(const struct xrow_update_op *op,
			 const char *needed_type)
{
	diag_set(ClientError, ER_UPDATE_ARG_TYPE, op->opcode,
		 xrow_update_op_field_str(op), needed_type);
	return -1;
}

static inline int
xrow_update_err_int_overflow(const struct xrow_update_op *op)
{
	diag_set(ClientError, ER_UPDATE_INTEGER_OVERFLOW, op->opcode,
		 xrow_update_op_field_str(op));
	return -1;
}

static inline int
xrow_update_err_decimal_overflow(const struct xrow_update_op *op)
{
	diag_set(ClientError, ER_UPDATE_DECIMAL_OVERFLOW, op->opcode,
		 xrow_update_op_field_str(op));
	return -1;
}

static inline int
xrow_update_err_splice_bound(const struct xrow_update_op *op)
{
	diag_set(ClientError, ER_UPDATE_SPLICE, xrow_update_op_field_str(op),
		 "offset is out of bound");
	return -1;
}

static inline int
xrow_update_err_no_such_field(const struct xrow_update_op *op)
{
	diag_set(ClientError, ER_NO_SUCH_FIELD_NO, op->field_no >= 0 ?
		 TUPLE_INDEX_BASE + op->field_no : op->field_no);
	return -1;
}

static inline int
xrow_update_err(const struct xrow_update_op *op, const char *reason)
{
	diag_set(ClientError, ER_UPDATE_FIELD, xrow_update_op_field_str(op),
		 reason);
	return -1;
}

static inline int
xrow_update_err_double(const struct xrow_update_op *op)
{
	return xrow_update_err(op, "double update of the same field");
}

/**
 * We can have more than one operation on the same field.
 * A descriptor of one changed field.
 */
struct xrow_update_field {
	/** UPDATE operation against the first field in the range. */
	struct xrow_update_op *op;
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
xrow_update_field_init(struct xrow_update_field *field, const char *old,
		       uint32_t old_len, uint32_t tail_len)
{
	field->op = NULL;
	field->old = old;
	field->tail = old + old_len;
	field->tail_len = tail_len;
}

/* {{{ read_arg helpers */

/** Read a field index or any other integer field. */
static inline int
xrow_update_mp_read_int32(struct xrow_update_op *op, const char **expr,
			  int32_t *ret)
{
	if (mp_read_int32(expr, ret) == 0)
		return 0;
	return xrow_update_err_arg_type(op, "an integer");
}

static inline int
xrow_update_mp_read_uint(struct xrow_update_op *op, const char **expr,
			 uint64_t *ret)
{
	if (mp_typeof(**expr) == MP_UINT) {
		*ret = mp_decode_uint(expr);
		return 0;
	}
	return xrow_update_err_arg_type(op, "a positive integer");
}

/**
 * Load an argument of an arithmetic operation either from tuple
 * or from the UPDATE command.
 */
static inline int
xrow_mp_read_arg_arith(struct xrow_update_op *op, const char **expr,
		       struct xrow_update_arg_arith *ret)
{
	if (mp_typeof(**expr) == MP_UINT) {
		ret->type = XUPDATE_TYPE_INT;
		int96_set_unsigned(&ret->int96, mp_decode_uint(expr));
	} else if (mp_typeof(**expr) == MP_INT) {
		ret->type = XUPDATE_TYPE_INT;
		int96_set_signed(&ret->int96, mp_decode_int(expr));
	} else if (mp_typeof(**expr) == MP_DOUBLE) {
		ret->type = XUPDATE_TYPE_DOUBLE;
		ret->dbl = mp_decode_double(expr);
	} else if (mp_typeof(**expr) == MP_FLOAT) {
		ret->type = XUPDATE_TYPE_FLOAT;
		ret->flt = mp_decode_float(expr);
	} else if (mp_typeof(**expr) == MP_EXT) {
		int8_t ext_type;
		uint32_t len = mp_decode_extl(expr, &ext_type);
		switch (ext_type) {
		case MP_DECIMAL:
			ret->type = XUPDATE_TYPE_DECIMAL;
			decimal_unpack(expr, len, &ret->dec);
			break;
		default:
			goto err;
		}
	} else {
err:
		return xrow_update_err_arg_type(op, "a number");
	}
	return 0;
}

static inline int
xrow_update_mp_read_str(struct xrow_update_op *op, const char **expr,
			uint32_t *len, const char **ret)
{
	if (mp_typeof(**expr) == MP_STR) {
		*ret = mp_decode_str(expr, len); /* value */
		return 0;
	}
	return xrow_update_err_arg_type(op, "a string");
}

/* }}} read_arg helpers */

/* {{{ read_arg */

static int
xrow_update_read_arg_set(int index_base, struct xrow_update_op *op,
			 const char **expr)
{
	(void)index_base;
	op->arg.set.value = *expr;
	mp_next(expr);
	op->arg.set.length = (uint32_t) (*expr - op->arg.set.value);
	return 0;
}

static int
xrow_update_read_arg_insert(int index_base, struct xrow_update_op *op,
			    const char **expr)
{
	return xrow_update_read_arg_set(index_base, op, expr);
}

static int
xrow_update_read_arg_delete(int index_base, struct xrow_update_op *op,
			    const char **expr)
{
	(void) index_base;
	if (mp_typeof(**expr) == MP_UINT) {
		op->arg.del.count = (uint32_t) mp_decode_uint(expr);
		if (op->arg.del.count != 0)
			return 0;
		return xrow_update_err(op, "cannot delete 0 fields");
	}
	return xrow_update_err_arg_type(op, "a positive integer");
}

static int
xrow_update_read_arg_arith(int index_base, struct xrow_update_op *op,
			   const char **expr)
{
	(void) index_base;
	return xrow_mp_read_arg_arith(op, expr, &op->arg.arith);
}

static int
xrow_update_read_arg_bit(int index_base, struct xrow_update_op *op,
			 const char **expr)
{
	(void) index_base;
	struct xrow_update_arg_bit *arg = &op->arg.bit;
	return xrow_update_mp_read_uint(op, expr, &arg->val);
}

static int
xrow_update_read_arg_splice(int index_base, struct xrow_update_op *op,
			    const char **expr)
{
	struct xrow_update_arg_splice *arg = &op->arg.splice;
	if (xrow_update_mp_read_int32(op, expr, &arg->offset) != 0)
		return -1;
	if (arg->offset >= 0) {
		if (arg->offset - index_base < 0)
			return xrow_update_err_splice_bound(op);
		arg->offset -= index_base;
	}
	/* cut length */
	if (xrow_update_mp_read_int32(op, expr, &arg->cut_length) != 0)
		return -1;
	 /* value */
	return xrow_update_mp_read_str(op, expr, &arg->paste_length,
				       &arg->paste);
}

/* }}} read_arg */

/* {{{ do_op helpers */

static inline int
xrow_update_op_adjust_field_no(struct xrow_update_op *op, int32_t field_max)
{
	if (op->field_no >= 0) {
		if (op->field_no < field_max)
			return 0;
	} else if (op->field_no + field_max >= 0) {
		op->field_no += field_max;
		return 0;
	}
	return xrow_update_err_no_such_field(op);
}

static inline double
xrow_update_arg_arith_to_double(struct xrow_update_arg_arith arg)
{
	if (arg.type == XUPDATE_TYPE_DOUBLE) {
		return arg.dbl;
	} else if (arg.type == XUPDATE_TYPE_FLOAT) {
		return arg.flt;
	} else {
		assert(arg.type == XUPDATE_TYPE_INT);
		if (int96_is_uint64(&arg.int96)) {
			return int96_extract_uint64(&arg.int96);
		} else {
			assert(int96_is_neg_int64(&arg.int96));
			return int96_extract_neg_int64(&arg.int96);
		}
	}
}

static inline decimal_t *
xrow_update_arg_arith_to_decimal(struct xrow_update_arg_arith arg,
				 decimal_t *dec)
{
	decimal_t *ret;
	if (arg.type == XUPDATE_TYPE_DECIMAL) {
		*dec = arg.dec;
		return dec;
	} else if (arg.type == XUPDATE_TYPE_DOUBLE) {
		ret = decimal_from_double(dec, arg.dbl);
	} else if (arg.type == XUPDATE_TYPE_FLOAT) {
		ret = decimal_from_double(dec, arg.flt);
	} else {
		assert(arg.type == XUPDATE_TYPE_INT);
		if (int96_is_uint64(&arg.int96)) {
			uint64_t val = int96_extract_uint64(&arg.int96);
			ret = decimal_from_uint64(dec, val);
		} else {
			assert(int96_is_neg_int64(&arg.int96));
			int64_t val = int96_extract_neg_int64(&arg.int96);
			ret = decimal_from_int64(dec, val);
		}
	}

	return ret;
}

/** Return the MsgPack size of an arithmetic operation result. */
static inline uint32_t
xrow_update_arg_arith_sizeof(struct xrow_update_arg_arith arg)
{
	if (arg.type == XUPDATE_TYPE_INT) {
		if (int96_is_uint64(&arg.int96)) {
			uint64_t val = int96_extract_uint64(&arg.int96);
			return mp_sizeof_uint(val);
		} else {
			int64_t val = int96_extract_neg_int64(&arg.int96);
			return mp_sizeof_int(val);
		}
	} else if (arg.type == XUPDATE_TYPE_DOUBLE) {
		return mp_sizeof_double(arg.dbl);
	} else if (arg.type == XUPDATE_TYPE_FLOAT) {
		return mp_sizeof_float(arg.flt);
	} else {
		assert(arg.type == XUPDATE_TYPE_DECIMAL);
		return mp_sizeof_decimal(&arg.dec);
	}
}

static inline int
xrow_update_arith_make(struct xrow_update_op *op,
		       struct xrow_update_arg_arith arg,
		       struct xrow_update_arg_arith *ret)
{
	struct xrow_update_arg_arith arg1 = arg;
	struct xrow_update_arg_arith arg2 = op->arg.arith;
	enum xrow_update_arith_type lowest_type = arg1.type;
	char opcode = op->opcode;
	if (arg1.type > arg2.type)
		lowest_type = arg2.type;

	if (lowest_type == XUPDATE_TYPE_INT) {
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
		    !int96_is_neg_int64(&arg1.int96))
			return xrow_update_err_int_overflow(op);
		*ret = arg1;
		return 0;
	} else if (lowest_type >= XUPDATE_TYPE_DOUBLE) {
		/* At least one of operands is double or float */
		double a = xrow_update_arg_arith_to_double(arg1);
		double b = xrow_update_arg_arith_to_double(arg2);
		double c;
		switch(opcode) {
		case '+': c = a + b; break;
		case '-': c = a - b; break;
		default:
			unreachable();
			break;
		}
		if (lowest_type == XUPDATE_TYPE_DOUBLE) {
			/* result is DOUBLE */
			ret->type = XUPDATE_TYPE_DOUBLE;
			ret->dbl = c;
		} else {
			/* result is FLOAT */
			assert(lowest_type == XUPDATE_TYPE_FLOAT);
			ret->type = XUPDATE_TYPE_FLOAT;
			ret->flt = (float)c;
		}
	} else {
		/* At least one of the operands is decimal. */
		decimal_t a, b, c;
		if (! xrow_update_arg_arith_to_decimal(arg1, &a) ||
		    ! xrow_update_arg_arith_to_decimal(arg2, &b)) {
			return xrow_update_err_arg_type(op, "a number "\
							"convertible to "\
							"decimal.");
		}
		switch(opcode) {
		case '+':
			if (decimal_add(&c, &a, &b) == NULL)
				return xrow_update_err_decimal_overflow(op);
			break;
		case '-':
			if (decimal_sub(&c, &a, &b) == NULL)
				return xrow_update_err_decimal_overflow(op);
			break;
		default:
			unreachable();
			break;
		}
		ret->type = XUPDATE_TYPE_DECIMAL;
		ret->dec = c;
	}
	return 0;
}

/* }}} do_op helpers */

/* {{{ do_op */

static int
xrow_update_op_do_insert(struct xrow_update *update, struct xrow_update_op *op)
{
	uint32_t size = xrow_update_rope_size(update->rope);
	if (xrow_update_op_adjust_field_no(op, size + 1) != 0)
		return -1;
	struct xrow_update_field *field = (struct xrow_update_field *)
		xrow_update_alloc(&fiber()->gc, sizeof(*field));
	if (field == NULL)
		return -1;
	xrow_update_field_init(field, op->arg.set.value, op->arg.set.length, 0);
	return xrow_update_rope_insert(update->rope, op->field_no, field, 1);
}

static int
xrow_update_op_do_set(struct xrow_update *update, struct xrow_update_op *op)
{
	/* intepret '=' for n +1 field as insert */
	if (op->field_no == (int32_t) xrow_update_rope_size(update->rope))
		return xrow_update_op_do_insert(update, op);

	uint32_t size = xrow_update_rope_size(update->rope);
	if (xrow_update_op_adjust_field_no(op, size) != 0)
		return -1;
	struct xrow_update_field *field =
		xrow_update_rope_extract(update->rope, op->field_no);
	if (field == NULL)
		return -1;
	/* Ignore the previous op, if any. */
	field->op = op;
	op->new_field_len = op->arg.set.length;
	return 0;
}

static int
xrow_update_op_do_delete(struct xrow_update *update, struct xrow_update_op *op)
{
	uint32_t size = xrow_update_rope_size(update->rope);
	if (xrow_update_op_adjust_field_no(op, size) != 0)
		return -1;
	uint32_t delete_count = op->arg.del.count;

	if ((uint64_t) op->field_no + delete_count > size)
		delete_count = size - op->field_no;

	assert(delete_count > 0);
	for (uint32_t u = 0; u < delete_count; u++)
		xrow_update_rope_erase(update->rope, op->field_no);
	return 0;
}

static int
xrow_update_op_do_arith(struct xrow_update *update, struct xrow_update_op *op)
{
	uint32_t size = xrow_update_rope_size(update->rope);
	if (xrow_update_op_adjust_field_no(op, size) != 0)
		return -1;

	struct xrow_update_field *field =
		xrow_update_rope_extract(update->rope, op->field_no);
	if (field == NULL)
		return -1;
	if (field->op != NULL)
		return xrow_update_err_double(op);
	const char *old = field->old;
	struct xrow_update_arg_arith left_arg;
	if (xrow_mp_read_arg_arith(op, &old, &left_arg) != 0)
		return -1;

	if (xrow_update_arith_make(op, left_arg, &op->arg.arith) != 0)
		return -1;
	field->op = op;
	op->new_field_len = xrow_update_arg_arith_sizeof(op->arg.arith);
	return 0;
}

static int
xrow_update_op_do_bit(struct xrow_update *update, struct xrow_update_op *op)
{
	uint32_t size =  xrow_update_rope_size(update->rope);
	if (xrow_update_op_adjust_field_no(op, size) != 0)
		return -1;
	struct xrow_update_field *field =
		xrow_update_rope_extract(update->rope, op->field_no);
	if (field == NULL)
		return -1;
	struct xrow_update_arg_bit *arg = &op->arg.bit;
	if (field->op != NULL)
		return xrow_update_err_double(op);
	const char *old = field->old;
	uint64_t val = 0;
	if (xrow_update_mp_read_uint(op, &old, &val) != 0)
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
xrow_update_op_do_splice(struct xrow_update *update, struct xrow_update_op *op)
{
	uint32_t size = xrow_update_rope_size(update->rope);
	if (xrow_update_op_adjust_field_no(op, size) != 0)
		return -1;
	struct xrow_update_field *field =
		xrow_update_rope_extract(update->rope, op->field_no);
	if (field == NULL)
		return -1;
	if (field->op != NULL)
		return xrow_update_err_double(op);

	struct xrow_update_arg_splice *arg = &op->arg.splice;

	const char *in = field->old;
	int32_t str_len = 0;
	if (xrow_update_mp_read_str(op, &in, (uint32_t *) &str_len, &in) != 0)
		return -1;

	if (arg->offset < 0) {
		if (-arg->offset > str_len + 1)
			return xrow_update_err_splice_bound(op);
		arg->offset = arg->offset + str_len + 1;
	} else if (arg->offset > str_len) {
		arg->offset = str_len;
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
xrow_update_op_store_set(struct xrow_update_arg_set *arg, const char *in,
			 char *out)
{
	(void)in;
	memcpy(out, arg->value, arg->length);
}

static void
xrow_update_op_store_insert(struct xrow_update_arg_set *arg, const char *in,
			    char *out)
{
	(void)in;
	memcpy(out, arg->value, arg->length);
}

static void
xrow_update_op_store_arith(struct xrow_update_arg_arith *arg, const char *in,
			   char *out)
{
	(void)in;
	if (arg->type == XUPDATE_TYPE_INT) {
		if (int96_is_uint64(&arg->int96)) {
			mp_encode_uint(out, int96_extract_uint64(&arg->int96));
		} else {
			assert(int96_is_neg_int64(&arg->int96));
			mp_encode_int(out, int96_extract_neg_int64(&arg->int96));
		}
	} else if (arg->type == XUPDATE_TYPE_DOUBLE) {
		mp_encode_double(out, arg->dbl);
	} else if (arg->type == XUPDATE_TYPE_FLOAT) {
		mp_encode_float(out, arg->flt);
	} else {
		assert (arg->type == XUPDATE_TYPE_DECIMAL);
		mp_encode_decimal(out, &arg->dec);
	}
}

static void
xrow_update_op_store_bit(struct xrow_update_arg_bit *arg, const char *in,
			 char *out)
{
	(void)in;
	mp_encode_uint(out, arg->val);
}

static void
xrow_update_op_store_splice(struct xrow_update_arg_splice *arg, const char *in,
			    char *out)
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

static const struct xrow_update_op_meta op_set = {
	xrow_update_read_arg_set, xrow_update_op_do_set,
	(xrow_update_op_store_f) xrow_update_op_store_set, 3
};
static const struct xrow_update_op_meta op_insert = {
	xrow_update_read_arg_insert, xrow_update_op_do_insert,
	(xrow_update_op_store_f) xrow_update_op_store_insert, 3
};
static const struct xrow_update_op_meta op_arith = {
	xrow_update_read_arg_arith, xrow_update_op_do_arith,
	(xrow_update_op_store_f) xrow_update_op_store_arith, 3
};
static const struct xrow_update_op_meta op_bit = {
	xrow_update_read_arg_bit, xrow_update_op_do_bit,
	(xrow_update_op_store_f) xrow_update_op_store_bit, 3
};
static const struct xrow_update_op_meta op_splice = {
	xrow_update_read_arg_splice, xrow_update_op_do_splice,
	(xrow_update_op_store_f) xrow_update_op_store_splice, 5
};
static const struct xrow_update_op_meta op_delete = {
	xrow_update_read_arg_delete, xrow_update_op_do_delete,
	(xrow_update_op_store_f) NULL, 3
};

/** Split a range of fields in two, allocating update_field
 * context for the new range.
 */
static struct xrow_update_field *
xrow_update_field_split(struct region *region, struct xrow_update_field *prev,
			size_t size, size_t offset)
{
	(void) size;
	struct xrow_update_field *next = (struct xrow_update_field *)
		xrow_update_alloc(region, sizeof(*next));
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

	xrow_update_field_init(next, field, field_len, end - field - field_len);
	return next;
}

/**
 * We found a tuple to do the update on. Prepare a rope
 * to perform operations on.
 * @param tuple_data MessagePack array without the array header.
 * @param tuple_data_end End of the @tuple_data.
 * @param field_count Field count in @tuple_data.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
struct xrow_update_rope *
xrow_update_rope_new_by_tuple(const char *tuple_data,
			      const char *tuple_data_end, uint32_t field_count)
{
	struct region *region = &fiber()->gc;
	struct xrow_update_rope *rope = xrow_update_rope_new(region);
	if (rope == NULL)
		return NULL;
	/* Initialize the rope with the old tuple. */
	struct xrow_update_field *first = (struct xrow_update_field *)
		xrow_update_alloc(region, sizeof(*first));
	if (first == NULL)
		return NULL;
	const char *field = tuple_data;
	const char *end = tuple_data_end;
	if (field == end)
		return rope;

	/* Add first field to rope */
	mp_next(&tuple_data);
	uint32_t field_len = tuple_data - field;
	xrow_update_field_init(first, field, field_len,
			       end - field - field_len);

	return xrow_update_rope_append(rope, first, field_count) != 0 ?
	       NULL : rope;
}

static uint32_t
xrow_update_calc_tuple_length(struct xrow_update *update)
{
	uint32_t res = mp_sizeof_array(xrow_update_rope_size(update->rope));
	struct xrow_update_rope_iter it;
	struct xrow_update_rope_node *node;

	xrow_update_rope_iter_create(&it, update->rope);
	for (node = xrow_update_rope_iter_start(&it); node;
	     node = xrow_update_rope_iter_next(&it)) {
		struct xrow_update_field *field =
			xrow_update_rope_leaf_data(node);
		uint32_t field_len = (field->op ? field->op->new_field_len :
				      (uint32_t)(field->tail - field->old));
		res += field_len + field->tail_len;
	}

	return res;
}

static uint32_t
xrow_update_write_tuple(struct xrow_update *update, char *buffer,
			char *buffer_end)
{
	char *new_data = buffer;
	new_data = mp_encode_array(new_data,
				   xrow_update_rope_size(update->rope));

	(void) buffer_end;

	uint32_t total_field_count = 0;

	struct xrow_update_rope_iter it;
	struct xrow_update_rope_node *node;

	xrow_update_rope_iter_create(&it, update->rope);
	for (node = xrow_update_rope_iter_start(&it); node;
	     node = xrow_update_rope_iter_next(&it)) {
		struct xrow_update_field *field =
			xrow_update_rope_leaf_data(node);
		uint32_t field_count = xrow_update_rope_leaf_size(node);
		const char *old_field = field->old;
		struct xrow_update_op *op = field->op;
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

	assert(xrow_update_rope_size(update->rope) == total_field_count);
	assert(new_data <= buffer_end);
	return new_data - buffer; /* real_tuple_size */
}

static const struct xrow_update_op_meta *
xrow_update_op_by(char opcode)
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
 * Decode an update operation from MessagePack.
 * @param[out] op Update operation.
 * @param index_base Field numbers base: 0 or 1.
 * @param dict Dictionary to lookup field number by a name.
 * @param expr MessagePack.
 *
 * @retval 0 Success.
 * @retval -1 Client error.
 */
static inline int
xrow_update_op_decode(struct xrow_update_op *op, int index_base,
		      struct tuple_dictionary *dict, const char **expr)
{
	if (mp_typeof(**expr) != MP_ARRAY) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS, "update operation "
			 "must be an array {op,..}");
		return -1;
	}
	uint32_t len, args = mp_decode_array(expr);
	if (args < 1) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS, "update operation "\
			 "must be an array {op,..}, got empty array");
		return -1;
	}
	if (mp_typeof(**expr) != MP_STR) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "update operation name must be a string");
		return -1;
	}
	op->opcode = *mp_decode_str(expr, &len);
	op->meta = xrow_update_op_by(op->opcode);
	if (op->meta == NULL)
		return -1;
	if (args != op->meta->args) {
		diag_set(ClientError, ER_UNKNOWN_UPDATE_OP);
		return -1;
	}
	int32_t field_no = 0;
	switch(mp_typeof(**expr)) {
	case MP_INT:
	case MP_UINT: {
		if (xrow_update_mp_read_int32(op, expr, &field_no) != 0)
			return -1;
		if (field_no - index_base >= 0) {
			op->field_no = field_no - index_base;
		} else if (field_no < 0) {
			op->field_no = field_no;
		} else {
			diag_set(ClientError, ER_NO_SUCH_FIELD_NO, field_no);
			return -1;
		}
		break;
	}
	case MP_STR: {
		const char *path = mp_decode_str(expr, &len);
		uint32_t field_no, hash = field_name_hash(path, len);
		if (tuple_fieldno_by_name(dict, path, len, hash,
					  &field_no) != 0) {
			diag_set(ClientError, ER_NO_SUCH_FIELD_NAME,
				 tt_cstr(path, len));
			return -1;
		}
		op->field_no = (int32_t) field_no;
		break;
	}
	default:
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "field id must be a number or a string");
		return -1;
	}
	return op->meta->read_arg(index_base, op, expr);
}

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
	for (; op < ops_end; op++) {
		if (xrow_update_op_decode(op, update->index_base, dict,
					  &expr) != 0)
			return -1;
		/*
		 * Continue collecting the changed columns
		 * only if there are unset bits in the mask.
		 */
		if (column_mask != COLUMN_MASK_FULL) {
			int32_t field_no;
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
xrow_update_do_ops(struct xrow_update *update, const char *old_data,
		   const char *old_data_end, uint32_t part_count)
{
	update->rope = xrow_update_rope_new_by_tuple(old_data, old_data_end,
						     part_count);
	if (update->rope == NULL)
		return -1;
	struct xrow_update_op *op = update->ops;
	struct xrow_update_op *ops_end = op + update->op_count;
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
xrow_upsert_do_ops(struct xrow_update *update, const char *old_data,
		   const char *old_data_end, uint32_t part_count,
		   bool suppress_error)
{
	update->rope = xrow_update_rope_new_by_tuple(old_data, old_data_end,
						     part_count);
	if (update->rope == NULL)
		return -1;
	struct xrow_update_op *op = update->ops;
	struct xrow_update_op *ops_end = op + update->op_count;
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
xrow_update_init(struct xrow_update *update, int index_base)
{
	memset(update, 0, sizeof(*update));
	/*
	 * Base field offset, e.g. 0 for C and 1 for Lua. Used only for
	 * error messages. All fields numbers must be zero-based!
	 */
	update->index_base = index_base;
}

static const char *
xrow_update_finish(struct xrow_update *update, uint32_t *p_tuple_len)
{
	uint32_t tuple_len = xrow_update_calc_tuple_length(update);
	char *buffer = (char *) region_alloc(&fiber()->gc, tuple_len);
	if (buffer == NULL) {
		diag_set(OutOfMemory, tuple_len, "region_alloc", "buffer");
		return NULL;
	}
	*p_tuple_len = xrow_update_write_tuple(update, buffer,
					       buffer + tuple_len);
	return buffer;
}

int
tuple_update_check_ops(const char *expr, const char *expr_end,
		       struct tuple_dictionary *dict, int index_base)
{
	struct xrow_update update;
	xrow_update_init(&update, index_base);
	return xrow_update_read_ops(&update, expr, expr_end, dict, 0);
}

const char *
tuple_update_execute(const char *expr,const char *expr_end,
		     const char *old_data, const char *old_data_end,
		     struct tuple_dictionary *dict, uint32_t *p_tuple_len,
		     int index_base, uint64_t *column_mask)
{
	struct xrow_update update;
	xrow_update_init(&update, index_base);
	uint32_t field_count = mp_decode_array(&old_data);

	if (xrow_update_read_ops(&update, expr, expr_end, dict,
				 field_count) != 0)
		return NULL;
	if (xrow_update_do_ops(&update, old_data, old_data_end, field_count))
		return NULL;
	if (column_mask)
		*column_mask = update.column_mask;

	return xrow_update_finish(&update, p_tuple_len);
}

const char *
tuple_upsert_execute(const char *expr,const char *expr_end,
		     const char *old_data, const char *old_data_end,
		     struct tuple_dictionary *dict, uint32_t *p_tuple_len,
		     int index_base, bool suppress_error, uint64_t *column_mask)
{
	struct xrow_update update;
	xrow_update_init(&update, index_base);
	uint32_t field_count = mp_decode_array(&old_data);

	if (xrow_update_read_ops(&update, expr, expr_end, dict,
				 field_count) != 0)
		return NULL;
	if (xrow_upsert_do_ops(&update, old_data, old_data_end, field_count,
			       suppress_error))
		return NULL;
	if (column_mask)
		*column_mask = update.column_mask;

	return xrow_update_finish(&update, p_tuple_len);
}

const char *
tuple_upsert_squash(const char *expr1, const char *expr1_end,
		    const char *expr2, const char *expr2_end,
		    struct tuple_dictionary *dict, size_t *result_size,
		    int index_base)
{
	const char *expr[2] = {expr1, expr2};
	const char *expr_end[2] = {expr1_end, expr2_end};
	struct xrow_update update[2];
	for (int j = 0; j < 2; j++) {
		xrow_update_init(&update[j], index_base);
		if (xrow_update_read_ops(&update[j], expr[j], expr_end[j],
					 dict, 0) != 0)
			return NULL;
		mp_decode_array(&expr[j]);
		int32_t prev_field_no = index_base - 1;
		for (uint32_t i = 0; i < update[j].op_count; i++) {
			struct xrow_update_op *op = &update[j].ops[i];
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
	char *buf = (char *) region_alloc(&fiber()->gc,
					  possible_size + space_for_arr_tag);
	if (buf == NULL) {
		diag_set(OutOfMemory, possible_size + space_for_arr_tag,
			 "region_alloc", "buf");
		return NULL;
	}
	/* reserve some space for mp array header */
	char *res_ops = buf + space_for_arr_tag;
	uint32_t res_count = 0; /* number of resulting operations */

	uint32_t op_count[2] = {update[0].op_count, update[1].op_count};
	uint32_t op_no[2] = {0, 0};
	while (op_no[0] < op_count[0] || op_no[1] < op_count[1]) {
		res_count++;
		struct xrow_update_op *op[2] = {update[0].ops + op_no[0],
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
		struct xrow_update_arg_arith res;
		if (xrow_update_arith_make(op[1], op[0]->arg.arith, &res) != 0)
			return NULL;
		res_ops = mp_encode_array(res_ops, 3);
		res_ops = mp_encode_str(res_ops,
					(const char *)&op[0]->opcode, 1);
		res_ops = mp_encode_uint(res_ops,
					 op[0]->field_no +
						 update[0].index_base);
		xrow_update_op_store_arith(&res, NULL, res_ops);
		res_ops += xrow_update_arg_arith_sizeof(res);
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
