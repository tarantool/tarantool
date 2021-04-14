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
#include "tuple_format.h"
#include "mp_extension_types.h"

#include <float.h>

/* {{{ Error helpers. */

/** Take a string identifier of a field being updated by @a op. */
static inline const char *
xrow_update_op_field_str(const struct xrow_update_op *op)
{
	if (op->lexer.src != NULL)
		return tt_sprintf("'%.*s'", op->lexer.src_len, op->lexer.src);
	else if (op->field_no >= 0)
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

int
xrow_update_err_no_such_field(const struct xrow_update_op *op)
{
	if (op->lexer.src == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FIELD_NO, op->field_no +
			 (op->field_no >= 0 ? TUPLE_INDEX_BASE : 0));
		return -1;
	}
	diag_set(ClientError, ER_NO_SUCH_FIELD_NAME,
		 xrow_update_op_field_str(op));
	return -1;
}

int
xrow_update_err(const struct xrow_update_op *op, const char *reason)
{
	diag_set(ClientError, ER_UPDATE_FIELD, xrow_update_op_field_str(op),
		 reason);
	return -1;
}

/* }}} Error helpers. */

uint32_t
xrow_update_field_sizeof(struct xrow_update_field *field)
{
	switch (field->type) {
	case XUPDATE_NOP:
		return field->size;
	case XUPDATE_SCALAR:
		return field->scalar.op->new_field_len;
	case XUPDATE_ARRAY:
		return xrow_update_array_sizeof(field);
	case XUPDATE_BAR:
		return xrow_update_bar_sizeof(field);
	case XUPDATE_ROUTE:
		return xrow_update_route_sizeof(field);
	case XUPDATE_MAP:
		return xrow_update_map_sizeof(field);
	default:
		unreachable();
	}
	return 0;
}

uint32_t
xrow_update_field_store(struct xrow_update_field *field,
			struct json_tree *format_tree,
			struct json_token *this_node, char *out, char *out_end)
{
	struct xrow_update_op *op;
	switch(field->type) {
	case XUPDATE_NOP:
		assert(out_end - out >= field->size);
		memcpy(out, field->data, field->size);
		return field->size;
	case XUPDATE_SCALAR:
		op = field->scalar.op;
		assert(out_end - out >= op->new_field_len);
		return op->meta->store(op, format_tree, this_node, field->data,
				       out);
	case XUPDATE_ARRAY:
		return xrow_update_array_store(field, format_tree, this_node,
					       out, out_end);
	case XUPDATE_BAR:
		return xrow_update_bar_store(field, format_tree, this_node, out,
					     out_end);
	case XUPDATE_ROUTE:
		return xrow_update_route_store(field, format_tree, this_node,
					       out, out_end);
	case XUPDATE_MAP:
		return xrow_update_map_store(field, format_tree, this_node, out,
					     out_end);
	default:
		unreachable();
	}
	return 0;
}

/* {{{ read_arg helpers. */

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

int
xrow_mp_read_arg_arith(struct xrow_update_op *op, const char **expr,
		       struct xrow_update_arg_arith *ret)
{
	int8_t ext_type;
	uint32_t len;
	switch(mp_typeof(**expr)) {
	case MP_UINT:
		ret->type = XUPDATE_TYPE_INT;
		int96_set_unsigned(&ret->int96, mp_decode_uint(expr));
		return 0;
	case MP_INT:
		ret->type = XUPDATE_TYPE_INT;
		int96_set_signed(&ret->int96, mp_decode_int(expr));
		return 0;
	case MP_DOUBLE:
		ret->type = XUPDATE_TYPE_DOUBLE;
		ret->dbl = mp_decode_double(expr);
		return 0;
	case MP_FLOAT:
		ret->type = XUPDATE_TYPE_FLOAT;
		ret->flt = mp_decode_float(expr);
		return 0;
	case MP_EXT:
		len = mp_decode_extl(expr, &ext_type);
		if (ext_type == MP_DECIMAL) {
			ret->type = XUPDATE_TYPE_DECIMAL;
			decimal_unpack(expr, len, &ret->dec);
			return 0;
		}
		FALLTHROUGH;
	default:
		return xrow_update_err_arg_type(op, "a number");
	}
}

static inline int
xrow_update_mp_read_str(struct xrow_update_op *op, const char **expr,
			uint32_t *len, const char **ret)
{
	if (mp_typeof(**expr) == MP_STR) {
		*ret = mp_decode_str(expr, len);
		return 0;
	}
	return xrow_update_err_arg_type(op, "a string");
}

/* }}} read_arg helpers. */

/* {{{ read_arg */

