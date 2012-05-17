/*
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
#include "request.h"
#include <objc/runtime.h>
#include "txn.h"
#include "tuple.h"
#include "index.h"
#include "space.h"
#include "port.h"
#include "box_lua.h"
#include <errinj.h>
#include <tbuf.h>
#include <pickle.h>
#include <fiber.h>

STRS(requests, REQUESTS);
STRS(update_op_codes, UPDATE_OP_CODES);

static void
read_key(struct tbuf *data, void **key_ptr, u32 *key_part_count_ptr)
{
	void *key = NULL;
	u32 key_part_count = read_u32(data);
	if (key_part_count) {
		key = read_field(data);
		/* advance remaining fields of a key */
		for (int i = 1; i < key_part_count; i++)
			read_field(data);
	}

	*key_ptr = key;
	*key_part_count_ptr = key_part_count;
}

static struct space *
read_space(struct tbuf *data)
{
	u32 space_no = read_u32(data);
	return space_find(space_no);
}

static void
port_send_tuple(u32 flags, Port *port, struct tuple *tuple)
{
	if (tuple) {
		[port dupU32: 1]; /* affected tuples */
		if (flags & BOX_RETURN_TUPLE)
			[port addTuple: tuple];
	} else {
		[port dupU32: 0]; /* affected tuples. */
	}
}

@interface Replace: Request
- (void) execute: (struct txn *) txn :(Port *) port;
@end

@implementation Replace
- (void) execute: (struct txn *) txn :(Port *) port
{
	txn_add_redo(txn, type, data);
	struct space *sp = read_space(data);
	u32 flags = read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
	size_t field_count = read_u32(data);

	if (field_count == 0)
		tnt_raise(IllegalParams, :"tuple field count is 0");

	if (data->size == 0 || data->size != valid_tuple(data, field_count))
		tnt_raise(IllegalParams, :"incorrect tuple length");

	txn->new_tuple = tuple_alloc(data->size);
	txn->new_tuple->field_count = field_count;
	memcpy(txn->new_tuple->data, data->data, data->size);

	struct tuple *old_tuple = [sp->index[0] findByTuple: txn->new_tuple];

	if (flags & BOX_ADD && old_tuple != NULL)
		tnt_raise(ClientError, :ER_TUPLE_FOUND);

	if (flags & BOX_REPLACE && old_tuple == NULL)
		tnt_raise(ClientError, :ER_TUPLE_NOT_FOUND);

	space_validate(sp, old_tuple, txn->new_tuple);

	/** XXX: bug! This must be in space_validate(),
	 * not here. Update can also replace a tuple,
	 * and it can modify the primary key. We do not
	 * take a gap lock for the tuple inserted by UPDATE.
	 */
#ifndef NDEBUG
	if (old_tuple != NULL) {
		Index *index = sp->index[0];
		void *ka, *kb;
		ka = tuple_field(txn->new_tuple,
				 index->key_def->parts[0].fieldno);
		kb = tuple_field(old_tuple, index->key_def->parts[0].fieldno);
		int kal, kab;
		kal = load_varint32(&ka);
		kab = load_varint32(&kb);
		assert(kal == kab && memcmp(ka, kb, kal) == 0);
	}
#endif
	txn_add_undo(txn, sp, old_tuple, txn->new_tuple);

	[port dupU32: 1]; /* Affected tuples */

	if (flags & BOX_RETURN_TUPLE)
		[port addTuple: txn->new_tuple];
}
@end

/** {{{ UPDATE request implementation.
 * UPDATE request is represented by a sequence of operations,
 * each working with a single field. However, there
 * can be more than one operation on the same field.
 * Supported operations are: SET, ADD, bitwise AND, XOR and OR,
 * SPLICE and DELETE.
 *
 * The typical case is when the operation count is much less
 * than field count in a tuple.
 *
 * To ensure minimal use of intermediate memory, UPDATE is
 * performed in a streaming fashion: all operations in the request
 * are sorted by field number. The resulting tuple length is
 * calculated. A new tuple is allocated. Operation are applied
 * sequentially, each copying data from the old tuple to the new
 * data.
 * With this approach we have in most cases linear (tuple length)
 * UPDATE complexity and copy data from the old tuple to the new
 * one only once.
 *
 * There are complications in this scheme caused by multiple
 * operations on the same field: for example, we may have
 * SET(4, "aaaaaa"), SPLICE(4, 0, 5, 0, ""), resulting in
 * zero increase of total tuple length, but requiring an
 * intermediate buffer to store SET results. Please
 * read the source of do_update_ops() to see how these
 * complications  are worked around.
 */

