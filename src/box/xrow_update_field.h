#ifndef TARANTOOL_BOX_TUPLE_UPDATE_FIELD_H
#define TARANTOOL_BOX_TUPLE_UPDATE_FIELD_H
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
#include "trivia/util.h"
#include "tt_static.h"
#include "json/json.h"
#include "bit/int96.h"
#include "mp_decimal.h"
#include <stdint.h>

/**
 * This file is a link between all the update operations for all
 * the field types. It functions like a router, when an update
 * operation is being parsed step by step, going down the update
 * tree. For example, when an update operation goes through an
 * array, a map, another array, and ends with a scalar operation,
 * at the end of each step the operation goes to the next one via
 * functions of this file: xrow_update_array.c ->
 * xrow_update_field.c -> xrow_update_map.c ->
 * xrow_update_field.c -> xrow_update_array.c -> ... . The
 * functions, doing the routing, are
 * xrow_update_op_do_field_<operation_type>(), see them below.
 */

struct xrow_update_rope;
struct xrow_update_field;
struct xrow_update_op;
struct tuple_dictionary;

/* {{{ xrow_update_op */

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
 * To perform an arithmetic operation, update first loads left
 * and right arguments into corresponding value objects, then
 * performs arithmetics on types of arguments, thus calculating
 * the type of the result, and then performs the requested
 * operation according to the calculated type rules.
 *
 * The rules are as follows:
 * - when one of the argument types is double, the result is
 *   double;
 * - when one of the argument types is float, the result is float;
 * - when one of the arguments is a decimal, the result is decimal
 *   too;
 * - for integer arguments, the result type code depends on the
 *   range in which falls the result of the operation. If the
 *   result is in negative range, it's MP_INT, otherwise it's
 *   MP_UINT. If the result is out of bounds of (-2^63, 2^64), an
 *   exception is raised for overflow.
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
	/** Splice position. */
	int32_t offset;
	/** Byte count to delete. */
	int32_t cut_length;
	/** New content. */
	const char *paste;
	/** New content length. */
	uint32_t paste_length;

	/** Offset of the tail in the old field. */
	int32_t tail_offset;
	/** Size of the tail. */
	int32_t tail_length;
};

/** Update operation argument. */
union xrow_update_arg {
	struct xrow_update_arg_set set;
	struct xrow_update_arg_del del;
	struct xrow_update_arg_arith arith;
	struct xrow_update_arg_bit bit;
	struct xrow_update_arg_splice splice;
};

typedef int
(*xrow_update_op_read_arg_f)(struct xrow_update_op *op, const char **expr,
			     int index_base);

typedef int
(*xrow_update_op_do_f)(struct xrow_update_op *op,
		       struct xrow_update_field *field);

typedef void
(*xrow_update_op_store_f)(struct xrow_update_op *op, const char *in, char *out);

/**
 * A set of functions and properties to initialize, do and store
 * an operation.
 */
struct xrow_update_op_meta {
	/**
	 * Virtual function to read the arguments of the
	 * operation.
	 */
	xrow_update_op_read_arg_f read_arg;
	/** Virtual function to execute the operation. */
	xrow_update_op_do_f do_op;
	/**
	 * Virtual function to store a result of the operation.
	 */
	xrow_update_op_store_f store;
	/** Argument count. */
	uint32_t arg_count;
};

/** A single UPDATE operation. */
struct xrow_update_op {
	/** Operation meta depending on the op type. */
	const struct xrow_update_op_meta *meta;
	/** Operation arguments. */
	union xrow_update_arg arg;
	/** Current level token. */
	enum json_token_type token_type;
	/**
	 * The flag says whether the token is already consumed by
	 * the update operation during its forwarding down the
	 * update tree. When the flag is true, it means that the
	 * next node of the update tree will need to fetch a next
	 * token from the lexer.
	 */
	bool is_token_consumed;
	union {
		struct {
			const char *key;
			uint32_t key_len;
		};
		int32_t field_no;
	};
	/** Size of a new field after it is updated. */
	uint32_t new_field_len;
	/** Opcode symbol: = + - / ... */
	char opcode;
	/**
	 * Operation target path and its lexer in one. This lexer
	 * is used when the operation is applied down through the
	 * update tree.
	 */
	struct json_lexer lexer;
};

/**
 * Extract a next token from the operation path lexer. The result
 * is used to decide to which child of a current map/array the
 * operation should be forwarded. It is not just a synonym to
 * json_lexer_next_token, because fills some fields of @a op,
 * and should be used only to chose a next child inside a current
 * map/array.
 */
int
xrow_update_op_next_token(struct xrow_update_op *op);

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
int
xrow_update_op_decode(struct xrow_update_op *op, int index_base,
		      struct tuple_dictionary *dict, const char **expr);