static int
xrow_update_read_arg_set(struct xrow_update_op *op, const char **expr,
			 int index_base)
{
	(void) index_base;
	op->arg.set.value = *expr;
	mp_next(expr);
	op->arg.set.length = (uint32_t) (*expr - op->arg.set.value);
	return 0;
}

static int
xrow_update_read_arg_delete(struct xrow_update_op *op, const char **expr,
			    int index_base)
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
xrow_update_read_arg_arith(struct xrow_update_op *op, const char **expr,
			   int index_base)
{
	(void) index_base;
	return xrow_mp_read_arg_arith(op, expr, &op->arg.arith);
}

static int
xrow_update_read_arg_bit(struct xrow_update_op *op, const char **expr,
			 int index_base)
{
	(void) index_base;
	return xrow_update_mp_read_uint(op, expr, &op->arg.bit.val);
}

static int
xrow_update_read_arg_splice(struct xrow_update_op *op, const char **expr,
			    int index_base)
{
	struct xrow_update_arg_splice *arg = &op->arg.splice;
	if (xrow_update_mp_read_int32(op, expr, &arg->offset))
		return -1;
	if (arg->offset >= 0) {
		if (arg->offset - index_base < 0)
			return xrow_update_err_splice_bound(op);
		arg->offset -= index_base;
	}
	if (xrow_update_mp_read_int32(op, expr, &arg->cut_length) != 0)
		return -1;
	return xrow_update_mp_read_str(op, expr, &arg->paste_length,
				       &arg->paste);
}

/* }}} read_arg */

/* {{{ do_op helpers. */

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
	if (arg.type == XUPDATE_TYPE_DECIMAL) {
		*dec = arg.dec;
		return dec;
	} else if (arg.type == XUPDATE_TYPE_DOUBLE) {
		return decimal_from_double(dec, arg.dbl);
	} else if (arg.type == XUPDATE_TYPE_FLOAT) {
		return decimal_from_double(dec, arg.flt);
	} else {
		assert(arg.type == XUPDATE_TYPE_INT);
		if (int96_is_uint64(&arg.int96)) {
			uint64_t val = int96_extract_uint64(&arg.int96);
			return decimal_from_uint64(dec, val);
		} else {
			assert(int96_is_neg_int64(&arg.int96));
			int64_t val = int96_extract_neg_int64(&arg.int96);
			return decimal_from_int64(dec, val);
		}
	}
}

uint32_t
xrow_update_arg_arith_sizeof(const struct xrow_update_arg_arith *arg)
{
	switch (arg->type) {
	case XUPDATE_TYPE_INT:
		if (int96_is_uint64(&arg->int96)) {
			uint64_t val = int96_extract_uint64(&arg->int96);
			return mp_sizeof_uint(val);
		} else {
			int64_t val = int96_extract_neg_int64(&arg->int96);
			return mp_sizeof_int(val);
		}
		break;
	case XUPDATE_TYPE_DOUBLE:
	case XUPDATE_TYPE_FLOAT:
		return mp_sizeof_double(arg->dbl);
	default:
		assert(arg->type == XUPDATE_TYPE_DECIMAL);
		return mp_sizeof_decimal(&arg->dec);
	}
}