@interface Update: Request
- (void) execute: (struct txn *) txn :(Port *) port;
@end

/** Argument of SET operation. */
struct op_set_arg {
	u32 length;
	void *value;
};

/** Argument of ADD, AND, XOR, OR operations. */
struct op_arith_arg {
	u32 val_size;
	union {
		i32 i32_val;
		i64 i64_val;
	};
};

/** Argument of SPLICE. */
struct op_splice_arg {
	i32 offset;	/** splice position */
	i32 cut_length; /** cut this many bytes. */
	void *paste;      /** paste what? */
	i32 paste_length; /** paste this many bytes. */

	/** Inferred data */
	i32 tail_offset;
	i32 tail_length;
};

union update_op_arg {
	struct op_set_arg set;
	struct op_arith_arg arith;
	struct op_splice_arg splice;
};

struct update_cmd;
struct update_field;
struct update_op;

typedef void (*init_op_func)(struct update_cmd *cmd,
			     struct update_field *field,
			     struct update_op *op);
typedef void (*do_op_func)(union update_op_arg *arg, void *in, void *out);

/** A set of functions and properties to initialize and do an op. */
struct update_op_meta {
	init_op_func init_op;
	do_op_func do_op;
	bool works_in_place;
};

/** A single UPDATE operation. */
struct update_op {
	struct update_op_meta *meta;
	union update_op_arg arg;
	u32 field_no;
	u32 new_field_len;
	u8 opcode;
};

/**
 * We can have more than one operation on the same field.
 * A descriptor of one changed field.
 */
struct update_field {
	/** Pointer to the first operation on this field. */
	struct update_op *first;
	/** Points after the last operation on this field. */
	struct update_op *end;
	/** Points at start of field *data* in the old tuple. */
	void *old;
	/** The final length of the new field. */
	u32 new_len;
	/** End of the old field. */
	void *tail;
	/** Copy old data after this field. */
	u32 tail_len;
	/** How many fields we're copying. */
	int tail_field_count;
};

/** UPDATE command context. */
struct update_cmd {
	/** Search key */
	void *key;
	/** Search key part count. */
	u32 key_part_count;
	/** Operations. */
	struct update_op *op;
	struct update_op *op_end;
	/* Distinct fields affected by UPDATE. */
	struct update_field *field;
	struct update_field *field_end;
	/** new tuple length after all operations are applied. */
	u32 new_tuple_len;
	u32 new_field_count;
};

static int
update_op_cmp(const void *op1_ptr, const void *op2_ptr)
{
	const struct update_op *op1 = op1_ptr;
	const struct update_op *op2 = op2_ptr;

	/* Compare operations by field number. */
	int result = (int) op1->field_no - (int) op2->field_no;
	if (result)
		return result;
	/*
	 * INSERT operations create a new field
	 * at index field_no, shifting other fields to the right.
	 * Operations on field_no are done on the field
	 * in the old tuple and do not affect the inserted
	 * field. Therefore, field insertion should be
	 * done first, followed by all other operations
	 * on the given field_no.
	 */
	result = (op2->opcode == UPDATE_OP_INSERT) -
		(op1->opcode == UPDATE_OP_INSERT);
	if (result)
		return result;
	/*
	 * We end up here in two cases:
	 *   1) both operations are INSERTs,
	 *   2) both operations are not INSERTs.
	 *
	 * Preserve the original order of operations on the same
	 * field. To do it, order them by their address in the
	 * UPDATE request.
	 *
	 * The expression below should work even if sizeof(ptrdiff_t)
	 * is greater than sizeof(int) because we presume that both
	 * value addresses belong to the same UPDATE command buffer
	 * and therefore their difference must be small enough to fit
	 * into an int comfortably.
	 */
	return (int) (op1->arg.set.value - op2->arg.set.value);
}

static void
do_update_op_set(struct op_set_arg *arg, void *in __attribute__((unused)),
		 void *out)
{
	memcpy(out, arg->value, arg->length);
}

