/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <mod/box/box.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <arpa/inet.h>

#include <cfg/warning.h>
#include <errcode.h>
#include <fiber.h>
#include <log_io.h>
#include <pickle.h>
#include <say.h>
#include <stat.h>
#include <tarantool.h>
#include <tbuf.h>
#include <util.h>
#include <errinj.h>

#include <cfg/tarantool_box_cfg.h>
#include <mod/box/tuple.h>
#include "memcached.h"
#include "box_lua.h"

extern pid_t logger_pid;

static void box_process_ro(u32 op, struct tbuf *request_data);
static void box_process_rw(u32 op, struct tbuf *request_data);

const char *mod_name = "Box";

iproto_callback rw_callback = box_process_ro;
static char status[64] = "unknown";

static int stat_base;
STRS(messages, MESSAGES);
STRS(update_op_codes, UPDATE_OP_CODES);

/*
  For tuples of size below this threshold, when sending a tuple
  to the client, make a deep copy of the tuple for the duration
  of sending rather than increment a reference counter.
  This is necessary to avoid excessive page splits when taking
  a snapshot: many small tuples can be accessed by clients
  immediately after the snapshot process has forked off,
  thus incrementing tuple ref count, and causing the OS to
  create a copy of the memory page for the forked
  child.
*/

const int BOX_REF_THRESHOLD = 8196;

struct space *space = NULL;

/* Secondary indexes are built in bulk after all data is
   recovered. This flag indicates that the indexes are
   already built and ready for use. */
static bool secondary_indexes_enabled = false;

struct box_snap_row {
	u32 space;
	u32 tuple_size;
	u32 data_size;
	u8 data[];
} __attribute__((packed));


static inline int
index_count(struct space *sp)
{
	if (!secondary_indexes_enabled) {
		/* If the secondary indexes are not enabled yet
		   then we can use only the primary index. So
		   return 1 if there is at least one index (which
		   must be primary) and return 0 otherwise. */
		return sp->key_count > 0;
	} else {
		/* Return the actual number of indexes. */
		return sp->key_count;
	}
}

static inline struct box_snap_row *
box_snap_row(const struct tbuf *t)
{
	return (struct box_snap_row *)t->data;
}

static void
lock_tuple(struct box_txn *txn, struct box_tuple *tuple)
{
	if (tuple->flags & WAL_WAIT)
		tnt_raise(ClientError, :ER_TUPLE_IS_RO);

	say_debug("lock_tuple(%p)", tuple);
	txn->lock_tuple = tuple;
	tuple->flags |= WAL_WAIT;
}

static void
unlock_tuples(struct box_txn *txn)
{
	if (txn->lock_tuple) {
		txn->lock_tuple->flags &= ~WAL_WAIT;
		txn->lock_tuple = NULL;
	}
}

void
tuple_txn_ref(struct box_txn *txn, struct box_tuple *tuple)
{
	say_debug("tuple_txn_ref(%p)", tuple);
	tbuf_append(txn->ref_tuples, &tuple, sizeof(struct box_tuple *));
	tuple_ref(tuple, +1);
}

static void
validate_indexes(struct box_txn *txn)
{
	struct space *sp = &space[txn->n];
	int n = index_count(sp);

	/* Only secondary indexes are validated here. So check to see
	   if there are any.*/
	if (n <= 1) {
		return;
	}

	/* Check to see if the tuple has a sufficient number of fields. */
	if (txn->tuple->cardinality < sp->field_count)
		tnt_raise(IllegalParams, :"tuple must have all indexed fields");

	/* Sweep through the tuple and check the field sizes. */
	u8 *data = txn->tuple->data;
	for (int f = 0; f < sp->field_count; ++f) {
		/* Get the size of the current field and advance. */
		u32 len = load_varint32((void **) &data);
		data += len;

		/* Check fixed size fields (NUM and NUM64) and skip undefined
		   size fields (STRING and UNKNOWN). */
		if (sp->field_types[f] == NUM) {
			if (len != sizeof(u32))
				tnt_raise(IllegalParams, :"field must be NUM");
		} else if (sp->field_types[f] == NUM64) {
			if (len != sizeof(u64))
				tnt_raise(IllegalParams, :"field must be NUM64");
		}
	}

	/* Check key uniqueness */
	for (int i = 1; i < n; ++i) {
		Index *index = space[txn->n].index[i];
		if (index->key_def->is_unique) {
			struct box_tuple *tuple = [index findByTuple: txn->tuple];
			if (tuple != NULL && tuple != txn->old_tuple)
				tnt_raise(ClientError, :ER_INDEX_VIOLATION);
		}
	}
}

static void
read_key(struct tbuf *data, void **key_ptr, u32 *key_cardinality_ptr)
{
	void *key = NULL;
	u32 key_cardinality = read_u32(data);
	if (key_cardinality) {
		key = read_field(data);
		/* advance remaining fields of a key */
		for (int i = 1; i < key_cardinality; i++)
			read_field(data);
	}

	*key_ptr = key;
	*key_cardinality_ptr = key_cardinality;
}

