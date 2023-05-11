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

/**
 * Return size of the string in bytes.
 */
static int
xrow_update_string_size(const struct xrow_update_string *str)
{
	uint32_t length = 0;
	for (size_t i = 0; i < lengthof(str->array); i++)
		length += str->array[i].length;
	return length;
}

/**
 * Return whether scalar is number or not.
 */
static bool
xrow_update_scalar_is_number(const struct xrow_update_scalar *scalar)
{
	switch (scalar->type) {
	case XUPDATE_TYPE_DECIMAL:
	case XUPDATE_TYPE_DOUBLE:
	case XUPDATE_TYPE_FLOAT:
	case XUPDATE_TYPE_INT:
		return true;
	case XUPDATE_TYPE_NONE:
	case XUPDATE_TYPE_STR:
	default:
		return false;
	}
}

uint32_t
xrow_update_field_sizeof(struct xrow_update_field *field)
{
	switch (field->type) {
	case XUPDATE_NOP:
		return field->size;
	case XUPDATE_SCALAR:
		return xrow_update_scalar_sizeof(&field->scalar);
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
	switch(field->type) {
	case XUPDATE_NOP:
		assert(out_end - out >= field->size);
		memcpy(out, field->data, field->size);
		return field->size;
	case XUPDATE_SCALAR:
		return xrow_update_store_scalar(&field->scalar, this_node, out,
						out_end);
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

void
xrow_update_mp_read_scalar(const char **expr,
			   struct xrow_update_scalar *ret)
{
	int8_t ext_type;
	uint32_t len;
	switch(mp_typeof(**expr)) {
	case MP_UINT:
		ret->type = XUPDATE_TYPE_INT;
		int96_set_unsigned(&ret->int96, mp_decode_uint(expr));
		break;
	case MP_INT:
		ret->type = XUPDATE_TYPE_INT;
		int96_set_signed(&ret->int96, mp_decode_int(expr));
		break;
	case MP_DOUBLE:
		ret->type = XUPDATE_TYPE_DOUBLE;
		ret->dbl = mp_decode_double(expr);
		break;
	case MP_FLOAT:
		ret->type = XUPDATE_TYPE_FLOAT;
		ret->flt = mp_decode_float(expr);
		break;
	case MP_STR: {
		ret->type = XUPDATE_TYPE_STR;
		struct xrow_update_string *str = &ret->str;
		str->array[0].data = mp_decode_str(expr, &str->array[0].length);
		memset(&str->array[1], 0, sizeof(str->array[1]) * 2);
		break;
		}
	case MP_EXT:
		len = mp_decode_extl(expr, &ext_type);
		if (ext_type == MP_DECIMAL) {
			ret->type = XUPDATE_TYPE_DECIMAL;
			decimal_unpack(expr, len, &ret->dec);
			break;
		}
		FALLTHROUGH;
	default:
		ret->type = XUPDATE_TYPE_NONE;
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
	xrow_update_mp_read_scalar(expr, &op->arg.arith.value);
	if (!xrow_update_scalar_is_number(&op->arg.arith.value))
		return xrow_update_err_arg_type(op, "a number");
	return 0;
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

/**
 * Convert scalar to double.
 */
static inline double
xrow_update_scalar_to_double(const struct xrow_update_scalar *scalar)
{
	switch (scalar->type) {
	case XUPDATE_TYPE_DOUBLE:
		return scalar->dbl;
	case XUPDATE_TYPE_FLOAT:
		return scalar->flt;
	case XUPDATE_TYPE_INT:
		if (int96_is_uint64(&scalar->int96)) {
			return int96_extract_uint64(&scalar->int96);
		} else {
			assert(int96_is_neg_int64(&scalar->int96));
			return int96_extract_neg_int64(&scalar->int96);
		}
	case XUPDATE_TYPE_DECIMAL:
	case XUPDATE_TYPE_STR:
	case XUPDATE_TYPE_NONE:
	default:
		unreachable();
	}
	return 0;
}

/**
 * Convert scalar to decimal.
 *
 * @param scalar A scalar value.
 * @param dec Output argument holding the result.
 *
 * @retval NULL Conversion is not possible.
 * @retval not NULL Conversion is successful.
 */
static inline decimal_t *
xrow_update_scalar_to_decimal(const struct xrow_update_scalar *scalar,
			      decimal_t *dec)
{
	switch (scalar->type) {
	case XUPDATE_TYPE_DECIMAL:
		*dec = scalar->dec;
		return dec;
	case XUPDATE_TYPE_DOUBLE:
		return decimal_from_double(dec, scalar->dbl);
	case XUPDATE_TYPE_FLOAT:
		return decimal_from_double(dec, scalar->flt);
	case XUPDATE_TYPE_INT:
		if (int96_is_uint64(&scalar->int96)) {
			uint64_t val = int96_extract_uint64(&scalar->int96);
			return decimal_from_uint64(dec, val);
		} else {
			assert(int96_is_neg_int64(&scalar->int96));
			int64_t val = int96_extract_neg_int64(&scalar->int96);
			return decimal_from_int64(dec, val);
		}
	case XUPDATE_TYPE_STR:
	case XUPDATE_TYPE_NONE:
	default:
		unreachable();
	}
	return NULL;
}

uint32_t
xrow_update_scalar_sizeof(const struct xrow_update_scalar *scalar)
{
	switch (scalar->type) {
	case XUPDATE_TYPE_INT:
		if (int96_is_uint64(&scalar->int96)) {
			uint64_t val = int96_extract_uint64(&scalar->int96);
			return mp_sizeof_uint(val);
		} else {
			int64_t val = int96_extract_neg_int64(&scalar->int96);
			return mp_sizeof_int(val);
		}
		break;
	case XUPDATE_TYPE_DOUBLE:
	case XUPDATE_TYPE_FLOAT:
		return mp_sizeof_double(scalar->dbl);
	case XUPDATE_TYPE_DECIMAL:
		return mp_sizeof_decimal(&scalar->dec);
	case XUPDATE_TYPE_STR:
		return mp_sizeof_str(xrow_update_string_size(&scalar->str));
	case XUPDATE_TYPE_NONE:
	default:
		unreachable();
	}
	return 0;
}

int
xrow_update_op_do_arith(struct xrow_update_op *op,
			struct xrow_update_scalar *ret)
{
	assert(xrow_update_scalar_is_number(&op->arg.arith.value));
	if (!xrow_update_scalar_is_number(ret))
		return xrow_update_err_arg_type(op, "a number");

	struct xrow_update_scalar *arg1 = ret;
	struct xrow_update_scalar *arg2 = &op->arg.arith.value;
	enum xrow_update_scalar_type lowest_type = arg1->type;
	char opcode = op->opcode;

	if (arg1->type > arg2->type)
		lowest_type = arg2->type;

	if (lowest_type == XUPDATE_TYPE_INT) {
		switch(opcode) {
		case '+':
			int96_add(&arg1->int96, &arg2->int96);
			break;
		case '-':
			int96_invert(&arg2->int96);
			int96_add(&arg1->int96, &arg2->int96);
			break;
		default:
			unreachable();
			break;
		}
		if (!int96_is_uint64(&arg1->int96) &&
		    !int96_is_neg_int64(&arg1->int96))
			return xrow_update_err_int_overflow(op);
	} else if (lowest_type >= XUPDATE_TYPE_DOUBLE) {
		double a = xrow_update_scalar_to_double(arg1);
		double b = xrow_update_scalar_to_double(arg2);
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
		if (xrow_update_scalar_to_decimal(arg1, &a) == NULL ||
		    xrow_update_scalar_to_decimal(arg2, &b) == NULL) {
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
xrow_update_op_do_bit(struct xrow_update_op *op,
		      struct xrow_update_scalar *scalar)
{
	if (scalar->type != XUPDATE_TYPE_INT ||
	    !int96_is_uint64(&scalar->int96))
		return xrow_update_err_arg_type(op, "a positive integer");
	uint64_t val = int96_extract_uint64(&scalar->int96);
	struct xrow_update_arg_bit *arg = &op->arg.bit;
	switch (op->opcode) {
	case '&':
		val &= arg->val;
		break;
	case '^':
		val ^= arg->val;
		break;
	case '|':
		val |= arg->val;
		break;
	default:
		unreachable();
	}
	int96_set_unsigned(&scalar->int96, val);
	return 0;
}

int
xrow_update_op_do_splice(struct xrow_update_op *op,
			 struct xrow_update_scalar *scalar)
{
	struct xrow_update_arg_splice *arg = &op->arg.splice;

	if (scalar->type != XUPDATE_TYPE_STR)
		return xrow_update_err_arg_type(op, "a string");

	struct xrow_update_string *str = &scalar->str;
	int32_t str_len = xrow_update_string_size(str);

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
	assert(arg->offset + arg->cut_length <= str_len);

	uint32_t tail_offset = arg->offset + arg->cut_length;
	str->array[0].length = arg->offset;
	str->array[1].data = arg->paste;
	str->array[1].length = arg->paste_length;
	str->array[2].data = str->array[0].data + tail_offset;
	str->array[2].length = str_len - tail_offset;

	return 0;
}

/* }}} do_op helpers. */

/* {{{ store_op */

/**
 * Store string rope representation as MsgPack.
 *
 * @param str A string.
 * @param out A pointer to output buffer.
 *
 * @return Output buffer position after encoded string.
 */
static char *
xrow_update_store_string(const struct xrow_update_string *str, char *out);

uint32_t
xrow_update_store_scalar(const struct xrow_update_scalar *scalar,
			 struct json_token *this_node, char *out, char *out_end)
{
	(void)out_end;
	assert(out_end - out >= xrow_update_scalar_sizeof(scalar));
	char *begin = out;
	switch (scalar->type) {
	case XUPDATE_TYPE_INT:
		if (int96_is_uint64(&scalar->int96)) {
			out = mp_encode_uint(
				out, int96_extract_uint64(&scalar->int96));
		} else {
			assert(int96_is_neg_int64(&scalar->int96));
			out = mp_encode_int(
				out, int96_extract_neg_int64(&scalar->int96));
		}
		break;
	case XUPDATE_TYPE_DOUBLE:
		out = mp_encode_double(out, scalar->dbl);
		break;
	case XUPDATE_TYPE_FLOAT:
		if (this_node != NULL) {
			enum field_type type =
				json_tree_entry(this_node, struct tuple_field,
						token)->type;
			if (type == FIELD_TYPE_DOUBLE) {
				out = mp_encode_double(out, scalar->flt);
				break;
			}
		}
		out = mp_encode_float(out, scalar->flt);
		break;
	case XUPDATE_TYPE_DECIMAL:
		out = mp_encode_decimal(out, &scalar->dec);
		break;
	case XUPDATE_TYPE_STR:
		out = xrow_update_store_string(&scalar->str, out);
		break;
	case XUPDATE_TYPE_NONE:
	default:
		unreachable();
	}
	return out - begin;
}

static char *
xrow_update_store_string(const struct xrow_update_string *str, char *out)
{
	out = mp_encode_strl(out, xrow_update_string_size(str));
	for (size_t i = 0; i < lengthof(str->array); i++) {
		memcpy(out, str->array[i].data, str->array[i].length);
		out += str->array[i].length;
	}
	return out;
}

/* }}} store_op */

static const struct xrow_update_op_meta op_set = {
	xrow_update_read_arg_set, xrow_update_op_do_field_set, 3
};
static const struct xrow_update_op_meta op_insert = {
	xrow_update_read_arg_set, xrow_update_op_do_field_insert, 3
};
static const struct xrow_update_op_meta op_arith = {
	xrow_update_read_arg_arith, xrow_update_op_do_field_arith, 3
};
static const struct xrow_update_op_meta op_bit = {
	xrow_update_read_arg_bit, xrow_update_op_do_field_bit, 3
};
static const struct xrow_update_op_meta op_splice = {
	xrow_update_read_arg_splice, xrow_update_op_do_field_splice, 5
};
static const struct xrow_update_op_meta op_delete = {
	xrow_update_read_arg_delete, xrow_update_op_do_field_delete, 3
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