static void
do_update_op_add(struct op_arith_arg *arg, void *in, void *out)
{
	switch (arg->val_size) {
	case sizeof(i32):
		*(i32 *)out = *(i32 *)in + arg->i32_val;
		break;
	case sizeof(i64):
		*(i64 *)out = *(i64 *)in + arg->i64_val;
		break;
	}
}

static void
do_update_op_and(struct op_arith_arg *arg, void *in, void *out)
{
	switch (arg->val_size) {
	case sizeof(i32):
		*(i32 *)out = *(i32 *)in & arg->i32_val;
		break;
	case sizeof(i64):
		*(i64 *)out = *(i64 *)in & arg->i64_val;
		break;
	}
}

static void
do_update_op_xor(struct op_arith_arg *arg, void *in, void *out)
{
	switch (arg->val_size) {
	case sizeof(i32):
		*(i32 *)out = *(i32 *)in ^ arg->i32_val;
		break;
	case sizeof(i64):
		*(i64 *)out = *(i64 *)in ^ arg->i64_val;
		break;
	}
}

static void
do_update_op_or(struct op_arith_arg *arg, void *in, void *out)
{
	switch (arg->val_size) {
	case sizeof(i32):
		*(i32 *)out = *(i32 *)in | arg->i32_val;
		break;
	case sizeof(i64):
		*(i64 *)out = *(i64 *)in | arg->i64_val;
		break;
	}
}

static void
do_update_op_splice(struct op_splice_arg *arg, void *in, void *out)
{
	memcpy(out, in, arg->offset);           /* copy field head. */
	out += arg->offset;
	memcpy(out, arg->paste, arg->paste_length); /* copy the paste */
	out += arg->paste_length;
	memcpy(out, in + arg->tail_offset, arg->tail_length); /* copy tail */
}

static void
do_update_op_insert(struct op_set_arg *arg, void *in __attribute__((unused)),
		 void *out)
{
	memcpy(out, arg->value, arg->length);
}

static void
do_update_op_none(struct op_set_arg *arg, void *in, void *out)
{
	memcpy(out, in, arg->length);
}

static void
init_update_op_set(struct update_cmd *cmd __attribute__((unused)),
		   struct update_field *field, struct update_op *op)
{
	/* Skip all previous ops. */
	field->first = op;
	op->new_field_len = op->arg.set.length;
}

static void
init_update_op_arith(struct update_cmd *cmd __attribute__((unused)),
		     struct update_field *field, struct update_op *op)
{
	struct op_arith_arg *arg = &op->arg.arith;

	switch (field->new_len) {
	case sizeof(i32):
		/* 32-bit operation */

		/* Check the operand type. */
		if (op->arg.set.length != sizeof(i32))
			tnt_raise(ClientError, :ER_ARG_TYPE,
				  "32-bit int");

		arg->i32_val = *(i32 *)op->arg.set.value;
		break;
	case sizeof(i64):
		/* 64-bit operation */
		switch (op->arg.set.length) {
		case sizeof(i32):
			/* 32-bit operand */
			/* cast 32-bit operand to 64-bit */
			arg->i64_val = *(i32 *)op->arg.set.value;
			break;
		case sizeof(i64):
			/* 64-bit operand */
			arg->i64_val = *(i64 *)op->arg.set.value;
			break;
		default:
			tnt_raise(ClientError, :ER_ARG_TYPE,
				  "32-bit or 64-bit int");
		}
		break;
	default:
		tnt_raise(ClientError, :ER_FIELD_TYPE,
			  "32-bit or 64-bit int");
	}
	arg->val_size = op->new_field_len = field->new_len;
}

static void
init_update_op_splice(struct update_cmd *cmd __attribute__((unused)),
		      struct update_field *field, struct update_op *op)
{
	u32 field_len = field->new_len;
	struct tbuf operands = {
		.capacity = op->arg.set.length,
		.size = op->arg.set.length,
		.data = op->arg.set.value,
		.pool = NULL
	};
	struct op_splice_arg *arg = &op->arg.splice;