/**
 * Check if the operation should be applied on the current path
 * node, i.e. it is terminal. When an operation is just decoded
 * and is applied to the top level of a tuple, a head of the JSON
 * path is cut out. If nothing left, it is applied there.
 * Otherwise the operation is applied to the next level of the
 * tuple, according to where the path goes, and so on. In the end
 * it reaches the target point, where it becomes terminal.
 */
static inline bool
xrow_update_op_is_term(const struct xrow_update_op *op)
{
	return json_lexer_is_eof(&op->lexer);
}

/* }}} xrow_update_op */

/* {{{ xrow_update_field */

/** Types of field update. */
enum xrow_update_type {
	/**
	 * Field is not updated. Just save it as is. It is used,
	 * for example, when a rope is split in two parts: not
	 * changed left range of fields, and a right range with
	 * its first field changed. In this case the left range is
	 * NOP.
	 */
	XUPDATE_NOP,
	/**
	 * Field is a scalar value, updated via set, arith, bit,
	 * splice, or any other scalar-applicable operation.
	 */
	XUPDATE_SCALAR,
	/**
	 * Field is an updated array. Check the rope for updates
	 * of individual fields.
	 */
	XUPDATE_ARRAY,
	/**
	 * Field of this type stores such update, that has
	 * non-empty JSON path isolated from all other update
	 * operations. In such optimized case it is possible to do
	 * not allocate neither fields nor ops nor anything for
	 * path nodes. And this is the most common case.
	 */
	XUPDATE_BAR,
};

/**
 * Generic structure describing update of a field. It can be
 * tuple field, field of a tuple field, or any another tuple
 * internal object: map, array, scalar, or unchanged field of any
 * type without op. This is a node of an update field tree.
 */
struct xrow_update_field {
	/**
	 * Type of this field's update. Union below depends on it.
	 */
	enum xrow_update_type type;
	/** Field data to update. */
	const char *data;
	uint32_t size;
	union {
		/**
		 * This update is terminal. It does a scalar
		 * operation and has no children fields.
		 */
		struct {
			struct xrow_update_op *op;
		} scalar;
		/**
		 * This update changes an array. Children fields
		 * are stored in rope nodes.
		 */
		struct {
			struct xrow_update_rope *rope;
		} array;
		/**
		 * Bar update - by an isolated JSON path not
		 * intersected with any another update operation.
		 */
		struct {
			/**
			 * Bar update is a single operation
			 * always, no children, by definition.
			 */
			struct xrow_update_op *op;
			/**
			 * Always has a non-empty path leading
			 * inside this field's data. This is used
			 * to find the longest common prefix, when
			 * a new update operation intersects with
			 * this bar.
			 */
			const char *path;
			int path_len;
			/**
			 * For insertion/deletion to change
			 * parent's header.
			 */
			const char *parent;
			union {
				/**
				 * For scalar op; insertion into
				 * array; deletion. This is the
				 * point to delete, change or
				 * insert after.
				 */
				struct {
					const char *point;
					uint32_t point_size;
				};
				/*
				 * For insertion into map. New
				 * key. On insertion into a map
				 * there is no strict order as in
				 * array and no point. The field
				 * is inserted just right after
				 * the parent header.
				 */
				struct {
					const char *new_key;
					uint32_t new_key_len;
				};
			};
		} bar;
	};
};

/**
 * Update_field has a generic API and a typed API. The generic API
 * takes fields of any type. These are:
 *
 *     xrow_update_field_sizeof
 *     xrow_update_field_store
 *     xrow_update_op_do_field_<operation>
 *
 * Typed API is used when type of an update_field is known in
 * code, and by generic API internally. These are
 *
 *     xrow_update_<type>_sizeof
 *     xrow_update_<type>_store
 *     xrow_update_op_do_<type>_<operation>
 *
 * Sizeof calculates size of the whole subtree of a given
 * update_field. Store saves the whole subtree. Operation
 * executors apply an operation to one of the nodes in the
 * subtree. They may change the update tree structure.
 */

/**
 * Size of the updated field, including all children recursively.
 */
uint32_t
xrow_update_field_sizeof(struct xrow_update_field *field);

/** Save the updated field, including all children recursively. */
uint32_t
xrow_update_field_store(struct xrow_update_field *field, char *out,
			char *out_end);

/**
 * Generate declarations for a concrete field type: array, bar
 * etc. Each complex type has basic operations of the same
 * signature: insert, set, delete, arith, bit, splice.
 */
#define OP_DECL_GENERIC(type)							\
int										\
xrow_update_op_do_##type##_insert(struct xrow_update_op *op,			\
				  struct xrow_update_field *field);		\
										\
int										\
xrow_update_op_do_##type##_set(struct xrow_update_op *op,			\
			       struct xrow_update_field *field);		\
										\
