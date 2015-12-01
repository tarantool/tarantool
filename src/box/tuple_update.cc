/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include "third_party/queue.h"

#include "salad/rope.h"
#include "error.h"
#include "msgpuck/msgpuck.h"
#include "bit/int96.h"

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

typedef void (*do_op_func)(struct tuple_update *update, struct update_op *op);
typedef void (*read_arg_func)(struct tuple_update *update, struct update_op *op,
			      const char **expr);
typedef void (*store_op_func)(union update_op_arg *arg, const char *in, char *out);

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
static inline int64_t
mp_read_int(struct tuple_update *update, struct update_op *op,
	    const char **expr)
{
	int64_t field_no;
	if (mp_typeof(**expr) == MP_UINT)
		field_no = mp_decode_uint(expr);
	else if (mp_typeof(**expr) == MP_INT)
		field_no = mp_decode_int(expr);
	else
		tnt_raise(ClientError, ER_ARG_TYPE, (char ) op->opcode,
			  update->index_base + op->field_no, "UINT");
	return field_no;
}

static inline uint64_t
mp_read_uint(struct tuple_update *update, struct update_op *op,
	     const char **expr)
{
	int64_t field_no;
	if (mp_typeof(**expr) == MP_UINT)
		field_no = mp_decode_uint(expr);
	else
		tnt_raise(ClientError, ER_ARG_TYPE, (char ) op->opcode,
			  update->index_base + op->field_no, "UINT");
	return field_no;
}

/**
 * Load an argument of an arithmetic operation either from tuple
 * or from the UPDATE command.
 */
static inline struct op_arith_arg
mp_read_arith_arg(struct tuple_update *update, struct update_op *op,
		  const char **expr)
{
	struct op_arith_arg result;
	if (mp_typeof(**expr) == MP_UINT) {
		result.type = AT_INT;
		int96_set_unsigned(&result.int96, mp_decode_uint(expr));
	} else if (mp_typeof(**expr) == MP_INT) {
		result.type = AT_INT;
		int96_set_signed(&result.int96, mp_decode_int(expr));
	} else if (mp_typeof(**expr) == MP_DOUBLE) {
		result.type = AT_DOUBLE;
		result.dbl = mp_decode_double(expr);
	} else if (mp_typeof(**expr) == MP_FLOAT) {
		result.type = AT_FLOAT;
		result.flt = mp_decode_float(expr);
	} else {
		tnt_raise(ClientError, ER_ARG_TYPE, (char ) op->opcode,
			  update->index_base + op->field_no, "NUMBER");
	}
	return result;
}

static inline const char *
mp_read_str(struct tuple_update *update, struct update_op *op,
	    const char **expr, uint32_t *len)
{
	if (mp_typeof(**expr) != MP_STR) {
		tnt_raise(ClientError, ER_ARG_TYPE, (char) op->opcode,
			  update->index_base + op->field_no, "STR");
	}
	return mp_decode_str(expr, len); /* value */
}

/* }}} read_arg helpers */

/* {{{ read_arg */

static void
read_arg_set(struct tuple_update *update, struct update_op *op,
	     const char **expr)
{
	(void)update;
	op->arg.set.value = *expr;
	mp_next(expr);
	op->arg.set.length = (uint32_t) (*expr - op->arg.set.value);
}

static void
read_arg_insert(struct tuple_update *update, struct update_op *op,
		const char **expr)
{
	read_arg_set(update, op, expr);
}

static void
read_arg_delete(struct tuple_update *update, struct update_op *op,
		const char **expr)
{
	op->arg.del.count = (uint32_t) mp_read_int(update, op, expr);
}

static void
read_arg_arith(struct tuple_update *update, struct update_op *op,
	       const char **expr)
{
	op->arg.arith = mp_read_arith_arg(update, op, expr);
}

static void
read_arg_bit(struct tuple_update *update, struct update_op *op,
	     const char **expr)
{
	struct op_bit_arg *arg = &op->arg.bit;
	arg->val = mp_read_uint(update, op, expr);
}