	/* Read the offset. */
	void *offset_field = read_field(&operands);
	u32 len = load_varint32(&offset_field);
	if (len != sizeof(i32))
		tnt_raise(IllegalParams, :"SPLICE offset");
	/* Sic: overwrite of op->arg.set.length. */
	arg->offset = *(i32 *)offset_field;
	if (arg->offset < 0) {
		if (-arg->offset > field_len)
			tnt_raise(ClientError, :ER_SPLICE,
				  "offset is out of bound");
		arg->offset += field_len;
	} else if (arg->offset > field_len) {
		arg->offset = field_len;
	}
	assert(arg->offset >= 0 && arg->offset <= field_len);

	/* Read the cut length. */
	void *length_field = read_field(&operands);
	len = load_varint32(&length_field);
	if (len != sizeof(i32))
		tnt_raise(IllegalParams, :"SPLICE length");
	arg->cut_length = *(i32 *)length_field;
	if (arg->cut_length < 0) {
		if (-arg->cut_length > (field_len - arg->offset))
			arg->cut_length = 0;
		else
			arg->cut_length += field_len - arg->offset;
	} else if (arg->cut_length > field_len - arg->offset) {
		arg->cut_length = field_len - arg->offset;
	}

	/* Read the paste. */
	void *paste_field = read_field(&operands);
	arg->paste_length = load_varint32(&paste_field);
	arg->paste = paste_field;

	/* Fill tail part */
	arg->tail_offset = arg->offset + arg->cut_length;
	arg->tail_length = field_len - arg->tail_offset;

	/* Check that the operands are fully read. */
	if (operands.size != 0)
		tnt_raise(IllegalParams, :"field splice format error");

	/* Record the new field length. */
	op->new_field_len = arg->offset + arg->paste_length + arg->tail_length;
}

static void
init_update_op_delete(struct update_cmd *cmd,
		      struct update_field *field, struct update_op *op)
{
	/*
	 * Either DELETE is the last op on a field or next op
	 * on this field is SET.
	 */
	if (op + 1 < cmd->op_end) {
		struct update_op *next_op = op + 1;
		if (next_op->field_no == op->field_no &&
		    next_op->opcode != UPDATE_OP_SET &&
		    next_op->opcode != UPDATE_OP_DELETE) {
			tnt_raise(ClientError, :ER_NO_SUCH_FIELD,
				  op->field_no);
		}
	}
	/* Skip all ops on this field, including this one. */
	field->first = op + 1;
	op->new_field_len = 0;
}

static void
init_update_op_insert(struct update_cmd *cmd __attribute__((unused)),
		      struct update_field *field __attribute__((unused)),
		      struct update_op *op)
{
	op->new_field_len = op->arg.set.length;
}

static void
init_update_op_none(struct update_cmd *cmd __attribute__((unused)),
		    struct update_field *field, struct update_op *op)
{
	op->new_field_len = op->arg.set.length = field->new_len;
}

static void
init_update_op_error(struct update_cmd *cmd __attribute__((unused)),
		     struct update_field *field __attribute__((unused)),
		     struct update_op *op __attribute__((unused)))
{
	tnt_raise(ClientError, :ER_UNKNOWN_UPDATE_OP);
}

static struct update_op_meta update_op_meta[UPDATE_OP_MAX + 1] = {
	{ init_update_op_set, (do_op_func) do_update_op_set, true },
	{ init_update_op_arith, (do_op_func) do_update_op_add, true },
	{ init_update_op_arith, (do_op_func) do_update_op_and, true },
	{ init_update_op_arith, (do_op_func) do_update_op_xor, true },
	{ init_update_op_arith, (do_op_func) do_update_op_or, true },
	{ init_update_op_splice, (do_op_func) do_update_op_splice, false },
	{ init_update_op_delete, (do_op_func) NULL, true },
	{ init_update_op_insert, (do_op_func) do_update_op_insert, true },
	{ init_update_op_none, (do_op_func) do_update_op_none, false },
	{ init_update_op_error, (do_op_func) NULL, true }
};

/**
 * Initial parse of update command. Unpack and record
 * update operations. Do not do too much, since the subject
 * tuple may not exist.
 */