int
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
			unreachable();
			break;
		}
		if (!int96_is_uint64(&arg1.int96) &&
		    !int96_is_neg_int64(&arg1.int96))
			return xrow_update_err_int_overflow(op);
		*ret = arg1;
	} else if (lowest_type >= XUPDATE_TYPE_DOUBLE) {
		double a = xrow_update_arg_arith_to_double(arg1);
		double b = xrow_update_arg_arith_to_double(arg2);
		double c;
		switch(opcode) {
		case '+':
			c = a + b;
			break;
		case '-':
			c = a - b;
			break;
		default:
			unreachable();
			break;
		}
		if (c <= FLT_MAX && c >= -FLT_MAX) {
			float fc = (float)c;
			if (c == (double)fc) {
				ret->type = XUPDATE_TYPE_FLOAT;
				ret->flt = fc;
				return 0;
			}
		}
		ret->type = XUPDATE_TYPE_DOUBLE;
		ret->dbl = c;
		return 0;
	} else {
		decimal_t a, b, c;
		if (! xrow_update_arg_arith_to_decimal(arg1, &a) ||
		    ! xrow_update_arg_arith_to_decimal(arg2, &b)) {
			return xrow_update_err_arg_type(op, "a number "\
							"convertible to "\
							"decimal");
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

int
xrow_update_op_do_arith(struct xrow_update_op *op, const char *old)
{
	struct xrow_update_arg_arith left_arg;
	if (xrow_mp_read_arg_arith(op, &old, &left_arg) != 0 ||
	    xrow_update_arith_make(op, left_arg, &op->arg.arith) != 0)
		return -1;
	op->new_field_len = xrow_update_arg_arith_sizeof(&op->arg.arith);
	return 0;
}

int
xrow_update_op_do_bit(struct xrow_update_op *op, const char *old)
{
	uint64_t val = 0;
	if (xrow_update_mp_read_uint(op, &old, &val) != 0)
		return -1;
	struct xrow_update_arg_bit *arg = &op->arg.bit;
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
		unreachable();
	}
	op->new_field_len = mp_sizeof_uint(arg->val);
	return 0;
}

int
xrow_update_op_do_splice(struct xrow_update_op *op, const char *old)
{
	struct xrow_update_arg_splice *arg = &op->arg.splice;
	int32_t str_len = 0;
	if (xrow_update_mp_read_str(op, &old, (uint32_t *) &str_len, &old) != 0)
		return -1;

	if (arg->offset < 0) {
		if (-arg->offset > str_len + 1)
			return xrow_update_err_splice_bound(op);
		arg->offset += str_len + 1;
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

	arg->tail_offset = arg->offset + arg->cut_length;
	arg->tail_length = str_len - arg->tail_offset;
	op->new_field_len = mp_sizeof_str(arg->offset + arg->paste_length +
					  arg->tail_length);
	return 0;
}

/* }}} do_op helpers. */

/* {{{ store_op */

static uint32_t
xrow_update_op_store_set(struct xrow_update_op *op,
			 struct json_tree *format_tree,
			 struct json_token *this_node, const char *in,
			 char *out)
{
	(void) format_tree;
	(void) this_node;
	(void) in;
	memcpy(out, op->arg.set.value, op->arg.set.length);
	return op->arg.set.length;
}

uint32_t
xrow_update_op_store_arith(struct xrow_update_op *op,
			   struct json_tree *format_tree,
			   struct json_token *this_node, const char *in,
			   char *out)
{
	(void) format_tree;
	(void) in;
	char *begin = out;
	struct xrow_update_arg_arith *arg = &op->arg.arith;
	switch (arg->type) {
	case XUPDATE_TYPE_INT:
		if (int96_is_uint64(&arg->int96)) {
			out = mp_encode_uint(
				out, int96_extract_uint64(&arg->int96));
		} else {
			assert(int96_is_neg_int64(&arg->int96));
			out = mp_encode_int(
				out, int96_extract_neg_int64( &arg->int96));
		}
		break;
	case XUPDATE_TYPE_DOUBLE:
		out = mp_encode_double(out, arg->dbl);
		break;
	case XUPDATE_TYPE_FLOAT:
		if (this_node != NULL) {
			enum field_type type =
				json_tree_entry(this_node, struct tuple_field,
						token)->type;
			if (type == FIELD_TYPE_DOUBLE) {
				out = mp_encode_double(out, arg->flt);
				break;
			}
		}
		out = mp_encode_float(out, arg->flt);
		break;
	default:
		assert(arg->type == XUPDATE_TYPE_DECIMAL);
		out = mp_encode_decimal(out, &arg->dec);
		break;
	}
	return out - begin;
}

static uint32_t
xrow_update_op_store_bit(struct xrow_update_op *op,
			 struct json_tree *format_tree,
			 struct json_token *this_node, const char *in,
			 char *out)
{
	(void) format_tree;
	(void) this_node;
	(void) in;
	char *end = mp_encode_uint(out, op->arg.bit.val);
	return end - out;
}

static uint32_t
xrow_update_op_store_splice(struct xrow_update_op *op,
			    struct json_tree *format_tree,
			    struct json_token *this_node, const char *in,
			    char *out)
{
	(void) format_tree;
	(void) this_node;
	struct xrow_update_arg_splice *arg = &op->arg.splice;
	uint32_t new_str_len = arg->offset + arg->paste_length +
			       arg->tail_length;
	char *begin = out;
	(void) mp_decode_strl(&in);
	out = mp_encode_strl(out, new_str_len);
	/* Copy field head. */
	memcpy(out, in, arg->offset);
	out = out + arg->offset;
	/* Copy the paste. */
	memcpy(out, arg->paste, arg->paste_length);
	out = out + arg->paste_length;
	/* Copy tail. */
	memcpy(out, in + arg->tail_offset, arg->tail_length);
	out = out + arg->tail_length;
	return out - begin;
}

/* }}} store_op */

static const struct xrow_update_op_meta op_set = {
	xrow_update_read_arg_set, xrow_update_op_do_field_set,
	(xrow_update_op_store_f) xrow_update_op_store_set, 3
};
static const struct xrow_update_op_meta op_insert = {
	xrow_update_read_arg_set, xrow_update_op_do_field_insert,
	(xrow_update_op_store_f) xrow_update_op_store_set, 3
};
static const struct xrow_update_op_meta op_arith = {
	xrow_update_read_arg_arith, xrow_update_op_do_field_arith,
	(xrow_update_op_store_f) xrow_update_op_store_arith, 3
};
static const struct xrow_update_op_meta op_bit = {
	xrow_update_read_arg_bit, xrow_update_op_do_field_bit,
	(xrow_update_op_store_f) xrow_update_op_store_bit, 3
};
static const struct xrow_update_op_meta op_splice = {
	xrow_update_read_arg_splice, xrow_update_op_do_field_splice,
	(xrow_update_op_store_f) xrow_update_op_store_splice, 5
};
static const struct xrow_update_op_meta op_delete = {
	xrow_update_read_arg_delete, xrow_update_op_do_field_delete,
	(xrow_update_op_store_f) NULL, 3
};

static inline const struct xrow_update_op_meta *
xrow_update_op_by(const char *opcode, uint32_t len, int op_num)
{
	if (len != 1)
		goto error;
	switch (*opcode) {
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
		goto error;
	}
error:
	diag_set(ClientError, ER_UNKNOWN_UPDATE_OP, op_num,
		 tt_sprintf("\"%.*s\"", len, opcode));
	return NULL;
}

int
xrow_update_op_next_token(struct xrow_update_op *op)
{
	struct json_token token;
	int rc = json_lexer_next_token(&op->lexer, &token);
	if (rc != 0)
		return xrow_update_err_bad_json(op, rc);
	if (token.type == JSON_TOKEN_END)
		return xrow_update_err_no_such_field(op);
	op->is_token_consumed = false;
	op->token_type = token.type;
	op->key = token.str;
	op->key_len = token.len;
	op->field_no = token.num;
	return 0;
}

int
xrow_update_op_decode(struct xrow_update_op *op, int op_num, int index_base,
		      struct tuple_dictionary *dict, const char **expr)
{
	if (mp_typeof(**expr) != MP_ARRAY) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS, "update operation "
			 "must be an array {op,..}");
		return -1;
	}
	uint32_t len, arg_count = mp_decode_array(expr);
	if (arg_count < 1) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS, "update operation "\
			 "must be an array {op,..}, got empty array");
		return -1;
	}
	if (mp_typeof(**expr) != MP_STR) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "update operation name must be a string");
		return -1;
	}
	const char *opcode = mp_decode_str(expr, &len);
	op->meta = xrow_update_op_by(opcode, len, op_num);
	if (op->meta == NULL)
		return -1;
	op->opcode = *opcode;
	if (arg_count != op->meta->arg_count) {
		const char *str = tt_sprintf("wrong number of arguments, "\
					     "expected %u, got %u",
					     op->meta->arg_count, arg_count);
		diag_set(ClientError, ER_UNKNOWN_UPDATE_OP, op_num, str);
		return -1;
	}
	/*
	 * First token is always num. Even if a user specified a
	 * field name it is converted to num by the tuple
	 * dictionary.
	 */
	op->token_type = JSON_TOKEN_NUM;
	op->is_token_consumed = false;
	int32_t field_no = 0;
	switch(mp_typeof(**expr)) {
	case MP_INT:
	case MP_UINT: {
		op->is_for_root = true;
		json_lexer_create(&op->lexer, NULL, 0, 0);
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
		json_lexer_create(&op->lexer, path, len, TUPLE_INDEX_BASE);
		if (tuple_fieldno_by_name(dict, path, len, hash,
					  &field_no) == 0) {
			op->field_no = (int32_t) field_no;
			op->lexer.offset = len;
			op->is_for_root = true;
			break;
		}
		struct json_token token;
		int rc = json_lexer_next_token(&op->lexer, &token);
		if (rc != 0)
			return xrow_update_err_bad_json(op, rc);
		switch (token.type) {
		case JSON_TOKEN_NUM:
			op->field_no = token.num;
			break;
		case JSON_TOKEN_STR:
			hash = field_name_hash(token.str, token.len);
			if (tuple_fieldno_by_name(dict, token.str, token.len,
						  hash, &field_no) == 0) {
				op->field_no = (int32_t) field_no;
				break;
			}
			FALLTHROUGH;
		default:
			diag_set(ClientError, ER_NO_SUCH_FIELD_NAME,
				 tt_cstr(path, len));
			return -1;
		}
		op->is_for_root = json_lexer_is_eof(&op->lexer);
		break;
	}
	default:
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "field id must be a number or a string");
		return -1;
	}
	return op->meta->read_arg(op, expr, index_base);
}
