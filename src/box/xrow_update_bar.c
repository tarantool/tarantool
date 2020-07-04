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
#include "tuple.h"

/**
 * Finish bar creation only when it is fully initialized and
 * valid. Because if all this is happening inside an upsert()
 * operation, an error in the bar won't stop the whole xrow
 * upsert. This field will still be saved in the result tuple. But
 * in case of an error this operation should be skipped. So this
 * is kept 'nop' when error happens.
 */
static inline int
xrow_update_bar_finish(struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_NOP);
	field->type = XUPDATE_BAR;
	return 0;
}

/**
 * Locate a field to update by @a op's JSON path and initialize
 * @a field as a bar update.
 *
 * @param op Update operation.
 * @param field Field to locate in.
 * @param[out] key_len_or_index One parameter for two values,
 *        depending on where the target point is located: in an
 *        array or a map. In case of map it is size of a key
 *        before the found point. It is used to find range of the
 *        both key and value in '#' operation to drop the pair.
 *        In case of array it is index of the array element to be
 *        able to check how many fields are left for deletion.
 *
 * @retval 0 Success.
 * @retval -1 Not found or invalid JSON.
 */
static inline int
xrow_update_bar_locate(struct xrow_update_op *op,
		       struct xrow_update_field *field,
		       int *key_len_or_index)
{
	/*
	 * Bar update is not flat by definition. It always has a
	 * non empty path. This is why op is expected to be not
	 * terminal.
	 */
	assert(!xrow_update_op_is_term(op));
	/*
	 * Nop means this function can change field->bar and
	 * nothing will break.
	 */
	assert(field->type == XUPDATE_NOP);
	int rc;
	field->bar.op = op;
	field->bar.path = op->lexer.src + op->lexer.offset;
	field->bar.path_len = op->lexer.src_len - op->lexer.offset;
	const char *pos = field->data;
	struct json_token token;
	while ((rc = json_lexer_next_token(&op->lexer, &token)) == 0 &&
	       token.type != JSON_TOKEN_END) {

		switch (token.type) {
		case JSON_TOKEN_NUM:
			field->bar.parent = pos;
			*key_len_or_index = token.num;
			rc = tuple_field_go_to_index(&pos, token.num);
			break;
		case JSON_TOKEN_STR:
			field->bar.parent = pos;
			*key_len_or_index = token.len;
			rc = tuple_field_go_to_key(&pos, token.str, token.len);
			break;
		default:
			assert(token.type == JSON_TOKEN_ANY);
			rc = op->lexer.symbol_count - 1;
			return xrow_update_err_bad_json(op, rc);
		}
		if (rc != 0)
			return xrow_update_err_no_such_field(op);
	}
	if (rc > 0)
		return xrow_update_err_bad_json(op, rc);

	field->bar.point = pos;
	mp_next(&pos);
	field->bar.point_size = pos - field->bar.point;
	return 0;
}

/**
 * Locate an optional field to update by @a op's JSON path. If
 * found or only a last path part is not found, initialize @a
 * field as a bar update. Last path part may not exist and it is
 * ok, for example, for '!' and '=' operations.
 */
static inline int
xrow_update_bar_locate_opt(struct xrow_update_op *op,
			   struct xrow_update_field *field, bool *is_found,
			   int *key_len_or_index)
{
	/*
	 * Bar update is not flat by definition. It always has a
	 * non empty path. This is why op is expected to be not
	 * terminal.
	 */
	assert(!xrow_update_op_is_term(op));
	/*
	 * Nop means this function can change field->bar and
	 * nothing will break.
	 */
	assert(field->type == XUPDATE_NOP);
	int rc;
	field->bar.op = op;
	field->bar.path = op->lexer.src + op->lexer.offset;
	field->bar.path_len = op->lexer.src_len - op->lexer.offset;
	const char *pos = field->data;
	struct json_token token;
	do {
		rc = json_lexer_next_token(&op->lexer, &token);
		if (rc != 0)
			return xrow_update_err_bad_json(op, rc);

		switch (token.type) {
		case JSON_TOKEN_END:
			*is_found = true;
			field->bar.point = pos;
			mp_next(&pos);
			field->bar.point_size = pos - field->bar.point;
			return 0;
		case JSON_TOKEN_NUM:
			field->bar.parent = pos;
			*key_len_or_index = token.num;
			rc = tuple_field_go_to_index(&pos, token.num);
			break;
		case JSON_TOKEN_STR:
			field->bar.parent = pos;
			*key_len_or_index = token.len;
			rc = tuple_field_go_to_key(&pos, token.str, token.len);
			break;
		default:
			assert(token.type == JSON_TOKEN_ANY);
			rc = op->lexer.symbol_count - 1;
			return xrow_update_err_bad_json(op, rc);
		}
	} while (rc == 0);
	assert(rc == -1);
	/* Ensure, that 'token' is next to last path part. */
	struct json_token tmp_token;
	rc = json_lexer_next_token(&op->lexer, &tmp_token);
	if (rc != 0)
		return xrow_update_err_bad_json(op, rc);
	if (tmp_token.type != JSON_TOKEN_END)
		return xrow_update_err_no_such_field(op);

	*is_found = false;
	if (token.type == JSON_TOKEN_NUM) {
		const char *tmp = field->bar.parent;
		if (mp_typeof(*tmp) != MP_ARRAY) {
			return xrow_update_err(op, "can not access by index a "\
					       "non-array field");
		}
		uint32_t size = mp_decode_array(&tmp);
		if ((uint32_t) token.num > size)
			return xrow_update_err_no_such_field(op);
		/*
		 * The updated point is in an array, its position
		 * was not found, and its index is <= size. The
		 * only way how can that happen - the update tries
		 * to append a new array element. The following
		 * code tries to find the array's end.
		 */
		assert((uint32_t) token.num == size);
		if (field->bar.parent == field->data) {
			/*
			 * Optimization for the case when the path
			 * is short. So parent of the updated
			 * point is the field itself. It allows
			 * not to decode anything at all. It is
			 * worth doing, since the paths are
			 * usually short.
			 */
			field->bar.point = field->data + field->size;
		} else {
			field->bar.point = field->bar.parent;
			mp_next(&field->bar.point);
		}
		field->bar.point_size = 0;
	} else {
		assert(token.type == JSON_TOKEN_STR);
		field->bar.new_key = token.str;
		field->bar.new_key_len = token.len;
		if (mp_typeof(*field->bar.parent) != MP_MAP) {
			return xrow_update_err(op, "can not access by key a "\
					       "non-map field");
		}
	}
	return 0;
}