static struct update_cmd *
parse_update_cmd(struct tbuf *data)
{
	struct update_cmd *cmd = palloc(fiber->gc_pool,
					sizeof(struct update_cmd));

	read_key(data, &cmd->key, &cmd->key_part_count);
	/* number of operations */
	u32 op_cnt = read_u32(data);
	if (op_cnt > BOX_UPDATE_OP_CNT_MAX)
		tnt_raise(IllegalParams, :"too many operations for update");
	if (op_cnt == 0)
		tnt_raise(IllegalParams, :"no operations for update");
	/*
	 * Read update operations. Allocate an extra dummy op to
	 * optionally "apply" to the first field.
	 */
	struct update_op *op = palloc(fiber->gc_pool, (op_cnt + 1) *
				      sizeof(struct update_op));
	cmd->op = ++op; /* Skip the extra op for now. */
	cmd->op_end = cmd->op + op_cnt;
	for (; op < cmd->op_end; op++) {
		/* read operation */
		op->field_no = read_u32(data);
		op->opcode = read_u8(data);
		if (op->opcode > UPDATE_OP_MAX)
			op->opcode = UPDATE_OP_MAX;
		op->meta = &update_op_meta[op->opcode];
		op->arg.set.value = read_field(data);
		op->arg.set.length = load_varint32(&op->arg.set.value);
	}
	/* Check the remainder length, the request must be fully read. */
	if (data->size != 0)
		tnt_raise(IllegalParams, :"can't unpack request");

	return cmd;
}

static void
update_field_init(struct update_field *field, struct update_op *op,
		  void **old_data, int old_field_count)
{
	field->first = op;

	if (op->field_no >= old_field_count ||
	    op->opcode == UPDATE_OP_INSERT) {
		/* Insert operation always creates a new field. */
		field->new_len = 0;
		field->old = ""; /* Beyond old fields. */
		/*
		 * Old tuple must have at least one field and we
		 * always have an op on the first field.
		 */
		assert(op->field_no > 0 || op->opcode == UPDATE_OP_INSERT);
		return;
	}
	/*
	 * Find out the new field length and
	 * shift the data pointer.
	 */
	field->new_len = load_varint32(old_data);
	field->old = *old_data;
	*old_data += field->new_len;
}

/**
 * Skip fields unaffected by UPDATE.
 * @return   length of skipped data
 */
static void
update_field_skip_fields(struct update_field *field, i32 skip_count,
			 void **data)
{
	if (skip_count < 0) {
		/* Happens when there are fields added by SET. */
		skip_count = 0;
	}

	field->tail_field_count = skip_count;

	field->tail = *data;
	while (skip_count-- > 0) {
		u32 len = load_varint32(data);
		*data += len;
	}

	field->tail_len = *data - field->tail;
}


/**
 * We found a tuple to do the update on. Prepare and optimize
 * the operations.
 */