static void __attribute__((noinline))
prepare_replace(struct box_txn *txn, size_t cardinality, struct tbuf *data)
{
	assert(data != NULL);
	if (cardinality == 0)
		tnt_raise(IllegalParams, :"tuple cardinality is 0");

	if (data->size == 0 || data->size != valid_tuple(data, cardinality))
		tnt_raise(IllegalParams, :"incorrect tuple length");

	txn->tuple = tuple_alloc(data->size);
	tuple_txn_ref(txn, txn->tuple);
	txn->tuple->cardinality = cardinality;
	memcpy(txn->tuple->data, data->data, data->size);

	txn->old_tuple = [txn->index findByTuple: txn->tuple];

	if (txn->old_tuple != NULL)
		tuple_txn_ref(txn, txn->old_tuple);

	if (txn->flags & BOX_ADD && txn->old_tuple != NULL)
		tnt_raise(ClientError, :ER_TUPLE_FOUND);

	if (txn->flags & BOX_REPLACE && txn->old_tuple == NULL)
		tnt_raise(ClientError, :ER_TUPLE_NOT_FOUND);

	validate_indexes(txn);

	if (txn->old_tuple != NULL) {
#ifndef NDEBUG
		void *ka, *kb;
		ka = tuple_field(txn->tuple, txn->index->key_def->parts[0].fieldno);
		kb = tuple_field(txn->old_tuple, txn->index->key_def->parts[0].fieldno);
		int kal, kab;
		kal = load_varint32(&ka);
		kab = load_varint32(&kb);
		assert(kal == kab && memcmp(ka, kb, kal) == 0);
#endif
		lock_tuple(txn, txn->old_tuple);
	} else {
		lock_tuple(txn, txn->tuple);
		/*
		 * Mark the tuple as ghost before attempting an
		 * index replace: if it fails, txn_rollback() will
		 * look at the flag and remove the tuple.
		 */
		txn->tuple->flags |= GHOST;
		/*
		 * If the tuple doesn't exist, insert a GHOST
		 * tuple in all indices in order to avoid a race
		 * condition when another REPLACE comes along:
		 * a concurrent REPLACE, UPDATE, or DELETE, returns
		 * an error when meets a ghost tuple.
		 *
		 * Tuple reference counter will be incremented in
		 * txn_commit().
		 */
		int n = index_count(&space[txn->n]);
		for (int i = 0; i < n; i++) {
			Index *index = space[txn->n].index[i];
			[index replace: NULL :txn->tuple];
		}
	}

	txn->out->dup_u32(1); /* Affected tuples */

	if (txn->flags & BOX_RETURN_TUPLE)
		txn->out->add_tuple(txn->tuple);
}

static void
commit_replace(struct box_txn *txn)
{
	if (txn->old_tuple != NULL) {
		int n = index_count(&space[txn->n]);
		for (int i = 0; i < n; i++) {
			Index *index = space[txn->n].index[i];
			[index replace: txn->old_tuple :txn->tuple];
		}

		tuple_ref(txn->old_tuple, -1);
	}

	if (txn->tuple != NULL) {
		txn->tuple->flags &= ~GHOST;
		tuple_ref(txn->tuple, +1);
	}
}

static void
rollback_replace(struct box_txn *txn)
{
	say_debug("rollback_replace: txn->tuple:%p", txn->tuple);

	if (txn->tuple && txn->tuple->flags & GHOST) {
		int n = index_count(&space[txn->n]);
		for (int i = 0; i < n; i++) {
			Index *index = space[txn->n].index[i];
			[index remove: txn->tuple];
		}
	}
}

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
 * read the source of do_update() to see how these
 * complications  are worked around.
 */

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
	/** Search key cardinality */
	u32 key_cardinality;
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

	read_key(data, &cmd->key, &cmd->key_cardinality);
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
init_update_operations(struct box_txn *txn, struct update_cmd *cmd)
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
	void *old_data = txn->old_tuple->data;
	int old_field_count = txn->old_tuple->cardinality;

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
do_update(struct box_txn *txn, struct update_cmd *cmd)
{
	void *new_data = txn->tuple->data;
	void *new_data_end = new_data + txn->tuple->bsize;
	struct update_field *field;
	txn->tuple->cardinality = 0;

	for (field = cmd->field; field < cmd->field_end; field++) {
		if (field->first < field->end) { /* -> field is not deleted. */
			new_data = save_varint32(new_data, field->new_len);
			txn->tuple->cardinality++;
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
			txn->tuple->cardinality += field->tail_field_count;
		}
	}
}

static void __attribute__((noinline))
prepare_update(struct box_txn *txn, struct tbuf *data)
{
	u32 tuples_affected = 1;

	/* Parse UPDATE request. */
	struct update_cmd *cmd = parse_update_cmd(data);

	/* Try to find the tuple. */
	txn->old_tuple = [txn->index find :cmd->key :cmd->key_cardinality];
	if (txn->old_tuple == NULL) {
		/* Not found. For simplicity, skip the logging. */
		txn->flags |= BOX_NOT_STORE;

		tuples_affected = 0;

		goto out;
	}

	init_update_operations(txn, cmd);
	/* allocate new tuple */
	txn->tuple = tuple_alloc(cmd->new_tuple_len);
	tuple_txn_ref(txn, txn->tuple);
	do_update(txn, cmd);
	lock_tuple(txn, txn->old_tuple);
	validate_indexes(txn);

out:
	txn->out->dup_u32(tuples_affected);

	if (txn->flags & BOX_RETURN_TUPLE && txn->tuple)
		txn->out->add_tuple(txn->tuple);
}

/** }}} */