static void
read_arg_splice(struct tuple_update *update, struct update_op *op,
		const char **expr)
{
	struct op_splice_arg *arg = &op->arg.splice;
	arg->offset = mp_read_int(update, op, expr);
	arg->cut_length = mp_read_int(update, op, expr); /* cut length */
	arg->paste = mp_read_str(update, op, expr, &arg->paste_length); /* value */
}

/* }}} read_arg */

/* {{{ do_op helpers */

static inline void
op_adjust_field_no(struct tuple_update *update, struct update_op *op,
		   int32_t field_max)
{
	if (op->field_no >= 0) {
		if (op->field_no < field_max)
			return;
		tnt_raise(ClientError, ER_NO_SUCH_FIELD, update->index_base +
			op->field_no);
	} else {
		if (op->field_no + field_max >= 0) {
			op->field_no += field_max;
			return;
		}
		tnt_raise(ClientError, ER_NO_SUCH_FIELD, op->field_no);
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

static inline struct op_arith_arg
make_arith_operation(struct op_arith_arg arg1, struct op_arith_arg arg2,
		     char opcode, uint32_t err_fieldno)
{
	struct op_arith_arg result;

	arith_type lowest_type = arg1.type;
	if (arg1.type > arg2.type)
		lowest_type = arg2.type;

	if (lowest_type == AT_INT) {
		result.type = AT_INT;
		switch(opcode) {
		case '+':
			int96_add(&arg1.int96, &arg2.int96);
			break;
		case '-':
			int96_invert(&arg2.int96);
			int96_add(&arg1.int96, &arg2.int96);
			break;
		default:
			assert(false); /* checked by update_read_ops */
			break;
		}
		if (!int96_is_uint64(&arg1.int96) &&
		    !int96_is_neg_int64(&arg1.int96)) {
			tnt_raise(ClientError,
				  ER_UPDATE_INTEGER_OVERFLOW,
				  opcode, err_fieldno);
		}
		return arg1;
	} else {
		/* At least one of operands is double or float */
		double a = cast_arith_arg_to_double(arg1);
		double b = cast_arith_arg_to_double(arg2);
		double c;
		switch(opcode) {
		case '+': c = a + b; break;
		case '-': c = a - b; break;
		default:
			tnt_raise(ClientError, ER_ARG_TYPE, (char ) opcode,
				  err_fieldno, "positive integer");
			break;
		}
		if (lowest_type == AT_DOUBLE) {
			/* result is DOUBLE */
			result.type = AT_DOUBLE;
			result.dbl = c;
		} else {
			/* result is FLOAT */
			assert(lowest_type == AT_FLOAT);
			result.type = AT_FLOAT;
			result.flt = (float)c;
		}
	}
	return result;
}

/* }}} do_op helpers */

/* {{{ do_op */

static void
do_op_insert(struct tuple_update *update, struct update_op *op)
{
	op_adjust_field_no(update, op, rope_size(update->rope) + 1);
	struct update_field *field = (struct update_field *)
		update->alloc(update->alloc_ctx, sizeof(*field));
	update_field_init(field, op->arg.set.value, op->arg.set.length, 0);
	rope_insert(update->rope, op->field_no, field, 1);
}

static void
do_op_set(struct tuple_update *update, struct update_op *op)
{
	/* intepret '=' for n +1 field as insert */
	if (op->field_no == rope_size(update->rope))
		return do_op_insert(update, op);
	op_adjust_field_no(update, op, rope_size(update->rope));
	struct update_field *field = (struct update_field *)
		rope_extract(update->rope, op->field_no);
	/* Ignore the previous op, if any. */
	field->op = op;
	op->new_field_len = op->arg.set.length;
}

static void
do_op_delete(struct tuple_update *update, struct update_op *op)
{
	op_adjust_field_no(update, op, rope_size(update->rope));
	uint32_t delete_count = op->arg.del.count;

	if ((uint64_t) op->field_no + delete_count > rope_size(update->rope))
		delete_count = rope_size(update->rope) - op->field_no;

	if (delete_count == 0) {
		tnt_raise(ClientError, ER_UPDATE_FIELD,
			  update->index_base + op->field_no,
			  "cannot delete 0 fields");
	}

	for (uint32_t u = 0; u < delete_count; u++)
		rope_erase(update->rope, op->field_no);
}

static void
do_op_arith(struct tuple_update *update, struct update_op *op)
{
	op_adjust_field_no(update, op, rope_size(update->rope));

	struct update_field *field = (struct update_field *)
		rope_extract(update->rope, op->field_no);
	if (field->op) {
		tnt_raise(ClientError, ER_UPDATE_FIELD,
			  update->index_base + op->field_no,
			  "double update of the same field");
	}
	const char *old = field->old;
	struct op_arith_arg left_arg = mp_read_arith_arg(update, op, &old);

	struct op_arith_arg right_arg = op->arg.arith;
	op->arg.arith = make_arith_operation(left_arg, right_arg, op->opcode,
					     update->index_base + op->field_no);
	field->op = op;
	op->new_field_len = mp_sizeof_op_arith_arg(op->arg.arith);
}

static void
do_op_bit(struct tuple_update *update, struct update_op *op)
{
	op_adjust_field_no(update, op, rope_size(update->rope));
	struct update_field *field = (struct update_field *)
		rope_extract(update->rope, op->field_no);

	struct op_bit_arg *arg = &op->arg.bit;
	if (field->op) {
		tnt_raise(ClientError, ER_UPDATE_FIELD,
			  update->index_base + op->field_no,
			  "double update of the same field");
	}
	const char *old = field->old;
	uint64_t val = mp_read_uint(update, op, &old);
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
		assert(false); /* checked by update_read_ops */
	}
	field->op = op;
	op->new_field_len = mp_sizeof_uint(arg->val);
}

static void
do_op_splice(struct tuple_update *update, struct update_op *op)
{
	op_adjust_field_no(update, op, rope_size(update->rope));
	struct update_field *field = (struct update_field *)
		rope_extract(update->rope, op->field_no);
	if (field->op) {
		tnt_raise(ClientError, ER_UPDATE_FIELD,
			  update->index_base + op->field_no,
			  "double update of the same field");
	}

	struct op_splice_arg *arg = &op->arg.splice;

	const char *in = field->old;
	uint32_t str_len;
	in = mp_read_str(update, op, &in, &str_len);

	if (arg->offset < 0) {
		if (-arg->offset > str_len + 1) {
			tnt_raise(ClientError, ER_SPLICE,
				  update->index_base + op->field_no,
				  "offset is out of bound");
		}
		arg->offset = arg->offset + str_len + 1;
	} else if (arg->offset - update->index_base >= 0) {
		arg->offset -= update->index_base;
		if (arg->offset > str_len)
			arg->offset = str_len;
	} else /* (offset <= 0) */ {
		tnt_raise(ClientError, ER_SPLICE,
			  update->index_base + op->field_no,
			  "offset is out of bound");
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
 */
void
update_create_rope(struct tuple_update *update,
		   const char *tuple_data, const char *tuple_data_end)
{
	uint32_t field_count = mp_decode_array(&tuple_data);

	update->rope = rope_new(update_field_split, update, update->alloc,
				region_alloc_free_stub, update->alloc_ctx);

	/* Initialize the rope with the old tuple. */

	struct update_field *first = (struct update_field *)
			update->alloc(update->alloc_ctx, sizeof(*first));
	const char *field = tuple_data;
	const char *end = tuple_data_end;

	/* Add first field to rope */
	mp_next(&tuple_data);
	uint32_t field_len = tuple_data - field;
	update_field_init(first, field, field_len,
			  end - field - field_len);

	rope_append(update->rope, first, field_count);
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
				      field->tail - field->old);
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

static void
update_read_ops(struct tuple_update *update, const char *expr,
		const char *expr_end)
{
	if (mp_typeof(*expr) != MP_ARRAY)
		tnt_raise(IllegalParams,
			  "update operations must be an "
			  "array {{op,..}, {op,..}}");

	/* number of operations */
	update->op_count = mp_decode_array(&expr);

	if (update->op_count > BOX_UPDATE_OP_CNT_MAX)
		tnt_raise(IllegalParams, "too many operations for update");
	if (update->op_count == 0)
		tnt_raise(IllegalParams, "no operations for update");
	if (mp_typeof(*expr) != MP_ARRAY)
		tnt_raise(IllegalParams,
			  "update operations"
			  " must be an array {{op,..}, {op,..}}");

	/* Read update operations.  */
	update->ops = (struct update_op *) update->alloc(update->alloc_ctx,
				update->op_count * sizeof(struct update_op));
	struct update_op *op = update->ops;
	struct update_op *ops_end = op + update->op_count;
	for (; op < ops_end; op++) {
		/* Read operation */
		uint32_t args, len;
		args = mp_decode_array(&expr);
		if (args < 1)
			tnt_raise(IllegalParams,
				  "update operation must be an "
				  "array {op,..}, got empty array");
		if (mp_typeof(*expr) != MP_STR)
			tnt_raise(IllegalParams,
				  "update operation name must be a string");

		op->opcode = *mp_decode_str(&expr, &len);

		switch (op->opcode) {
		case '=':
			op->meta = &op_set;
			break;
		case '+':
		case '-':
			op->meta = &op_arith;
			break;
		case '&':
		case '|':
		case '^':
			op->meta = &op_bit;
			break;
		case ':':
			op->meta = &op_splice;
			break;
		case '#':
			op->meta = &op_delete;
			break;
		case '!':
			op->meta = &op_insert;
			break;
		default:
			tnt_raise(ClientError, ER_UNKNOWN_UPDATE_OP);
		}
		if (args != op->meta->args)
			tnt_raise(ClientError, ER_UNKNOWN_UPDATE_OP);
		if (mp_typeof(*expr) != MP_INT && mp_typeof(*expr) != MP_UINT)
			tnt_raise(IllegalParams, "field id must be a number");
		int32_t field_no = mp_read_int(update, op, &expr);
		if (field_no - update->index_base >= 0) {
			op->field_no = field_no - update->index_base;
		} else if (field_no < 0) {
			op->field_no = field_no;
		} else {
			tnt_raise(ClientError, ER_NO_SUCH_FIELD, field_no);
		}
		op->meta->read_arg(update, op, &expr);
	}

	/* Check the remainder length, the request must be fully read. */
	if (expr != expr_end)
		tnt_raise(IllegalParams, "can't unpack update operations");
}

static void
update_do_ops(struct tuple_update *update)
{
	struct update_op *op = update->ops;
	struct update_op *ops_end = op + update->op_count;
	for (; op < ops_end; op++) {
		op->meta->do_op(update, op);
	}
}

static void
upsert_do_ops(struct tuple_update *update)
{
	struct update_op *op = update->ops;
	struct update_op *ops_end = op + update->op_count;
	for (; op < ops_end; op++) {
		try {
			op->meta->do_op(update, op);
		} catch (ClientError *e) {
			say_error("UPSERT operation failed:");
			e->log();
		}
	}
}

static void
update_init(struct tuple_update *update,
	    tuple_update_alloc_func alloc, void *alloc_ctx,
	    const char *old_data, const char *old_data_end,
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

	update_create_rope(update, old_data, old_data_end);
}

const char *
update_finish(struct tuple_update *update, uint32_t *p_tuple_len)
{
	uint32_t tuple_len = update_calc_tuple_length(update);
	char *buffer = (char *) update->alloc(update->alloc_ctx, tuple_len);
	*p_tuple_len = update_write_tuple(update, buffer, buffer + tuple_len);
	return buffer;
}

const char *
tuple_update_execute(tuple_update_alloc_func alloc, void *alloc_ctx,
		     const char *expr,const char *expr_end,
		     const char *old_data, const char *old_data_end,
		     uint32_t *p_tuple_len, int index_base)
{
	struct tuple_update update;
	update_init(&update, alloc, alloc_ctx, old_data, old_data_end,
		    index_base);

	update_read_ops(&update, expr, expr_end);
	update_do_ops(&update);

	return update_finish(&update, p_tuple_len);
}

const char *
tuple_upsert_execute(tuple_update_alloc_func alloc, void *alloc_ctx,
		     const char *expr,const char *expr_end,
		     const char *old_data, const char *old_data_end,
		     uint32_t *p_tuple_len, int index_base)
{
	struct tuple_update update;
	update_init(&update, alloc, alloc_ctx, old_data, old_data_end,
		    index_base);

	update_read_ops(&update, expr, expr_end);
	upsert_do_ops(&update);

	return update_finish(&update, p_tuple_len);
}