static void
init_update_operations(struct update_cmd *cmd, struct tuple *old_tuple)
{
	/*
	 * 1. Sort operations by field number and order within the
	 * request.
	 */
	qsort(cmd->op, cmd->op_end - cmd->op, sizeof(struct update_op),
	      update_op_cmp);

	/*
	 * 2. Take care of the old tuple head.
	 */
	if (cmd->op->field_no != 0) {
		/*
		 * We need to copy part of the old tuple to the
		 * new one.
		 * Prepend a no-op which copies the first field
		 * intact. We may also make use of its tail_len
		 * if next op field_no > 1.
		 */
		cmd->op--;
		cmd->op->opcode = UPDATE_OP_NONE;
		cmd->op->meta = &update_op_meta[UPDATE_OP_NONE];
		cmd->op->field_no = 0;
	}

	/*
	 * 3. Initialize and optimize the operations.
	 */
	cmd->new_tuple_len = 0;
	assert(cmd->op < cmd->op_end); /* Ensured by parse_update_cmd(). */
	cmd->field = palloc(fiber->gc_pool, (cmd->op_end - cmd->op) *
			    sizeof(struct update_field));
	struct update_op *op = cmd->op;
	struct update_field *field = cmd->field;
	void *old_data = old_tuple->data;
	int old_field_count = old_tuple->field_count;

	update_field_init(field, op, &old_data, old_field_count);
	do {
		struct update_op *prev_op = op - 1;
		struct update_op *next_op = op + 1;

		/*
		 * Various checks for added fields:
		 */
		if (op->field_no >= old_field_count) {
			/*
			 * We can not do anything with a new field unless a
			 * previous field exists.
			 */
			int prev_field_no = MAX(old_field_count,
						prev_op->field_no + 1);
			if (op->field_no > prev_field_no)
				tnt_raise(ClientError, :ER_NO_SUCH_FIELD,
					  op->field_no);
			/*
			 * We can not do any op except SET or INSERT
			 * on a field which does not exist.
			 */
			if (prev_op->field_no != op->field_no &&
			    (op->opcode != UPDATE_OP_SET &&
			     op->opcode != UPDATE_OP_INSERT))
				tnt_raise(ClientError, :ER_NO_SUCH_FIELD,
					  op->field_no);
		}
		op->meta->init_op(cmd, field, op);
		field->new_len = op->new_field_len;
		/*
		 * Find out how many fields to copy to the
		 * new tuple intact once this op is done.
		 */
		int skip_count;
		if (next_op >= cmd->op_end) {
			/* This is the last op in the request. */
			skip_count = old_field_count - op->field_no - 1;
		} else if (op->field_no < next_op->field_no ||
			   op->opcode == UPDATE_OP_INSERT) {
			/*
			 * This is the last op on this field. UPDATE_OP_INSERT
			 * creates a new field, so it falls
			 * into this category. Find out length of
			 * the gap between the op->field_no and
			 * next and copy the gap.
			 */
			skip_count = MIN(next_op->field_no, old_field_count) -
				     op->field_no - 1;
		} else {
			/* Continue, we have more operations on this field */
			continue;
		}
		if (op->opcode == UPDATE_OP_INSERT) {
			/*
			 * We're adding a new field, take this
			 * into account.
			 */
			skip_count++;
		}
		/* Jumping over a gap. */
		update_field_skip_fields(field, skip_count, &old_data);

		field->end = next_op;
		if (field->first < field->end) {
			/* Field is not deleted. */
			cmd->new_tuple_len += varint32_sizeof(field->new_len);
			cmd->new_tuple_len += field->new_len;
		}
		cmd->new_tuple_len += field->tail_len;

		/* Move to the next field. */
		field++;
		if (next_op < cmd->op_end) {
			update_field_init(field, next_op,  &old_data,
					  old_field_count);
		}

	} while (++op < cmd->op_end);

	cmd->field_end = field;

	if (cmd->new_tuple_len == 0)
		tnt_raise(ClientError, :ER_TUPLE_IS_EMPTY);
}

static void
do_update_ops(struct update_cmd *cmd, struct tuple *new_tuple)
{
	void *new_data = new_tuple->data;
	void *new_data_end = new_data + new_tuple->bsize;
	struct update_field *field;
	new_tuple->field_count = 0;

	for (field = cmd->field; field < cmd->field_end; field++) {
		if (field->first < field->end) { /* -> field is not deleted. */
			new_data = save_varint32(new_data, field->new_len);
			new_tuple->field_count++;
		}
		void *new_field = new_data;
		void *old_field = field->old;

		struct update_op *op;
		for (op = field->first; op < field->end; op++) {
			/*
			 * Pre-allocate a temporary buffer when the
			 * subject operation requires it, i.e.:
			 * - op overwrites data while reading it thus
			 *   can't work with in == out (SPLICE)
			 * - op result doesn't fit into the new tuple
			 *   (can happen when a big SET is then
			 *   shrunk by a SPLICE).
			 */
			if ((old_field == new_field &&
			     !op->meta->works_in_place) ||
			    /*
			     * Sic: this predicate must function even if
			     * new_field != new_data.
			     */
			    new_data + op->new_field_len > new_data_end) {
				/*
				 * Since we don't know which of the two
				 * conditions above got us here, simply
				 * palloc a *new* buffer of sufficient
				 * size.
				 */
				new_field = palloc(fiber->gc_pool,
						   op->new_field_len);
			}
			op->meta->do_op(&op->arg, old_field, new_field);
			/* Next op uses previous op output as its input. */
			old_field = new_field;
		}
		/*
		 * Make sure op results end up in the tuple, copy
		 * tail_len from the old tuple.
		*/
		if (new_field != new_data)
			memcpy(new_data, new_field, field->new_len);
		new_data += field->new_len;
		if (field->tail_field_count) {
			memcpy(new_data, field->tail, field->tail_len);
			new_data += field->tail_len;
			new_tuple->field_count += field->tail_field_count;
		}
	}
}