static void __attribute__((noinline))
process_select(struct box_txn *txn, u32 limit, u32 offset, struct tbuf *data)
{
	struct box_tuple *tuple;
	uint32_t *found;
	u32 count = read_u32(data);
	if (count == 0)
		tnt_raise(IllegalParams, :"tuple count must be positive");

	found = palloc(fiber->gc_pool, sizeof(*found));
	txn->out->add_u32(found);
	*found = 0;

	for (u32 i = 0; i < count; i++) {

		Index *index = txn->index;
		/* End the loop if reached the limit. */
		if (limit == *found)
			return;

		/* read key */
		u32 key_cardinality;
		void *key;
		read_key(data, &key, &key_cardinality);

		struct iterator *it = index->position;
		[index initIterator: it :ITER_FORWARD :key :key_cardinality];

		while ((tuple = it->next_equal(it)) != NULL) {
			if (tuple->flags & GHOST)
				continue;

			if (offset > 0) {
				offset--;
				continue;
			}

			txn->out->add_tuple(tuple);

			if (limit == ++(*found))
				break;
		}
	}
	if (data->size != 0)
		tnt_raise(IllegalParams, :"can't unpack request");
}

static void __attribute__((noinline))
prepare_delete(struct box_txn *txn, struct tbuf *data)
{
	u32 tuples_affected = 0;

	/* read key */
	u32 key_cardinality;
	void *key;
	read_key(data, &key, &key_cardinality);
	/* try to find tuple in primary index */
	txn->old_tuple = [txn->index find :key :key_cardinality];

	if (txn->old_tuple == NULL) {
		/*
		 * There is no subject tuple we could write to WAL, which means,
		 * to do a write, we would have to allocate one. Too complicated,
		 * for now, just do no logging for DELETEs that do nothing.
		 */
		txn->flags |= BOX_NOT_STORE;
	} else {
		tuple_txn_ref(txn, txn->old_tuple);
		lock_tuple(txn, txn->old_tuple);

		tuples_affected = 1;
	}

	txn->out->dup_u32(tuples_affected);

	if (txn->old_tuple && (txn->flags & BOX_RETURN_TUPLE))
		txn->out->add_tuple(txn->old_tuple);
}

static void
commit_delete(struct box_txn *txn)
{
	if (txn->old_tuple == NULL)
		return;

	int n = index_count(&space[txn->n]);
	for (int i = 0; i < n; i++) {
		Index *index = space[txn->n].index[i];
		[index remove: txn->old_tuple];
	}

	tuple_ref(txn->old_tuple, -1);
}

static bool
op_is_select(u32 op)
{
	return op == SELECT || op == CALL;
}

static void
iov_add_u32(u32 *p_u32)
{
	iov_add(p_u32, sizeof(u32));
}

static void
iov_dup_u32(u32 u32)
{
	iov_dup(&u32, sizeof(u32));
}

static void
iov_add_tuple(struct box_tuple *tuple)
{
	size_t len = tuple_len(tuple);

	if (len > BOX_REF_THRESHOLD) {
		tuple_txn_ref(in_txn(), tuple);
		iov_add(&tuple->bsize, len);
	} else {
		iov_dup(&tuple->bsize, len);
	}
}

static struct box_out box_out_iproto = {
	iov_add_u32,
	iov_dup_u32,
	iov_add_tuple
};

static void box_quiet_add_u32(u32 *p_u32 __attribute__((unused))) {}
static void box_quiet_dup_u32(u32 u32 __attribute__((unused))) {}
static void box_quiet_add_tuple(struct box_tuple *tuple __attribute__((unused))) {}

struct box_out box_out_quiet = {
	box_quiet_add_u32,
	box_quiet_dup_u32,
	box_quiet_add_tuple
};

struct box_txn *
txn_begin()
{
	struct box_txn *txn = p0alloc(fiber->gc_pool, sizeof(*txn));
	txn->ref_tuples = tbuf_alloc(fiber->gc_pool);
	assert(fiber->mod_data.txn == NULL);
	fiber->mod_data.txn = txn;
	return txn;
}

void txn_assign_n(struct box_txn *txn, struct tbuf *data)
{
	txn->n = read_u32(data);

	if (txn->n < 0 || txn->n >= BOX_SPACE_MAX)
		tnt_raise(ClientError, :ER_NO_SUCH_SPACE, txn->n);

	txn->space = &space[txn->n];

	if (!txn->space->enabled)
		tnt_raise(ClientError, :ER_SPACE_DISABLED, txn->n);

	txn->index = txn->space->index[0];
}

/** Remember op code/request in the txn. */
static void
txn_set_op(struct box_txn *txn, u16 op, struct tbuf *data)
{
	txn->op = op;
	txn->req = (struct tbuf) {
		.data = data->data,
		.size = data->size,
		.capacity = data->size,
		.pool = NULL };
}

static void
txn_cleanup(struct box_txn *txn)
{
	struct box_tuple **tuple = txn->ref_tuples->data;
	int i = txn->ref_tuples->size / sizeof(struct box_txn *);

	while (i-- > 0) {
		say_debug("tuple_txn_unref(%p)", *tuple);
		tuple_ref(*tuple++, -1);
	}

	/* mark txn as clean */
	memset(txn, 0, sizeof(*txn));
}

void
txn_commit(struct box_txn *txn)
{
	assert(txn == in_txn());
	assert(txn->op);

	if (!op_is_select(txn->op)) {
		say_debug("box_commit(op:%s)", messages_strs[txn->op]);

		if (txn->flags & BOX_NOT_STORE)
			;
		else {
			fiber_peer_name(fiber); /* fill the cookie */

			i64 lsn = next_lsn(recovery_state, 0);
			int res = wal_write(recovery_state, wal_tag, txn->op,
					    fiber->cookie, lsn, &txn->req);
			confirm_lsn(recovery_state, lsn);
			if (res)
				tnt_raise(LoggedError, :ER_WAL_IO);
		}

		unlock_tuples(txn);

		if (txn->op == DELETE_1_3 || txn->op == DELETE)
			commit_delete(txn);
		else
			commit_replace(txn);
	}
	/*
	 * If anything above throws, we must be able to
	 * roll back. Thus clear mod_data.txn only when
	 * we know for sure the commit has succeeded.
	 */
	fiber->mod_data.txn = 0;

	if (txn->flags & BOX_GC_TXN)
		fiber_register_cleanup((fiber_cleanup_handler)txn_cleanup, txn);
	else
		txn_cleanup(txn);
}