int										\
xrow_update_op_do_##type##_delete(struct xrow_update_op *op,			\
				  struct xrow_update_field *field);		\
										\
int										\
xrow_update_op_do_##type##_arith(struct xrow_update_op *op,			\
				 struct xrow_update_field *field);		\
										\
int										\
xrow_update_op_do_##type##_bit(struct xrow_update_op *op,			\
			       struct xrow_update_field *field);		\
										\
int										\
xrow_update_op_do_##type##_splice(struct xrow_update_op *op,			\
				  struct xrow_update_field *field);		\
										\
uint32_t									\
xrow_update_##type##_sizeof(struct xrow_update_field *field);			\
										\
uint32_t									\
xrow_update_##type##_store(struct xrow_update_field *field, char *out,		\
			   char *out_end);

/* }}} xrow_update_field */

/* {{{ xrow_update_field.array */

/**
 * Initialize @a field as an array to update.
 * @param[out] field Field to initialize.
 * @param header Header of the MessagePack array @a data.
 * @param data MessagePack data of the array to update.
 * @param data_end End of @a data.
 * @param field_count Field count in @data.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
int
xrow_update_array_create(struct xrow_update_field *field, const char *header,
			 const char *data, const char *data_end,
			 uint32_t field_count);

OP_DECL_GENERIC(array)

/* }}} xrow_update_field.array */

/* {{{ update_field.bar */

OP_DECL_GENERIC(bar)

/* }}} update_field.bar */

/* {{{ update_field.nop */

OP_DECL_GENERIC(nop)

/* }}} update_field.nop */

#undef OP_DECL_GENERIC

/* {{{ Common helpers. */

/**
 * These helper functions are used when a function, updating a
 * field, doesn't know type of a child node to which wants to
 * propagate the update.
 * For example, xrow_update_op_do_array_set calls
 * xrow_update_op_do_field_set on its childs. Each child can be
 * another array, a bar, a route, a map - anything. The functions
 * below help to make such places shorter and simpler.
 */
#define OP_DECL_GENERIC(op_type)						\
static inline int								\
xrow_update_op_do_field_##op_type(struct xrow_update_op *op,			\
				  struct xrow_update_field *field)		\
{										\
	switch (field->type) {							\
	case XUPDATE_ARRAY:							\
		return xrow_update_op_do_array_##op_type(op, field);		\
	case XUPDATE_NOP:							\
		return xrow_update_op_do_nop_##op_type(op, field);		\
	case XUPDATE_BAR:							\
		return xrow_update_op_do_bar_##op_type(op, field);		\
	default:								\
		unreachable();							\
	}									\
	return 0;								\
}

OP_DECL_GENERIC(insert)

OP_DECL_GENERIC(set)

OP_DECL_GENERIC(delete)

OP_DECL_GENERIC(arith)

OP_DECL_GENERIC(bit)

OP_DECL_GENERIC(splice)

#undef OP_DECL_GENERIC

/* }}} Common helpers. */

/* {{{ Scalar helpers. */

int
xrow_update_arith_make(struct xrow_update_op *op,
		       struct xrow_update_arg_arith arg,
		       struct xrow_update_arg_arith *ret);

void
xrow_update_op_store_arith(struct xrow_update_op *op, const char *in,
			   char *out);

uint32_t
xrow_update_arg_arith_sizeof(const struct xrow_update_arg_arith *arg);

int
xrow_update_op_do_arith(struct xrow_update_op *op, const char *old);

int
xrow_update_op_do_bit(struct xrow_update_op *op, const char *old);

int
xrow_update_op_do_splice(struct xrow_update_op *op, const char *old);

/* }}} Scalar helpers. */

/** {{{ Error helpers. */
/**
 * All the error helpers below set diag with appropriate error
 * code, taking into account field_no < 0, complex paths. They all
 * return -1 to shorten error returning in a caller function to
 * single line.
 */

int
xrow_update_err_no_such_field(const struct xrow_update_op *op);

int
xrow_update_err(const struct xrow_update_op *op, const char *reason);

static inline int
xrow_update_err_double(const struct xrow_update_op *op)
{
	return xrow_update_err(op, "double update of the same field");
}

static inline int
xrow_update_err_bad_json(const struct xrow_update_op *op, int pos)
{
	return xrow_update_err(op, tt_sprintf("invalid JSON in position %d",
					      pos));
}

static inline int
xrow_update_err_delete1(const struct xrow_update_op *op)
{
	return xrow_update_err(op, "can delete only 1 field from a map in a "\
			       "row");
}

static inline int
xrow_update_err_duplicate(const struct xrow_update_op *op)
{
	return xrow_update_err(op, "the key exists already");
}

/** }}} Error helpers. */

#endif /* TARANTOOL_BOX_TUPLE_UPDATE_FIELD_H */