@implementation Update
- (void) execute: (struct txn *) txn :(Port *) port
{
	txn_add_redo(txn, type, data);
	struct space *sp = read_space(data);
	u32 flags = read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
	/* Parse UPDATE request. */
	struct update_cmd *cmd = parse_update_cmd(data);

	/* Try to find the tuple. */
	struct tuple *old_tuple =
		[sp->index[0] findByKey :cmd->key :cmd->key_part_count];
	if (old_tuple != NULL) {
		init_update_operations(cmd, old_tuple);
		/* allocate new tuple */
		txn->new_tuple = tuple_alloc(cmd->new_tuple_len);
		do_update_ops(cmd, txn->new_tuple);
		space_validate(sp, old_tuple, txn->new_tuple);
	}
	txn_add_undo(txn, sp, old_tuple, txn->new_tuple);

	port_send_tuple(flags, port, txn->new_tuple);
}
@end

/** }}} */

@interface Select: Request
- (void) execute: (struct txn *) txn :(Port *) port;
@end

@implementation Select
- (void) execute: (struct txn *) txn :(Port *) port
{
	(void) txn; /* Not used. */
	struct space *sp = read_space(data);
	u32 index_no = read_u32(data);
	Index *index = index_find(sp, index_no);
	u32 offset = read_u32(data);
	u32 limit = read_u32(data);
	u32 count = read_u32(data);
	if (count == 0)
		tnt_raise(IllegalParams, :"tuple count must be positive");

	uint32_t *found = palloc(fiber->gc_pool, sizeof(*found));
	*found = 0;
	[port addU32: found];

	ERROR_INJECT_EXCEPTION(ERRINJ_TESTING);

	for (u32 i = 0; i < count; i++) {

		/* End the loop if reached the limit. */
		if (limit == *found)
			return;

		/* read key */
		u32 key_part_count;
		void *key;
		read_key(data, &key, &key_part_count);

		struct iterator *it = index->position;
		[index initIteratorByKey: it :ITER_FORWARD :key :key_part_count];

		struct tuple *tuple;
		while ((tuple = it->next_equal(it)) != NULL) {
			if (tuple->flags & GHOST)
				continue;

			if (offset > 0) {
				offset--;
				continue;
			}

			[port addTuple: tuple];

			if (limit == ++(*found))
				break;
		}
	}
	if (data->size != 0)
		tnt_raise(IllegalParams, :"can't unpack request");
}
@end

@interface Delete: Request
- (void) execute: (struct txn *) txn :(Port *) port;
@end

@implementation Delete
- (void) execute: (struct txn *) txn :(Port *) port
{
	txn_add_redo(txn, type, data);
	u32 flags = 0;
	struct space *sp = read_space(data);
	if (type == DELETE)
		flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
	/* read key */
	u32 key_part_count;
	void *key;
	read_key(data, &key, &key_part_count);
	/* try to find tuple in primary index */
	struct tuple *old_tuple = [sp->index[0] findByKey :key :key_part_count];

	txn_add_undo(txn, sp, old_tuple, NULL);

	port_send_tuple(flags, port, old_tuple);
}
@end

@implementation Request
+ (Request *) alloc
{
	size_t sz = class_getInstanceSize(self);
	id new = palloc(fiber->gc_pool, sz);
	object_setClass(new, self);
	return new;
}

+ (Request *) build: (u32) type_arg
{
	Request *new = nil;
	switch (type_arg) {
	case REPLACE:
		new = [Replace alloc]; break;
	case SELECT:
		new = [Select alloc]; break;
	case UPDATE:
		new = [Update alloc]; break;
	case DELETE_1_3:
	case DELETE:
		new = [Delete alloc]; break;
	case CALL:
		new = [Call alloc]; break;
	default:
		say_error("Unsupported request = %" PRIi32 "", type_arg);
		tnt_raise(IllegalParams, :"unsupported command code, "
			  "check the error log");
		break;
	}
	new->type = type_arg;
	return new;
}

- (id) init: (struct tbuf *) data_arg
{
	assert(type);
	self = [super init];
	if (self == nil)
		return self;

	data = data_arg;
	return self;
}

- (void) execute: (struct txn *) txn :(Port *) port
{
	(void) txn;
	(void) port;
	[self subclassResponsibility: _cmd];
}
@end

/**
 * vim: foldmethod=marker
 */