/**
 * Nop fields are those which are not updated. And when they
 * receive an update via one of xrow_update_op_do_nop_* functions,
 * it means, that there is a non terminal path digging inside this
 * not updated field. It turns nop field into a bar field. How
 * exactly - depends on a concrete operation.
 */

int
xrow_update_op_do_nop_insert(struct xrow_update_op *op,
			     struct xrow_update_field *field)
{
	assert(op->opcode == '!');
	assert(field->type == XUPDATE_NOP);
	bool is_found = false;
	int key_len = 0;
	if (xrow_update_bar_locate_opt(op, field, &is_found, &key_len) != 0)
		return -1;
	op->new_field_len = op->arg.set.length;
	if (mp_typeof(*field->bar.parent) == MP_MAP) {
		if (is_found)
			return xrow_update_err_duplicate(op);
		/*
		 * Don't forget, that map element is a pair. So
		 * key length also should be accounted.
		 */
		op->new_field_len += mp_sizeof_str(key_len);
	}
	return xrow_update_bar_finish(field);
}

int
xrow_update_op_do_nop_set(struct xrow_update_op *op,
			  struct xrow_update_field *field)
{
	assert(op->opcode == '=');
	assert(field->type == XUPDATE_NOP);
	bool is_found = false;
	int key_len = 0;
	if (xrow_update_bar_locate_opt(op, field, &is_found, &key_len) != 0)
		return -1;
	op->new_field_len = op->arg.set.length;
	if (!is_found) {
		op->opcode = '!';
		if (mp_typeof(*field->bar.parent) == MP_MAP)
			op->new_field_len += mp_sizeof_str(key_len);
	}
	return xrow_update_bar_finish(field);
}

int
xrow_update_op_do_nop_delete(struct xrow_update_op *op,
			     struct xrow_update_field *field)
{
	assert(op->opcode == '#');
	assert(field->type == XUPDATE_NOP);
	int key_len_or_index = 0;
	if (xrow_update_bar_locate(op, field, &key_len_or_index) != 0)
		return -1;
	if (mp_typeof(*field->bar.parent) == MP_ARRAY) {
		const char *tmp = field->bar.parent;
		uint32_t size = mp_decode_array(&tmp);
		if (key_len_or_index + op->arg.del.count > size)
			op->arg.del.count = size - key_len_or_index;
		const char *end = field->bar.point + field->bar.point_size;
		for (uint32_t i = 1; i < op->arg.del.count; ++i)
			mp_next(&end);
		field->bar.point_size = end - field->bar.point;
	} else {
		if (op->arg.del.count != 1)
			return xrow_update_err_delete1(op);
		/* Take key size into account to delete it too. */
		key_len_or_index = mp_sizeof_str(key_len_or_index);
		field->bar.point -= key_len_or_index;
		field->bar.point_size += key_len_or_index;
	}
	return xrow_update_bar_finish(field);
}

#define DO_NOP_OP_GENERIC(op_type)						\
int										\
xrow_update_op_do_nop_##op_type(struct xrow_update_op *op,			\
				struct xrow_update_field *field)		\
{										\
	assert(field->type == XUPDATE_NOP);					\
	int key_len_or_index;							\
	if (xrow_update_bar_locate(op, field, &key_len_or_index) != 0)		\
		return -1;							\
	if (xrow_update_op_do_##op_type(op, field->bar.point) != 0)		\
		return -1;							\
	return xrow_update_bar_finish(field);					\
}

DO_NOP_OP_GENERIC(arith)

DO_NOP_OP_GENERIC(bit)

DO_NOP_OP_GENERIC(splice)

#undef DO_NOP_OP_GENERIC

#define DO_BAR_OP_GENERIC(op_type)						\
int										\
xrow_update_op_do_bar_##op_type(struct xrow_update_op *op,			\
				struct xrow_update_field *field)		\
{										\
	assert(field->type == XUPDATE_BAR);					\
	field = xrow_update_route_branch(field, op);				\
	if (field == NULL)							\
		return -1;							\
	return xrow_update_op_do_field_##op_type(op, field);			\
}