void
txn_rollback(struct box_txn *txn)
{
	assert(txn == in_txn());
	fiber->mod_data.txn = 0;
	if (txn->op == 0)
		return;

	if (!op_is_select(txn->op)) {
		say_debug("txn_rollback(op:%s)", messages_strs[txn->op]);

		unlock_tuples(txn);

		if (txn->op == REPLACE)
			rollback_replace(txn);
	}

	txn_cleanup(txn);
}

static void
box_dispatch(struct box_txn *txn, struct tbuf *data)
{
	u32 cardinality;

	say_debug("box_dispatch(%i)", txn->op);

	switch (txn->op) {
	case REPLACE:
		txn_assign_n(txn, data);
		txn->flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
		cardinality = read_u32(data);
		if (space[txn->n].cardinality > 0
		    && space[txn->n].cardinality != cardinality)
			tnt_raise(IllegalParams, :"tuple cardinality must match space cardinality");
		prepare_replace(txn, cardinality, data);
		break;

	case DELETE:
	case DELETE_1_3:
		txn_assign_n(txn, data);
		if (txn->op == DELETE)
			txn->flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;

		prepare_delete(txn, data);
		break;

	case SELECT:
	{
		ERROR_INJECT_EXCEPTION(ERRINJ_TESTING);
		txn_assign_n(txn, data);
		u32 i = read_u32(data);
		u32 offset = read_u32(data);
		u32 limit = read_u32(data);

		if (i >= space[txn->n].key_count)
			tnt_raise(LoggedError, :ER_NO_SUCH_INDEX, i, txn->n);
		txn->index = space[txn->n].index[i];

		process_select(txn, limit, offset, data);
		break;
	}

	case UPDATE:
		txn_assign_n(txn, data);
		txn->flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
		prepare_update(txn, data);
		break;
	case CALL:
		txn->flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
		box_lua_call(txn, data);
		break;

	default:
		say_error("box_dispatch: unsupported command = %" PRIi32 "", txn->op);
		tnt_raise(IllegalParams, :"unsupported command code, check the error log");
	}
}

static int
box_xlog_sprint(struct tbuf *buf, const struct tbuf *t)
{
	struct row_v11 *row = row_v11(t);

	struct tbuf *b = palloc(fiber->gc_pool, sizeof(*b));
	b->data = row->data;
	b->size = row->len;
	u16 tag, op;
	u64 cookie;
	struct sockaddr_in *peer = (void *)&cookie;

	u32 n, key_len;
	void *key;
	u32 cardinality, field_no;
	u32 flags;
	u32 op_cnt;

	tbuf_printf(buf, "lsn:%" PRIi64 " ", row->lsn);

	say_debug("b->len:%" PRIu32, b->size);

	tag = read_u16(b);
	cookie = read_u64(b);
	op = read_u16(b);
	n = read_u32(b);

	tbuf_printf(buf, "tm:%.3f t:%" PRIu16 " %s:%d %s n:%i",
		    row->tm, tag, inet_ntoa(peer->sin_addr), ntohs(peer->sin_port),
		    messages_strs[op], n);

	switch (op) {
	case REPLACE:
		flags = read_u32(b);
		cardinality = read_u32(b);
		if (b->size != valid_tuple(b, cardinality))
			abort();
		tuple_print(buf, cardinality, b->data);
		break;

	case DELETE:
		flags = read_u32(b);
	case DELETE_1_3:
		key_len = read_u32(b);
		key = read_field(b);
		if (b->size != 0)
			abort();
		tuple_print(buf, key_len, key);
		break;

	case UPDATE:
		flags = read_u32(b);
		key_len = read_u32(b);
		key = read_field(b);
		op_cnt = read_u32(b);

		tbuf_printf(buf, "flags:%08X ", flags);
		tuple_print(buf, key_len, key);

		while (op_cnt-- > 0) {
			field_no = read_u32(b);
			u8 op = read_u8(b);
			void *arg = read_field(b);

			tbuf_printf(buf, " [field_no:%i op:", field_no);
			switch (op) {
			case 0:
				tbuf_printf(buf, "set ");
				break;
			case 1:
				tbuf_printf(buf, "add ");
				break;
			case 2:
				tbuf_printf(buf, "and ");
				break;
			case 3:
				tbuf_printf(buf, "xor ");
				break;
			case 4:
				tbuf_printf(buf, "or ");
				break;
			}
			tuple_print(buf, 1, arg);
			tbuf_printf(buf, "] ");
		}
		break;
	default:
		tbuf_printf(buf, "unknown wal op %" PRIi32, op);
	}
	return 0;
}