DO_BAR_OP_GENERIC(insert)

DO_BAR_OP_GENERIC(set)

DO_BAR_OP_GENERIC(delete)

DO_BAR_OP_GENERIC(arith)

DO_BAR_OP_GENERIC(bit)

DO_BAR_OP_GENERIC(splice)

#undef DO_BAR_OP_GENERIC

uint32_t
xrow_update_bar_sizeof(struct xrow_update_field *field)
{
	assert(field->type == XUPDATE_BAR);
	switch(field->bar.op->opcode) {
	case '!': {
		const char *parent = field->bar.parent;
		uint32_t size = field->size + field->bar.op->new_field_len;
		if (mp_typeof(*parent) == MP_ARRAY) {
			uint32_t array_size = mp_decode_array(&parent);
			return size + mp_sizeof_array(array_size + 1) -
			       mp_sizeof_array(array_size);
		} else {
			uint32_t map_size = mp_decode_map(&parent);
			return size + mp_sizeof_map(map_size + 1) -
			       mp_sizeof_map(map_size);
		}
	}
	case '#': {
		const char *parent = field->bar.parent;
		uint32_t delete_count = field->bar.op->arg.del.count;
		uint32_t size = field->size - field->bar.point_size;
		if (mp_typeof(*parent) == MP_ARRAY) {
			uint32_t array_size = mp_decode_array(&parent);
			assert(array_size >= delete_count);
			return size - mp_sizeof_array(array_size) +
			       mp_sizeof_array(array_size - delete_count);
		} else {
			uint32_t map_size = mp_decode_map(&parent);
			assert(delete_count == 1);
			return size - mp_sizeof_map(map_size) +
			       mp_sizeof_map(map_size - 1);
		}
	}
	default: {
		return field->size - field->bar.point_size +
		       field->bar.op->new_field_len;
	}
	}
}

uint32_t
xrow_update_bar_store(struct xrow_update_field *field,
		      struct json_tree *format_tree,
		      struct json_token *this_node, char *out, char *out_end)
{
	assert(field->type == XUPDATE_BAR);
	(void) out_end;
	struct xrow_update_op *op = field->bar.op;
	char *out_saved = out;
	switch(op->opcode) {
	case '!': {
		const char *pos = field->bar.parent;
		uint32_t before_parent = pos - field->data;
		/* Before parent. */
		memcpy(out, field->data, before_parent);
		out += before_parent;
		if (mp_typeof(*pos) == MP_ARRAY) {
			/* New array header. */
			uint32_t size = mp_decode_array(&pos);
			out = mp_encode_array(out, size + 1);
			/* Before insertion point. */
			size = field->bar.point - pos;
			memcpy(out, pos, size);
			out += size;
			pos += size;
		} else {
			/* New map header. */
			uint32_t size = mp_decode_map(&pos);
			out = mp_encode_map(out, size + 1);
			/* New key. */
			out = mp_encode_str(out, field->bar.new_key,
					    field->bar.new_key_len);
		}
		/* New value. */
		memcpy(out, op->arg.set.value, op->arg.set.length);
		out += op->arg.set.length;
		/* Old values and field tail. */
		uint32_t after_point = field->data + field->size - pos;
		memcpy(out, pos, after_point);
		out += after_point;
		return out - out_saved;
	}
	case '#': {
		const char *pos = field->bar.parent;
		uint32_t size, before_parent = pos - field->data;
		memcpy(out, field->data, before_parent);
		out += before_parent;
		if (mp_typeof(*pos) == MP_ARRAY) {
			size = mp_decode_array(&pos);
			out = mp_encode_array(out, size - op->arg.del.count);
		} else {
			size = mp_decode_map(&pos);
			out = mp_encode_map(out, size - 1);
		}
		size = field->bar.point - pos;
		memcpy(out, pos, size);
		out += size;
		pos = field->bar.point + field->bar.point_size;

		size = field->data + field->size - pos;
		memcpy(out, pos, size);
		return out + size - out_saved;
	}
	default: {
		if (this_node != NULL) {
			this_node = json_tree_lookup_path(
				format_tree, this_node, field->bar.path,
				field->bar.path_len, 0);
		}
		uint32_t before_point = field->bar.point - field->data;
		const char *field_end = field->data + field->size;
		const char *point_end =
			field->bar.point + field->bar.point_size;
		uint32_t after_point = field_end - point_end;

		memcpy(out, field->data, before_point);
		out += before_point;
		out += op->meta->store(op, format_tree, this_node,
				       field->bar.point, out);
		memcpy(out, point_end, after_point);
		return out + after_point - out_saved;
	}
	}
}