static int
snap_print(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
{
	struct tbuf *out = tbuf_alloc(t->pool);
	struct box_snap_row *row;
	struct row_v11 *raw_row = row_v11(t);

	struct tbuf *b = palloc(fiber->gc_pool, sizeof(*b));
	b->data = raw_row->data;
	b->size = raw_row->len;

	(void)read_u16(b); /* drop tag */
	(void)read_u64(b); /* drop cookie */

	row = box_snap_row(b);

	tuple_print(out, row->tuple_size, row->data);
	printf("n:%i %*s\n", row->space, (int) out->size, (char *)out->data);
	return 0;
}

static int
xlog_print(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
{
	struct tbuf *out = tbuf_alloc(t->pool);
	int res = box_xlog_sprint(out, t);
	if (res >= 0)
		printf("%*s\n", (int)out->size, (char *)out->data);
	return res;
}

/** Free a key definition. */
static void
key_free(struct key_def *key_def)
{
	free(key_def->parts);
	free(key_def->cmp_order);
}

static void
space_free(void)
{
	int i;
	for (i = 0 ; i < BOX_SPACE_MAX ; i++) {
		if (!space[i].enabled)
			continue;

		int j;
		for (j = 0 ; j < space[i].key_count; j++) {
			Index *index = space[i].index[j];
			[index free];
			key_free(&space[i].key_defs[j]);
		}

		free(space[i].key_defs);
		free(space[i].field_types);
	}
}

static void
key_init(struct key_def *def, struct tarantool_cfg_space_index *cfg_index)
{
	def->max_fieldno = 0;
	def->part_count = 0;

	/* Calculate key cardinality and maximal field number. */
	for (int k = 0; cfg_index->key_field[k] != NULL; ++k) {
		typeof(cfg_index->key_field[k]) cfg_key = cfg_index->key_field[k];

		if (cfg_key->fieldno == -1) {
			/* last filled key reached */
			break;
		}

		def->max_fieldno = MAX(def->max_fieldno, cfg_key->fieldno);
		def->part_count++;
	}

	/* init def array */
	def->parts = malloc(sizeof(struct key_part) * def->part_count);
	if (def->parts == NULL) {
		panic("can't allocate def parts array for index");
	}

	/* init compare order array */
	def->max_fieldno++;
	def->cmp_order = malloc(def->max_fieldno * sizeof(u32));
	if (def->cmp_order == NULL) {
		panic("can't allocate def cmp_order array for index");
	}
	memset(def->cmp_order, -1, def->max_fieldno * sizeof(u32));

	/* fill fields and compare order */
	for (int k = 0; cfg_index->key_field[k] != NULL; ++k) {
		typeof(cfg_index->key_field[k]) cfg_key = cfg_index->key_field[k];

		if (cfg_key->fieldno == -1) {
			/* last filled key reached */
			break;
		}

		/* fill keys */
		def->parts[k].fieldno = cfg_key->fieldno;
		def->parts[k].type = STR2ENUM(field_data_type, cfg_key->type);
		/* fill compare order */
		def->cmp_order[cfg_key->fieldno] = k;
	}
	def->is_unique = cfg_index->unique;
}

/**
 * Extract all available field info from keys
 *
 * @param space		space to extract field info for
 * @param key_count	the number of keys
 * @param key_defs	key description array
 */
static void
space_init_field_types(struct space *space)
{
	int i, field_count;
	int key_count = space->key_count;
	struct key_def *key_defs = space->key_defs;

	/* find max max field no */
	field_count = 0;
	for (i = 0; i < key_count; i++) {
		field_count = MAX(field_count, key_defs[i].max_fieldno);
	}

	/* alloc & init field type info */
	space->field_count = field_count;
	space->field_types = malloc(field_count * sizeof(enum field_data_type));
	for (i = 0; i < field_count; i++) {
		space->field_types[i] = UNKNOWN;
	}

	/* extract field type info */
	for (i = 0; i < key_count; i++) {
		struct key_def *def = &key_defs[i];
		for (int pi = 0; pi < def->part_count; pi++) {
			struct key_part *part = &def->parts[pi];
			assert(part->fieldno < field_count);
			space->field_types[part->fieldno] = part->type;
		}
	}

#ifndef NDEBUG
	/* validate field type info */
	for (i = 0; i < key_count; i++) {
		struct key_def *def = &key_defs[i];
		for (int pi = 0; pi < def->part_count; pi++) {
			struct key_part *part = &def->parts[pi];
			assert(space->field_types[part->fieldno] == part->type);
		}
	}
#endif
}

static void
space_config(void)
{
	/* exit if no spaces are configured */
	if (cfg.space == NULL) {
		return;
	}

	/* fill box spaces */
	for (int i = 0; cfg.space[i] != NULL; ++i) {
		tarantool_cfg_space *cfg_space = cfg.space[i];

		if (!CNF_STRUCT_DEFINED(cfg_space) || !cfg_space->enabled)
			continue;

		assert(cfg.memcached_port == 0 || i != cfg.memcached_space);

		space[i].enabled = true;
		space[i].cardinality = cfg_space->cardinality;

		/*
		 * Collect key/field info. We need aggregate
		 * information on all keys before we can create
		 * indexes.
		 */
		space[i].key_count = 0;
		for (int j = 0; cfg_space->index[j] != NULL; ++j) {
			++space[i].key_count;
		}

		space[i].key_defs = malloc(space[i].key_count *
					    sizeof(struct key_def));
		if (space[i].key_defs == NULL) {
			panic("can't allocate key def array");
		}
		for (int j = 0; cfg_space->index[j] != NULL; ++j) {
			typeof(cfg_space->index[j]) cfg_index = cfg_space->index[j];
			key_init(&space[i].key_defs[j], cfg_index);
		}
		space_init_field_types(&space[i]);

		/* fill space indexes */
		for (int j = 0; cfg_space->index[j] != NULL; ++j) {
			typeof(cfg_space->index[j]) cfg_index = cfg_space->index[j];
			enum index_type type = STR2ENUM(index_type, cfg_index->type);
			struct key_def *key_def = &space[i].key_defs[j];
			Index *index = [Index alloc: type :key_def :&space[i]];
			[index init: key_def :&space[i]];
			space[i].index[j] = index;
		}
		say_info("space %i successfully configured", i);
	}
}

static void
space_init(void)
{
	/* Allocate and initialize space memory. */
	space = palloc(eter_pool, sizeof(struct space) * BOX_SPACE_MAX);
	memset(space, 0, sizeof(struct space) * BOX_SPACE_MAX);

	/* configure regular spaces */
	space_config();

	/* configure memcached space */
	memcached_space_init();
}

static void
build_indexes(void)
{
	for (u32 n = 0; n < BOX_SPACE_MAX; ++n) {
		if (space[n].enabled == false)
			continue;
		if (space[n].key_count <= 1)
			continue; /* no secondary keys */

		say_info("Building secondary keys in space %" PRIu32 "...", n);

		Index *pk = space[n].index[0];
		for (int i = 1; i < space[n].key_count; i++) {
			Index *index = space[n].index[i];
			[index build: pk];
		}

		say_info("Space %"PRIu32": done", n);
	}
}

static void
box_process_rw(u32 op, struct tbuf *request_data)
{
	ev_tstamp start = ev_now(), stop;

	stat_collect(stat_base, op, 1);

	struct box_txn *txn = in_txn();
	if (txn == NULL) {
		txn = txn_begin();
		txn->flags |= BOX_GC_TXN;
		txn->out = &box_out_iproto;
	}

	@try {
		txn_set_op(txn, op, request_data);
		box_dispatch(txn, request_data);
		txn_commit(txn);
	}
	@catch (id e) {
		txn_rollback(txn);
		@throw;
	}
	@finally {
		stop = ev_now();
		if (stop - start > cfg.too_long_threshold)
			say_warn("too long %s: %.3f sec", messages_strs[op], stop - start);
	}
}

static void
box_process_ro(u32 op, struct tbuf *request_data)
{
	if (!op_is_select(op)) {
		struct box_txn *txn = in_txn();
		if (txn != NULL)
			txn_rollback(txn);
		tnt_raise(LoggedError, :ER_NONMASTER);
	}

	return box_process_rw(op, request_data);
}

static struct tbuf *
convert_snap_row_to_wal(struct tbuf *t)
{
	struct tbuf *r = tbuf_alloc(fiber->gc_pool);
	struct box_snap_row *row = box_snap_row(t);
	u16 op = REPLACE;
	u32 flags = 0;

	tbuf_append(r, &op, sizeof(op));
	tbuf_append(r, &row->space, sizeof(row->space));
	tbuf_append(r, &flags, sizeof(flags));
	tbuf_append(r, &row->tuple_size, sizeof(row->tuple_size));
	tbuf_append(r, row->data, row->data_size);

	return r;
}

static int
recover_row(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
{
	/* drop wal header */
	if (tbuf_peek(t, sizeof(struct row_v11)) == NULL)
		return -1;

	u16 tag = read_u16(t);
	read_u64(t); /* drop cookie */
	if (tag == snap_tag)
		t = convert_snap_row_to_wal(t);
	else if (tag != wal_tag) {
		say_error("unknown row tag: %i", (int)tag);
		return -1;
	}

	u16 op = read_u16(t);

	struct box_txn *txn = txn_begin();
	txn->flags |= BOX_NOT_STORE;
	txn->out = &box_out_quiet;

	@try {
		box_process_rw(op, t);
	}
	@catch (id e) {
		return -1;
	}

	return 0;
}

static void
title(const char *fmt, ...)
{
	va_list ap;
	char buf[128], *bufptr = buf, *bufend = buf + sizeof(buf);

	va_start(ap, fmt);
	bufptr += vsnprintf(bufptr, bufend - bufptr, fmt, ap);
	va_end(ap);

	int ports[] = { cfg.primary_port, cfg.secondary_port,
			cfg.memcached_port, cfg.admin_port,
			cfg.replication_port };
	int *pptr = ports;
	char *names[] = { "pri", "sec", "memc", "adm", "rpl", NULL };
	char **nptr = names;

	for (; *nptr; nptr++, pptr++)
		if (*pptr)
			bufptr += snprintf(bufptr, bufend - bufptr,
					   " %s: %i", *nptr, *pptr);

	set_proc_title(buf);
}


static void
box_enter_master_or_replica_mode(struct tarantool_cfg *conf)
{
	if (conf->replication_source != NULL) {
		rw_callback = box_process_ro;

		recovery_wait_lsn(recovery_state, recovery_state->lsn);
		recovery_follow_remote(recovery_state, conf->replication_source);

		snprintf(status, sizeof(status), "replica/%s%s",
			 conf->replication_source, custom_proc_title);
		title("replica/%s%s", conf->replication_source, custom_proc_title);
	} else {
		rw_callback = box_process_rw;

		memcached_start_expire();

		snprintf(status, sizeof(status), "primary%s", custom_proc_title);
		title("primary%s", custom_proc_title);

		say_info("I am primary");
	}
}

static void
box_leave_local_standby_mode(void *data __attribute__((unused)))
{
	recover_finalize(recovery_state);

	box_enter_master_or_replica_mode(&cfg);
}

static i32
check_spaces(struct tarantool_cfg *conf)
{
	/* exit if no spaces are configured */
	if (conf->space == NULL) {
		return 0;
	}

	for (size_t i = 0; conf->space[i] != NULL; ++i) {
		typeof(conf->space[i]) space = conf->space[i];

		if (!CNF_STRUCT_DEFINED(space)) {
			/* space undefined, skip it */
			continue;
		}

		if (!space->enabled) {
			/* space disabled, skip it */
			continue;
		}

		/* check space bound */
		if (i >= BOX_SPACE_MAX) {
			/* maximum space is reached */
			out_warning(0, "(space = %zu) "
				    "too many spaces (%i maximum)", i, space);
			return -1;
		}

		if (conf->memcached_port && i == conf->memcached_space) {
			out_warning(0, "Space %i is already used as "
				    "memcached_space.", i);
			return -1;
		}

		/* at least one index in space must be defined
		 * */
		if (space->index == NULL) {
			out_warning(0, "(space = %zu) "
				    "at least one index must be defined", i);
			return -1;
		}

		int max_key_fieldno = -1;

		/* check spaces indexes */
		for (size_t j = 0; space->index[j] != NULL; ++j) {
			typeof(space->index[j]) index = space->index[j];
			u32 index_cardinality = 0;
			enum index_type index_type;

			/* check index bound */
			if (j >= BOX_INDEX_MAX) {
				/* maximum index in space reached */
				out_warning(0, "(space = %zu index = %zu) "
					    "too many indexed (%i maximum)", i, j, BOX_INDEX_MAX);
				return -1;
			}

			/* at least one key in index must be defined */
			if (index->key_field == NULL) {
				out_warning(0, "(space = %zu index = %zu) "
					    "at least one field must be defined", i, j);
				return -1;
			}

			/* check unique property */
			if (index->unique == -1) {
				/* unique property undefined */
				out_warning(0, "(space = %zu index = %zu) "
					    "unique property is undefined", i, j);
			}

			for (size_t k = 0; index->key_field[k] != NULL; ++k) {
				typeof(index->key_field[k]) key = index->key_field[k];

				if (key->fieldno == -1) {
					/* last key reached */
					break;
				}

				/* key must has valid type */
				if (STR2ENUM(field_data_type, key->type) == field_data_type_MAX) {
					out_warning(0, "(space = %zu index = %zu) "
						    "unknown field data type: `%s'", i, j, key->type);
					return -1;
				}

				if (max_key_fieldno < key->fieldno) {
					max_key_fieldno = key->fieldno;
				}

				++index_cardinality;
			}

			/* check index cardinality */
			if (index_cardinality == 0) {
				out_warning(0, "(space = %zu index = %zu) "
					    "at least one field must be defined", i, j);
				return -1;
			}

			index_type = STR2ENUM(index_type, index->type);

			/* check index type */
			if (index_type == index_type_MAX) {
				out_warning(0, "(space = %zu index = %zu) "
					    "unknown index type '%s'", i, j, index->type);
				return -1;
			}

			/* first space index must be unique and cardinality == 1 */
			if (j == 0) {
				if (index->unique == false) {
					out_warning(0, "(space = %zu) space first index must be unique", i);
					return -1;
				}
			}

			switch (index_type) {
			case HASH:
				/* check hash index */
				/* hash index must has single-field key */
				if (index_cardinality != 1) {
					out_warning(0, "(space = %zu index = %zu) "
					            "hash index must has a single-field key", i, j);
					return -1;
				}
				/* hash index must be unique */
				if (!index->unique) {
					out_warning(0, "(space = %zu index = %zu) "
					            "hash index must be unique", i, j);
					return -1;
				}
				break;
			case TREE:
				/* extra check for tree index not needed */
				break;
			default:
				assert(false);
			}
		}

		/* Check for index field type conflicts */
		if (max_key_fieldno >= 0) {
			char *types = alloca(max_key_fieldno + 1);
			memset(types, UNKNOWN, max_key_fieldno + 1);
			for (size_t j = 0; space->index[j] != NULL; ++j) {
				typeof(space->index[j]) index = space->index[j];
				for (size_t k = 0; index->key_field[k] != NULL; ++k) {
					typeof(index->key_field[k]) key = index->key_field[k];
					int f = key->fieldno;
					if (f == -1) {
						break;
					}
					enum field_data_type t = STR2ENUM(field_data_type, key->type);
					assert(t != field_data_type_MAX);
					if (types[f] != t) {
						if (types[f] == UNKNOWN) {
							types[f] = t;
						} else {
							out_warning(0, "(space = %zu fieldno = %zu) "
								    "index field type mismatch", i, f);
							return -1;
						}
					}
				}

			}
		}
	}

	return 0;
}

i32
mod_check_config(struct tarantool_cfg *conf)
{
	/* replication & hot standby modes can not work together */
	if (conf->replication_source != NULL && conf->local_hot_standby > 0) {
		out_warning(0, "replication and local hot standby modes "
			       "can't be enabled simultaneously");
		return -1;
	}

	/* check replication mode */
	if (conf->replication_source != NULL) {
		/* check replication port */
		char ip_addr[32];
		int port;

		if (sscanf(conf->replication_source, "%31[^:]:%i",
			   ip_addr, &port) != 2) {
			out_warning(0, "replication source IP address is not recognized");
			return -1;
		}
		if (port <= 0 || port >= USHRT_MAX) {
			out_warning(0, "invalid replication source port value: %i", port);
			return -1;
		}
	}

	/* check primary port */
	if (conf->primary_port != 0 &&
	    (conf->primary_port <= 0 || conf->primary_port >= USHRT_MAX)) {
		out_warning(0, "invalid primary port value: %i", conf->primary_port);
		return -1;
	}

	/* check secondary port */
	if (conf->secondary_port != 0 &&
	    (conf->secondary_port <= 0 || conf->secondary_port >= USHRT_MAX)) {
		out_warning(0, "invalid secondary port value: %i", conf->primary_port);
		return -1;
	}

	/* check if at least one space is defined */
	if (conf->space == NULL && conf->memcached_port == 0) {
		out_warning(0, "at least one space or memcached port must be defined");
		return -1;
	}

	/* check configured spaces */
	if (check_spaces(conf) != 0) {
		return -1;
	}

	/* check memcached configuration */
	if (memcached_check_config(conf) != 0) {
		return -1;
	}

	return 0;
}

i32
mod_reload_config(struct tarantool_cfg *old_conf, struct tarantool_cfg *new_conf)
{
	bool old_is_replica = old_conf->replication_source != NULL;
	bool new_is_replica = new_conf->replication_source != NULL;

	if (old_is_replica != new_is_replica ||
	    (old_is_replica &&
	     (strcmp(old_conf->replication_source, new_conf->replication_source) != 0))) {

		if (recovery_state->finalize != true) {
			out_warning(0, "Could not propagate %s before local recovery finished",
				    old_is_replica == true ? "slave to master" :
				    "master to slave");

			return -1;
		}

		if (!old_is_replica && new_is_replica)
			memcached_stop_expire();

		if (recovery_state->remote_recovery)
			recovery_stop_remote(recovery_state);

		box_enter_master_or_replica_mode(new_conf);
	}

	return 0;
}

void
mod_free(void)
{
	space_free();
	memcached_free();
}

void
mod_init(void)
{
	static iproto_callback ro_callback = box_process_ro;

	title("loading");
	atexit(mod_free);

	/* disable secondary indexes while loading */
	secondary_indexes_enabled = false;

	box_lua_init();

	/* initialization spaces */
	space_init();

	/* recovery initialization */
	recovery_init(cfg.snap_dir, cfg.wal_dir,
		      recover_row, cfg.rows_per_wal, cfg.wal_mode,
		      cfg.wal_fsync_delay,
		      init_storage ? RECOVER_READONLY : 0, NULL);

	recovery_state->snap_io_rate_limit = cfg.snap_io_rate_limit * 1024 * 1024;
	recovery_setup_panic(recovery_state, cfg.panic_on_snap_error, cfg.panic_on_wal_error);

	stat_base = stat_register(messages_strs, messages_MAX);

	/* memcached initialize */
	memcached_init();


	if (init_storage)
		return;

	recover(recovery_state, 0);
	stat_cleanup(stat_base, messages_MAX);

	title("building indexes");

	/* build secondary indexes */
	build_indexes();

	/* enable secondary indexes now */
	secondary_indexes_enabled = true;

	title("orphan");

	if (cfg.local_hot_standby) {
		say_info("starting local hot standby");
		recover_follow(recovery_state, cfg.wal_dir_rescan_delay);
		snprintf(status, sizeof(status), "hot_standby");
		title("hot_standby");
	}

	/* run primary server */
	if (cfg.primary_port != 0)
		fiber_server("primary", cfg.primary_port,
			     (fiber_server_callback) iproto_interact,
			     &rw_callback, box_leave_local_standby_mode);

	/* run secondary server */
	if (cfg.secondary_port != 0)
		fiber_server("secondary", cfg.secondary_port,
			     (fiber_server_callback) iproto_interact,
			     &ro_callback, NULL);

	/* run memcached server */
	if (cfg.memcached_port != 0)
		fiber_server("memcached", cfg.memcached_port,
			     memcached_handler, NULL, NULL);
}

int
mod_cat(const char *filename)
{
	return read_log(filename, xlog_print, snap_print, NULL);
}

static void
snapshot_write_tuple(struct log_io_iter *i, unsigned n, struct box_tuple *tuple)
{
	struct tbuf *row;
	struct box_snap_row header;

	if (tuple->flags & GHOST)	// do not save fictive rows
		return;

	header.space = n;
	header.tuple_size = tuple->cardinality;
	header.data_size = tuple->bsize;

	row = tbuf_alloc(fiber->gc_pool);
	tbuf_append(row, &header, sizeof(header));
	tbuf_append(row, tuple->data, tuple->bsize);

	snapshot_write_row(i, snap_tag, default_cookie, row);
}

void
mod_snapshot(struct log_io_iter *i)
{
	struct box_tuple *tuple;

	for (uint32_t n = 0; n < BOX_SPACE_MAX; ++n) {
		if (!space[n].enabled)
			continue;

		Index *pk = space[n].index[0];

		struct iterator *it = pk->position;
		[pk initIterator: it :ITER_FORWARD];
		while ((tuple = it->next(it))) {
			snapshot_write_tuple(i, n, tuple);
		}
	}
}

void
mod_info(struct tbuf *out)
{
	tbuf_printf(out, "  version: \"%s\"" CRLF, tarantool_version());
	tbuf_printf(out, "  uptime: %i" CRLF, (int)tarantool_uptime());
	tbuf_printf(out, "  pid: %i" CRLF, getpid());
	tbuf_printf(out, "  logger_pid: %i" CRLF, logger_pid);
	tbuf_printf(out, "  lsn: %" PRIi64 CRLF, recovery_state->confirmed_lsn);
	tbuf_printf(out, "  recovery_lag: %.3f" CRLF, recovery_state->recovery_lag);
	tbuf_printf(out, "  recovery_last_update: %.3f" CRLF,
		    recovery_state->recovery_last_update_tstamp);
	tbuf_printf(out, "  status: %s" CRLF, status);
}

/**
 * vim: foldmethod=marker
 */
