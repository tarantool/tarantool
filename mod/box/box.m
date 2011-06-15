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
#include <iproto.h>
#include <log_io.h>
#include <pickle.h>
#include <salloc.h>
#include <say.h>
#include <stat.h>
#include <tarantool.h>
#include <tbuf.h>
#include <util.h>

#include <cfg/tarantool_box_cfg.h>
#include <mod/box/index.h>

const char *mod_name = "Box";

bool box_updates_allowed = false;
static char *status = "unknown";

static int stat_base;
STRS(messages, MESSAGES);

const int MEMCACHED_NAMESPACE = 23;
static char *custom_proc_title;

static struct fiber *remote_recover;

/* hooks */
typedef int (*box_hook_t) (struct box_txn * txn);

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

struct namespace *namespace;
const int namespace_count = 256;

struct box_snap_row {
	u32 namespace;
	u32 tuple_size;
	u32 data_size;
	u8 data[];
} __attribute__((packed));

@implementation tnt_BoxException
- init:(const char *)p_file:(unsigned)p_line reason:(const char *)p_reason errcode:(u32)p_errcode
{
	[super init:p_file:p_line reason:p_reason];

	errcode = p_errcode;

	if (errcode != ERR_CODE_NODE_IS_RO)
		say_error("tnt_BoxException: %s/`%s' at %s:%i",
			  tnt_errcode_desc(errcode), reason, file, line);

	return self;
}

- init:(const char *)p_file:(unsigned)p_line errcode:(u32)p_errcode
{
	return [self init:p_file:p_line reason:"unknown" errcode:p_errcode];
}

@end

static inline struct box_snap_row *
box_snap_row(const struct tbuf *t)
{
	return (struct box_snap_row *)t->data;
}

static void tuple_add_iov(struct box_txn *txn, struct box_tuple *tuple);

box_hook_t *before_commit_update_hook;

static void
run_hooks(struct box_txn *txn, box_hook_t * hook)
{
	if (hook != NULL) {
		for (int i = 0; hook[i] != NULL; i++) {
			int result = (*hook[i]) (txn);
			if (result != ERR_CODE_OK)
				tnt_raise(tnt_BoxException,
					  reason:"hook returned error" errcode:result);
		}
	}
}

void *
next_field(void *f)
{
	u32 size = load_varint32(&f);
	return (u8 *)f + size;
}

void *
tuple_field(struct box_tuple *tuple, size_t i)
{
	void *field = tuple->data;

	if (i >= tuple->cardinality)
		return NULL;

	while (i-- > 0)
		field = next_field(field);

	return field;
}

static void
lock_tuple(struct box_txn *txn, struct box_tuple *tuple)
{
	if (tuple->flags & WAL_WAIT)
		tnt_raise(tnt_BoxException, reason:"tuple is locked" errcode:ERR_CODE_NODE_IS_RO);

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

static void
field_print(struct tbuf *buf, void *f)
{
	uint32_t size;

	size = load_varint32(&f);

	if (size == 2)
		tbuf_printf(buf, "%i:", *(u16 *)f);

	if (size == 4)
		tbuf_printf(buf, "%i:", *(u32 *)f);

	while (size-- > 0) {
		if (0x20 <= *(u8 *)f && *(u8 *)f < 0x7f)
			tbuf_printf(buf, "%c", *(u8 *)f++);
		else
			tbuf_printf(buf, "\\x%02X", *(u8 *)f++);
	}

}

static void
tuple_print(struct tbuf *buf, uint8_t cardinality, void *f)
{
	tbuf_printf(buf, "<");

	for (size_t i = 0; i < cardinality; i++, f = next_field(f)) {
		tbuf_printf(buf, "\"");
		field_print(buf, f);
		tbuf_printf(buf, "\"");

		if (likely(i + 1 < cardinality))
			tbuf_printf(buf, ", ");

	}

	tbuf_printf(buf, ">");
}

static struct box_tuple *
tuple_alloc(size_t size)
{
	struct box_tuple *tuple = salloc(sizeof(struct box_tuple) + size);

	if (tuple == NULL)
		tnt_raise(tnt_BoxException,
			  reason:"can't allocate tuple" errcode:ERR_CODE_MEMORY_ISSUE);

	tuple->flags = tuple->refs = 0;
	tuple->flags |= NEW;
	tuple->bsize = size;

	say_debug("tuple_alloc(%zu) = %p", size, tuple);
	return tuple;
}

static void
tuple_free(struct box_tuple *tuple)
{
	say_debug("tuple_free(%p)", tuple);
	assert(tuple->refs == 0);
	sfree(tuple);
}

static void
tuple_ref(struct box_tuple *tuple, int count)
{
	assert(tuple->refs + count >= 0);
	tuple->refs += count;

	if (tuple->refs > 0)
		tuple->flags &= ~NEW;

	if (tuple->refs == 0)
		tuple_free(tuple);
}

void
tuple_txn_ref(struct box_txn *txn, struct box_tuple *tuple)
{
	say_debug("tuple_txn_ref(%p)", tuple);
	tbuf_append(txn->ref_tuples, &tuple, sizeof(struct box_tuple *));
	tuple_ref(tuple, +1);
}

static void __attribute__((noinline))
prepare_replace(struct box_txn *txn, size_t cardinality, struct tbuf *data)
{
	assert(data != NULL);
	if (cardinality == 0)
		tnt_raise(tnt_BoxException,
			  reason:"cardinality can't be equal to 0" errcode:ERR_CODE_ILLEGAL_PARAMS);
	if (data->len == 0 || data->len != valid_tuple(data, cardinality))
		tnt_raise(tnt_BoxException,
			  reason:"tuple encoding error" errcode:ERR_CODE_ILLEGAL_PARAMS);

	txn->tuple = tuple_alloc(data->len);
	tuple_txn_ref(txn, txn->tuple);
	txn->tuple->cardinality = cardinality;
	memcpy(txn->tuple->data, data->data, data->len);

	txn->old_tuple = txn->index->find_by_tuple(txn->index, txn->tuple);

	if (txn->old_tuple != NULL)
		tuple_txn_ref(txn, txn->old_tuple);

	if (txn->flags & BOX_ADD && txn->old_tuple != NULL)
		tnt_raise(tnt_BoxException, reason:"tuple found" errcode:ERR_CODE_NODE_FOUND);

	if (txn->flags & BOX_REPLACE && txn->old_tuple == NULL)
		tnt_raise(tnt_BoxException,
			  reason:"tuple not found" errcode:ERR_CODE_NODE_NOT_FOUND);

	validate_indexes(txn);
	run_hooks(txn, before_commit_update_hook);

	if (txn->old_tuple != NULL) {
#ifndef NDEBUG
		void *ka, *kb;
		ka = tuple_field(txn->tuple, txn->index->key_field->fieldno);
		kb = tuple_field(txn->old_tuple, txn->index->key_field->fieldno);
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
		 * index replace: if it fails, txn_abort() will
		 * look at the flag and remove the tuple.
		 */
		txn->tuple->flags |= GHOST;
		/*
		 * If the tuple doesn't exist, insert a GHOST
		 * tuple in all indices in order to avoid a race
		 * condition when another INSERT comes along:
		 * a concurrent INSERT, UPDATE, or DELETE, returns
		 * an error when meets a ghost tuple.
		 *
		 * Tuple reference counter will be incremented in
		 * txn_commit().
		 */
		foreach_index(txn->n, index)
			index->replace(index, NULL, txn->tuple);
	}

	if (!(txn->flags & BOX_QUIET)) {
		u32 tuples_affected = 1;

		add_iov_dup(&tuples_affected, sizeof(uint32_t));

		if (txn->flags & BOX_RETURN_TUPLE)
			tuple_add_iov(txn, txn->tuple);
	}
}

static void
commit_replace(struct box_txn *txn)
{
	if (txn->old_tuple != NULL) {
		foreach_index(txn->n, index)
			index->replace(index, txn->old_tuple, txn->tuple);

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
		foreach_index(txn->n, index)
			index->remove(index, txn->tuple);
	}
}

static void
do_field_arith(u8 op, struct tbuf *field, void *arg, u32 arg_size)
{
	if (field->len != 4)
		tnt_raise(tnt_BoxException,
			  reason:"num op on field with length != 4"
			  errcode:ERR_CODE_ILLEGAL_PARAMS);
	if (arg_size != 4)
		tnt_raise(tnt_BoxException,
			  reason:"num op with arg not u32" errcode:ERR_CODE_ILLEGAL_PARAMS);

	switch (op) {
	case 1:
		*(i32 *)field->data += *(i32 *)arg;
		break;
	case 2:
		*(u32 *)field->data &= *(u32 *)arg;
		break;
	case 3:
		*(u32 *)field->data ^= *(u32 *)arg;
		break;
	case 4:
		*(u32 *)field->data |= *(u32 *)arg;
		break;
	}
}

static void
do_field_splice(struct tbuf *field, void *args_data, u32 args_data_size)
{
	struct tbuf args = {
		.len = args_data_size,
		.size = args_data_size,
		.data = args_data,
		.pool = NULL
	};
	struct tbuf *new_field = NULL;
	void *offset_field, *length_field, *list_field;
	u32 offset_size, length_size, list_size;
	i32 offset, length;
	u32 noffset, nlength;	/* normalized values */

	new_field = tbuf_alloc(fiber->pool);

	offset_field = read_field(&args);
	length_field = read_field(&args);
	list_field = read_field(&args);
	if (args.len != 0)
		tnt_raise(tnt_BoxException,
			  reason:"do_field_splice: bad args" errcode:ERR_CODE_ILLEGAL_PARAMS);

	offset_size = load_varint32(&offset_field);
	if (offset_size == 0)
		noffset = 0;
	else if (offset_size == sizeof(offset)) {
		offset = pick_u32(offset_field, &offset_field);
		if (offset < 0) {
			if (field->len < -offset)
				tnt_raise(tnt_BoxException,
					  reason:"do_field_splice: noffset is negative"
					  errcode:ERR_CODE_ILLEGAL_PARAMS);
			noffset = offset + field->len;
		} else
			noffset = offset;
	} else
		tnt_raise(tnt_BoxException,
			  reason:"do_field_splice: bad size of offset field"
			  errcode:ERR_CODE_ILLEGAL_PARAMS);
	if (noffset > field->len)
		noffset = field->len;

	length_size = load_varint32(&length_field);
	if (length_size == 0)
		nlength = field->len - noffset;
	else if (length_size == sizeof(length)) {
		if (offset_size == 0)
			tnt_raise(tnt_BoxException,
				  reason:"do_field_splice: offset field is empty but length is not"
				  errcode:ERR_CODE_ILLEGAL_PARAMS);

		length = pick_u32(length_field, &length_field);
		if (length < 0) {
			if ((field->len - noffset) < -length)
				nlength = 0;
			else
				nlength = length + field->len - noffset;
		} else
			nlength = length;
	} else
		tnt_raise(tnt_BoxException,
			  reason:"do_field_splice: bad size of length field"
			  errcode:ERR_CODE_ILLEGAL_PARAMS);
	if (nlength > (field->len - noffset))
		nlength = field->len - noffset;

	list_size = load_varint32(&list_field);
	if (list_size > 0 && length_size == 0)
		tnt_raise(tnt_BoxException,
			  reason:"do_field_splice: length field is empty but list is not"
			  errcode:ERR_CODE_ILLEGAL_PARAMS);
	if (list_size > (UINT32_MAX - (field->len - nlength)))
		tnt_raise(tnt_BoxException,
			  reason:"do_field_splice: list_size is too long"
			  errcode:ERR_CODE_ILLEGAL_PARAMS);

	say_debug("do_field_splice: noffset = %i, nlength = %i, list_size = %u",
		  noffset, nlength, list_size);

	new_field->len = 0;
	tbuf_append(new_field, field->data, noffset);
	tbuf_append(new_field, list_field, list_size);
	tbuf_append(new_field, field->data + noffset + nlength, field->len - (noffset + nlength));

	*field = *new_field;
}

static void __attribute__((noinline))
prepare_update_fields(struct box_txn *txn, struct tbuf *data)
{
	struct tbuf **fields;
	void *field;
	int i;
	void *key;
	u32 op_cnt;
	u32 tuples_affected = 1;

	u32 key_len = read_u32(data);
	if (key_len != 1)
		tnt_raise(tnt_BoxException,
			  reason:"key must be single valued" errcode:ERR_CODE_ILLEGAL_PARAMS);
	key = read_field(data);
	op_cnt = read_u32(data);

	if (op_cnt > 128)
		tnt_raise(tnt_BoxException, reason:"too many ops" errcode:ERR_CODE_ILLEGAL_PARAMS);
	if (op_cnt == 0)
		tnt_raise(tnt_BoxException, reason:"no ops" errcode:ERR_CODE_ILLEGAL_PARAMS);
	if (key == NULL)
		tnt_raise(tnt_BoxException, reason:"invalid key" errcode:ERR_CODE_ILLEGAL_PARAMS);

	txn->old_tuple = txn->index->find(txn->index, key);
	if (txn->old_tuple == NULL) {
		txn->flags |= BOX_NOT_STORE;

		tuples_affected = 0;

		goto out;
	}

	lock_tuple(txn, txn->old_tuple);

	fields = palloc(fiber->pool, (txn->old_tuple->cardinality + 1) * sizeof(struct tbuf *));
	memset(fields, 0, (txn->old_tuple->cardinality + 1) * sizeof(struct tbuf *));

	for (i = 0, field = (uint8_t *)txn->old_tuple->data; i < txn->old_tuple->cardinality; i++) {
		fields[i] = tbuf_alloc(fiber->pool);

		u32 field_size = load_varint32(&field);
		tbuf_append(fields[i], field, field_size);
		field += field_size;
	}

	while (op_cnt-- > 0) {
		u8 op;
		u32 field_no, arg_size;
		void *arg;

		field_no = read_u32(data);

		if (field_no >= txn->old_tuple->cardinality)
			tnt_raise(tnt_BoxException,
				  reason:"update of field beyond tuple cardinality"
				  errcode:ERR_CODE_ILLEGAL_PARAMS);

		struct tbuf *sptr_field = fields[field_no];

		op = read_u8(data);
		if (op > 5)
			tnt_raise(tnt_BoxException,
				  reason:"op is not 0, 1, 2, 3, 4 or 5"
				  errcode:ERR_CODE_ILLEGAL_PARAMS);
		arg = read_field(data);
		arg_size = load_varint32(&arg);

		if (op == 0) {
			tbuf_ensure(sptr_field, arg_size);
			sptr_field->len = arg_size;
			memcpy(sptr_field->data, arg, arg_size);
		} else {
			switch (op) {
			case 1:
			case 2:
			case 3:
			case 4:
				do_field_arith(op, sptr_field, arg, arg_size);
				break;
			case 5:
				do_field_splice(sptr_field, arg, arg_size);
				break;
			}
		}
	}

	if (data->len != 0)
		tnt_raise(tnt_BoxException,
			  reason:"can't unpack request" errcode:ERR_CODE_ILLEGAL_PARAMS);

	size_t bsize = 0;
	for (int i = 0; i < txn->old_tuple->cardinality; i++)
		bsize += fields[i]->len + varint32_sizeof(fields[i]->len);
	txn->tuple = tuple_alloc(bsize);
	tuple_txn_ref(txn, txn->tuple);
	txn->tuple->cardinality = txn->old_tuple->cardinality;

	uint8_t *p = txn->tuple->data;
	for (int i = 0; i < txn->old_tuple->cardinality; i++) {
		p = save_varint32(p, fields[i]->len);
		memcpy(p, fields[i]->data, fields[i]->len);
		p += fields[i]->len;
	}

	validate_indexes(txn);
	run_hooks(txn, before_commit_update_hook);

	if (data->len != 0)
		tnt_raise(tnt_BoxException,
			  reason:"can't unpack request" errcode:ERR_CODE_ILLEGAL_PARAMS);

out:
	if (!(txn->flags & BOX_QUIET)) {
		add_iov_dup(&tuples_affected, sizeof(uint32_t));

		if (txn->flags & BOX_RETURN_TUPLE)
			tuple_add_iov(txn, txn->tuple);
	}
}

static void
tuple_add_iov(struct box_txn *txn, struct box_tuple *tuple)
{
	size_t len;

	len = tuple->bsize +
		field_sizeof(struct box_tuple, bsize) +
		field_sizeof(struct box_tuple, cardinality);

	if (len > BOX_REF_THRESHOLD) {
		tuple_txn_ref(txn, tuple);
		add_iov(&tuple->bsize, len);
	} else {
		add_iov_dup(&tuple->bsize, len);
	}
}

static void __attribute__((noinline))
process_select(struct box_txn *txn, u32 limit, u32 offset, struct tbuf *data)
{
	struct box_tuple *tuple;
	uint32_t *found;
	u32 count = read_u32(data);
	if (count == 0)
		tnt_raise(tnt_BoxException,
			  reason:"tuple count must be greater than zero"
			  errcode:ERR_CODE_ILLEGAL_PARAMS);

	found = palloc(fiber->pool, sizeof(*found));
	add_iov(found, sizeof(*found));
	*found = 0;

	if (txn->index->type == TREE) {
		for (u32 i = 0; i < count; i++) {

			/* End the loop if reached the limit. */
			if (limit == *found)
				return;

			u32 key_len = read_u32(data);
			void *key = read_field(data);

			/* advance remaining fields of a key */
			for (int i = 1; i < key_len; i++)
				read_field(data);

			struct tree_index_member *pattern =
				alloc_search_pattern(txn->index, key_len, key);
			txn->index->iterator_init(txn->index, pattern);

			while ((tuple = txn->index->iterator_next(txn->index, pattern)) != NULL) {
				if (tuple->flags & GHOST)
					continue;

				if (offset > 0) {
					offset--;
					continue;
				}

				tuple_add_iov(txn, tuple);

				if (limit == ++(*found))
					break;
			}
		}
	} else {
		for (u32 i = 0; i < count; i++) {

			/* End the loop if reached the limit. */
			if (limit == *found)
				return;

			u32 key_len = read_u32(data);
			if (key_len != 1)
				tnt_raise(tnt_BoxException,
					  reason:"key must be single valued"
					  errcode:ERR_CODE_ILLEGAL_PARAMS);
			void *key = read_field(data);
			tuple = txn->index->find(txn->index, key);
			if (tuple == NULL || tuple->flags & GHOST)
				continue;

			if (offset > 0) {
				offset--;
				continue;
			}

			tuple_add_iov(txn, tuple);
			(*found)++;
		}
	}

	if (data->len != 0)
		tnt_raise(tnt_BoxException,
			  reason:"can't unpack request" errcode:ERR_CODE_ILLEGAL_PARAMS);
}

static void __attribute__((noinline))
prepare_delete(struct box_txn *txn, void *key)
{
	u32 tuples_affected = 0;

	txn->old_tuple = txn->index->find(txn->index, key);

	if (txn->old_tuple == NULL)
		/*
		 * There is no subject tuple we could write to WAL, which means,
		 * to do a write, we would have to allocate one. Too complicated,
		 * for now, just do no logging for DELETEs that do nothing.
		 */
		txn->flags |= BOX_NOT_STORE;
	else {
		tuple_txn_ref(txn, txn->old_tuple);
		lock_tuple(txn, txn->old_tuple);

		tuples_affected = 1;
	}

	if (!(txn->flags & BOX_QUIET))
		add_iov_dup(&tuples_affected, sizeof(tuples_affected));
}

static void
commit_delete(struct box_txn *txn)
{
	if (txn->old_tuple == NULL)
		return;

	foreach_index(txn->n, index)
		index->remove(index, txn->old_tuple);
	tuple_ref(txn->old_tuple, -1);
}

static bool
op_is_select(u32 op)
{
	return op == SELECT || op == SELECT_LIMIT;
}

struct box_txn *
txn_alloc(u32 flags)
{
	struct box_txn *txn = p0alloc(fiber->pool, sizeof(*txn));
	txn->ref_tuples = tbuf_alloc(fiber->pool);
	txn->flags = flags;
	return txn;
}

/**
  * Validate the request and start a transaction associated with that request.
  * 
  * This function does too much:
  * - parses the request,
  * - takes a "savepoint" of fiber->iov_cnt,
  * - performs a "name resolution", i.e. find the namespace object
  *   associated with the request.
  * @todo: split in smaller blocks. 
  */

static void
txn_begin(struct box_txn *txn, u16 op, struct tbuf *data)
{
	txn->saved_iov_cnt = fiber->iov_cnt;

	txn->op = op;
	txn->req = (struct tbuf){ .data = data->data, .len = data->len };
	txn->n = read_u32(data);
	if (txn->n < 0 || txn->n > namespace_count - 1)
		tnt_raise(tnt_BoxException,
			  reason:"bad namespace number" errcode:ERR_CODE_NO_SUCH_NAMESPACE);
	txn->index = &namespace[txn->n].index[0];

	if (!namespace[txn->n].enabled) {
		say_warn("namespace %i is not enabled", txn->n);
		tnt_raise(tnt_BoxException,
			  reason:"namespace is not enabled" errcode:ERR_CODE_NO_SUCH_NAMESPACE);
	}

	txn->namespace = &namespace[txn->n];
}

void
txn_cleanup(struct box_txn *txn)
{
	/*
	 * txn_cleanup maybe called twice in following scenario:
	 * several request processed by single iproto loop run
	 * first one successed, but the last one fails with OOM
	 * in this case fiber perform fiber_cleanup for every registered callback
	 * we should not not run cleanup twice.
	 */
	if (txn->op == 0)
		return;

	unlock_tuples(txn);

	struct box_tuple **tuple = txn->ref_tuples->data;
	int i = txn->ref_tuples->len / sizeof(struct box_txn *);

	while (i-- > 0) {
		say_debug("tuple_txn_unref(%p)", *tuple);
		tuple_ref(*tuple++, -1);
	}

	/* mark txn as clean */
	memset(txn, 0, sizeof(*txn));
}

static void
txn_commit(struct box_txn *txn)
{
	if (txn->op == 0)
		return;

	if (!op_is_select(txn->op)) {
		say_debug("box_commit(op:%s)", messages_strs[txn->op]);

		if (txn->flags & BOX_NOT_STORE)
			;
		else {
			fiber_peer_name(fiber); /* fill the cookie */
			struct tbuf *t = tbuf_alloc(fiber->pool);
			tbuf_append(t, &txn->op, sizeof(txn->op));
			tbuf_append(t, txn->req.data, txn->req.len);

			i64 lsn = next_lsn(recovery_state, 0);
			if (!wal_write(recovery_state, wal_tag, fiber->cookie, lsn, t))
				tnt_raise(tnt_BoxException, errcode:ERR_CODE_WAL_IO);
			confirm_lsn(recovery_state, lsn);
		}

		unlock_tuples(txn);

		if (txn->op == DELETE)
			commit_delete(txn);
		else
			commit_replace(txn);
	}

	if (txn->flags & BOX_QUIET)
		txn_cleanup(txn);
	else
		fiber_register_cleanup((fiber_cleanup_handler)txn_cleanup, txn);
}

static void
txn_abort(struct box_txn *txn)
{
	if (txn->op == 0)
		return;

	fiber->iov_cnt = txn->saved_iov_cnt;

	if (!op_is_select(txn->op)) {
		say_debug("box_rollback(op:%s)", messages_strs[txn->op]);

		unlock_tuples(txn);

		if (txn->op == INSERT)
			rollback_replace(txn);
	}

	txn_cleanup(txn);
}

static void
box_dispach(struct box_txn *txn, struct tbuf *data)
{
	u32 cardinality;
	void *key;
	u32 key_len;

	say_debug("box_dispach(%i)", txn->op);

	switch (txn->op) {
	case INSERT:
		txn->flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
		cardinality = read_u32(data);
		if (namespace[txn->n].cardinality > 0
		    && namespace[txn->n].cardinality != cardinality)
			tnt_raise(tnt_BoxException,
				  reason:"tuple cardinality must match namespace cardinality"
				  errcode:ERR_CODE_ILLEGAL_PARAMS);
		prepare_replace(txn, cardinality, data);
		break;

	case DELETE:
		key_len = read_u32(data);
		if (key_len != 1)
			tnt_raise(tnt_BoxException,
				  reason:"key must be single valued"
				  errcode:ERR_CODE_ILLEGAL_PARAMS);

		key = read_field(data);
		if (data->len != 0)
			tnt_raise(tnt_BoxException,
				  reason:"can't unpack request" errcode:ERR_CODE_ILLEGAL_PARAMS);

		prepare_delete(txn, key);
		break;

	case SELECT:{
			u32 i = read_u32(data);
			u32 offset = read_u32(data);
			u32 limit = read_u32(data);

			if (i > MAX_IDX)
				tnt_raise(tnt_BoxException,
					  reason:"index too big" errcode:ERR_CODE_NO_SUCH_INDEX);
			txn->index = &namespace[txn->n].index[i];
			if (txn->index->key_cardinality == 0)
				tnt_raise(tnt_BoxException,
					  reason:"index is invalid"
					  errcode:ERR_CODE_NO_SUCH_INDEX);

			process_select(txn, limit, offset, data);
			break;
		}

	case UPDATE_FIELDS:
		txn->flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
		prepare_update_fields(txn, data);
		break;

	default:
		say_error("silverbox_dispach: unsupported command = %" PRIi32 "", txn->op);
		tnt_raise(tnt_BoxException, errcode:ERR_CODE_ILLEGAL_PARAMS);
	}
}

static int
box_xlog_sprint(struct tbuf *buf, const struct tbuf *t)
{
	struct row_v11 *row = row_v11(t);

	struct tbuf *b = palloc(fiber->pool, sizeof(*b));
	b->data = row->data;
	b->len = row->len;
	u16 tag, op;
	u64 cookie;
	struct sockaddr_in *peer = (void *)&cookie;

	u32 n, key_len;
	void *key;
	u32 cardinality, field_no;
	u32 flags;
	u32 op_cnt;

	tbuf_printf(buf, "lsn:%" PRIi64 " ", row->lsn);

	say_debug("b->len:%" PRIu32, b->len);

	tag = read_u16(b);
	cookie = read_u64(b);
	op = read_u16(b);
	n = read_u32(b);

	tbuf_printf(buf, "tm:%.3f t:%" PRIu16 " %s:%d %s n:%i",
		    row->tm, tag, inet_ntoa(peer->sin_addr), ntohs(peer->sin_port),
		    messages_strs[op], n);

	switch (op) {
	case INSERT:
		flags = read_u32(b);
		cardinality = read_u32(b);
		if (b->len != valid_tuple(b, cardinality))
			abort();
		tuple_print(buf, cardinality, b->data);
		break;

	case DELETE:
		key_len = read_u32(b);
		key = read_field(b);
		if (b->len != 0)
			abort();
		tuple_print(buf, key_len, key);
		break;

	case UPDATE_FIELDS:
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

struct tbuf *
box_snap_reader(FILE *f, struct palloc_pool *pool)
{
	struct tbuf *row = tbuf_alloc(pool);
	const int header_size = sizeof(*box_snap_row(row));

	tbuf_reserve(row, header_size);
	if (fread(row->data, header_size, 1, f) != 1)
		return NULL;

	tbuf_reserve(row, box_snap_row(row)->data_size);
	if (fread(box_snap_row(row)->data, box_snap_row(row)->data_size, 1, f) != 1)
		return NULL;

	return convert_to_v11(row, snap_tag, default_cookie, 0);
}

static int
snap_print(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
{
	struct tbuf *out = tbuf_alloc(t->pool);
	struct box_snap_row *row;
	struct row_v11 *raw_row = row_v11(t);

	struct tbuf *b = palloc(fiber->pool, sizeof(*b));
	b->data = raw_row->data;
	b->len = raw_row->len;

	(void)read_u16(b); /* drop tag */
	(void)read_u64(b); /* drop cookie */

	row = box_snap_row(b);

	tuple_print(out, row->tuple_size, row->data);
	printf("n:%i %*s\n", row->namespace, (int)out->len, (char *)out->data);
	return 0;
}

static int
xlog_print(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
{
	struct tbuf *out = tbuf_alloc(t->pool);
	int res = box_xlog_sprint(out, t);
	if (res >= 0)
		printf("%*s\n", (int)out->len, (char *)out->data);
	return res;
}

static void
custom_init(void)
{
	before_commit_update_hook = calloc(1, sizeof(box_hook_t));

	if (cfg.namespace == NULL)
		panic("at least one namespace must be configured");

	for (int i = 0; i < namespace_count; i++) {
		if (cfg.namespace[i] == NULL)
			break;

		if (!CNF_STRUCT_DEFINED(cfg.namespace[i]))
			namespace[i].enabled = false;
		else
			namespace[i].enabled = !!cfg.namespace[i]->enabled;

		if (!namespace[i].enabled)
			continue;

		namespace[i].cardinality = cfg.namespace[i]->cardinality;
		int estimated_rows = cfg.namespace[i]->estimated_rows;

		if (cfg.namespace[i]->index == NULL)
			panic("(namespace = %" PRIu32 ") at least one index must be defined", i);

		for (int j = 0; j < nelem(namespace[i].index); j++) {
			struct index *index = &namespace[i].index[j];
			u32 max_key_fieldno = 0;

			memset(index, 0, sizeof(*index));

			if (cfg.namespace[i]->index[j] == NULL)
				break;

			if (cfg.namespace[i]->index[j]->key_field == NULL)
				panic("(namespace = %" PRIu32 " index = %" PRIu32 ") "
				      "at least one field must be defined", i, j);

			for (int k = 0; cfg.namespace[i]->index[j]->key_field[k] != NULL; k++) {
				if (cfg.namespace[i]->index[j]->key_field[k]->fieldno == -1)
					break;

				max_key_fieldno =
					MAX(max_key_fieldno,
					    cfg.namespace[i]->index[j]->key_field[k]->fieldno);

				++index->key_cardinality;
			}

			if (index->key_cardinality == 0)
				continue;

			index->key_field = salloc(sizeof(index->key_field[0]) *
						  index->key_cardinality);
			if (index->key_field == NULL)
				panic("can't allocate key_field for index");

			index->field_cmp_order_cnt = max_key_fieldno + 1;
			index->field_cmp_order =
				salloc(sizeof(index->field_cmp_order[0]) *
				       index->field_cmp_order_cnt);
			if (index->field_cmp_order == NULL)
				panic("can't allocate field_cmp_order for index");
			memset(index->field_cmp_order, -1,
			       sizeof(index->field_cmp_order[0]) * index->field_cmp_order_cnt);

			for (int k = 0; cfg.namespace[i]->index[j]->key_field[k] != NULL; k++) {
				typeof(cfg.namespace[i]->index[j]->key_field[k]) cfg_key_field =
					cfg.namespace[i]->index[j]->key_field[k];

				if (cfg_key_field->fieldno == -1)
					break;

				index->key_field[k].fieldno = cfg_key_field->fieldno;
				if (strcmp(cfg_key_field->type, "NUM") == 0)
					index->key_field[k].type = NUM;
				else if (strcmp(cfg_key_field->type, "NUM64") == 0)
					index->key_field[k].type = NUM64;
				else if (strcmp(cfg_key_field->type, "STR") == 0)
					index->key_field[k].type = STRING;
				else
					panic("(namespace = %" PRIu32 " index = %" PRIu32 ") "
					      "unknown field data type: `%s'",
					      i, j, cfg_key_field->type);

				index->field_cmp_order[index->key_field[k].fieldno] = k;
			}

			index->search_pattern = palloc(eter_pool, SIZEOF_TREE_INDEX_MEMBER(index));

			if (cfg.namespace[i]->index[j]->unique == 0)
				index->unique = false;
			else if (cfg.namespace[i]->index[j]->unique == 1)
				index->unique = true;
			else
				panic("(namespace = %" PRIu32 " index = %" PRIu32 ") "
				      "unique property is undefined", i, j);

			if (strcmp(cfg.namespace[i]->index[j]->type, "HASH") == 0) {
				if (index->key_cardinality != 1)
					panic("(namespace = %" PRIu32 " index = %" PRIu32 ") "
					      "hash index must have a single-field key", i, j);

				if (index->unique == false)
					panic("(namespace = %" PRIu32 " index = %" PRIu32 ") "
					      "hash index must be unique", i, j);

				index->enabled = true;
				if (index->key_field->type == NUM) {
					index_hash_num(index, &namespace[i], estimated_rows);
				} else if (index->key_field->type == NUM64) {
					index_hash_num64(index, &namespace[i], estimated_rows);
				} else {
					index_hash_str(index, &namespace[i], estimated_rows);
				}
			} else if (strcmp(cfg.namespace[i]->index[j]->type, "TREE") == 0) {
				index->enabled = false;
				index_tree(index, &namespace[0], 0);
			} else
				panic("namespace = %" PRIu32 " index = %" PRIu32 ") "
				      "unknown index type `%s'",
				      i, j, cfg.namespace[i]->index[j]->type);
		}

		if (namespace[i].index[0].key_cardinality == 0)
			panic("(namespace = %" PRIu32 ") namespace must have at least one index",
			      i);
		if (namespace[i].index[0].type != HASH)
			panic("(namespace = %" PRIu32 ") namespace first index must be HASH", i);

		namespace[i].enabled = true;
		namespace[i].n = i;

		say_info("namespace %i successfully configured", i);
	}
}

u32
box_process(struct box_txn *txn, u32 op, struct tbuf *request_data)
{
	ev_tstamp start = ev_now(), stop;

	stat_collect(stat_base, op, 1);

	@try {
		txn_begin(txn, op, request_data);
		box_dispach(txn, request_data);
		txn_commit(txn);

		return ERR_CODE_OK;
	}
	@catch (tnt_PickleException *e) {
		txn_abort(txn);

		say_error("tnt_PickleException: `%s' at %s:%i", e->reason, e->file, e->line);

		return ERR_CODE_ILLEGAL_PARAMS;
	}
	@catch (tnt_BoxException *e) {
		txn_abort(txn);

		if (e->errcode != ERR_CODE_NODE_IS_RO)
			say_error("tnt_BoxException: %s/`%s' at %s:%i",
				  tnt_errcode_desc(e->errcode),
				  e->reason, e->file, e->line);

		return e->errcode;
	}
	@catch (id e) {
		txn_abort(txn);

		@throw;
	}
	@finally {
		stop = ev_now();
		if (stop - start > cfg.too_long_threshold)
			say_warn("too long %s: %.3f sec", messages_strs[txn->op], stop - start);
	}
}

static u32
box_process_ro(u32 op, struct tbuf *request_data)
{
	if (!op_is_select(op)) {
		say_error("can't process %i command on RO port", op);

		return ERR_CODE_NONMASTER;
	}

	return box_process(txn_alloc(0), op, request_data);
}

static u32
box_process_rw(u32 op, struct tbuf *request_data)
{
	if (!op_is_select(op) && !box_updates_allowed) {
		say_error("can't process %i command, updates are disallowed", op);

		return ERR_CODE_NONMASTER;
	}

	return box_process(txn_alloc(0), op, request_data);
}

static struct tbuf *
convert_snap_row_to_wal(struct tbuf *t)
{
	struct tbuf *r = tbuf_alloc(fiber->pool);
	struct box_snap_row *row = box_snap_row(t);
	u16 op = INSERT;
	u32 flags = 0;

	tbuf_append(r, &op, sizeof(op));
	tbuf_append(r, &row->namespace, sizeof(row->namespace));
	tbuf_append(r, &flags, sizeof(flags));
	tbuf_append(r, &row->tuple_size, sizeof(row->tuple_size));
	tbuf_append(r, row->data, row->data_size);

	return r;
}

static int
recover_row(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
{
	struct box_txn *txn = txn_alloc(BOX_QUIET | BOX_NOT_STORE);
	u16 op;

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

	op = read_u16(t);

	if (box_process(txn, op, t) != ERR_CODE_OK)
		return -1;

	return 0;
}

static void
title(const char *fmt, ...)
{
	va_list ap;
	char buf[64];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (cfg.memcached)
		set_proc_title("%s%s memcached:%i adm:%i",
			       buf, custom_proc_title, cfg.primary_port, cfg.admin_port);
	else
		set_proc_title("%s%s pri:%i sec:%i adm:%i",
			       buf, custom_proc_title,
			       cfg.primary_port, cfg.secondary_port, cfg.admin_port);
}

static void
remote_recovery_restart(struct tarantool_cfg *conf)
{
	if (remote_recover) {
		say_info("shutting downing the replica");
		fiber_call(remote_recover);
	}

	say_info("starting the replica");
	remote_recover = recover_follow_remote(recovery_state, conf->wal_feeder_ipaddr,
					       conf->wal_feeder_port,
					       default_remote_row_handler);

	status = palloc(eter_pool, 64);
	snprintf(status, 64, "replica/%s:%i%s", conf->wal_feeder_ipaddr,
		 conf->wal_feeder_port, custom_proc_title);
	title("replica/%s:%i%s", conf->wal_feeder_ipaddr, conf->wal_feeder_port,
	      custom_proc_title);
}

static void
box_master_or_slave(struct tarantool_cfg *conf)
{
	if (conf->remote_hot_standby) {
		box_updates_allowed = false;

		remote_recovery_restart(conf);
	} else {
		if (remote_recover) {
			say_info("shuting downing the replica");
			fiber_cancel(remote_recover);

			remote_recover = NULL;
		}

		say_info("I am primary");

		box_updates_allowed = true;

		status = "primary";
		title("primary");
	}
}

static void
box_bound_to_primary(void *data __attribute__((unused)))
{
	recover_finalize(recovery_state);

	box_master_or_slave(&cfg);
}

static void
memcached_bound_to_primary(void *data __attribute__((unused)))
{
	box_bound_to_primary(NULL);

	struct fiber *expire = fiber_create("memecached_expire", -1, -1, memcached_expire, NULL);
	if (expire == NULL)
		panic("can't start the expire fiber");
	fiber_call(expire);
}

i32
mod_check_config(struct tarantool_cfg *conf)
{
	if (conf->remote_hot_standby > 0 && conf->local_hot_standby > 0) {
		out_warning(0, "Remote and local hot standby modes "
			       "can't be enabled simultaneously");

		return -1;
	}

	return 0;
}

i32
mod_reload_config(struct tarantool_cfg *old_conf, struct tarantool_cfg *new_conf)
{
	if (old_conf->remote_hot_standby != new_conf->remote_hot_standby) {
		if (recovery_state->finalize != true) {
			out_warning(0, "Could not propagate %s before local recovery finished",
				    old_conf->remote_hot_standby == true ? "slave to master" :
				    "master to slave");

			return -1;
		}
	}

	if (old_conf->remote_hot_standby != new_conf->remote_hot_standby) {
		/* Local recovery must be finalized at this point */
		assert(recovery_state->finalize == true);

		box_master_or_slave(new_conf);
	} else if (old_conf->remote_hot_standby > 0 &&
		   (strcmp(old_conf->wal_feeder_ipaddr, new_conf->wal_feeder_ipaddr) != 0 ||
		    old_conf->wal_feeder_port != new_conf->wal_feeder_port))
		remote_recovery_restart(new_conf);

	return 0;
}

void
mod_init(void)
{
	stat_base = stat_register(messages_strs, messages_MAX);

	namespace = palloc(eter_pool, sizeof(struct namespace) * namespace_count);
	for (int i = 0; i < namespace_count; i++) {
		namespace[i].enabled = false;
		for (int j = 0; j < MAX_IDX; j++)
			namespace[i].index[j].key_cardinality = 0;
	}

	if (cfg.custom_proc_title == NULL)
		custom_proc_title = "";
	else {
		custom_proc_title = palloc(eter_pool, strlen(cfg.custom_proc_title) + 2);
		strcat(custom_proc_title, "@");
		strcat(custom_proc_title, cfg.custom_proc_title);
	}

	if (cfg.memcached != 0) {
		if (cfg.secondary_port != 0)
			panic("in memcached mode secondary_port must be 0");
		if (cfg.remote_hot_standby)
			panic("remote replication is not supported in memcached mode.");

		memcached_init();
	}

	title("loading");

	if (cfg.remote_hot_standby) {
		if (cfg.wal_feeder_ipaddr == NULL || cfg.wal_feeder_port == 0)
			panic("wal_feeder_ipaddr & wal_feeder_port must be provided in remote_hot_standby mode");
	}

	recovery_state = recover_init(cfg.snap_dir, cfg.wal_dir,
				      box_snap_reader, recover_row,
				      cfg.rows_per_wal, cfg.wal_fsync_delay,
				      cfg.wal_writer_inbox_size,
				      init_storage ? RECOVER_READONLY : 0, NULL);

	recovery_state->snap_io_rate_limit = cfg.snap_io_rate_limit * 1024 * 1024;
	recovery_setup_panic(recovery_state, cfg.panic_on_snap_error, cfg.panic_on_wal_error);

	/* initialize hashes _after_ starting wal writer */
	if (cfg.memcached != 0) {
		int n = cfg.memcached_namespace > 0 ? cfg.memcached_namespace : MEMCACHED_NAMESPACE;

		cfg.namespace = palloc(eter_pool, (n + 1) * sizeof(cfg.namespace[0]));
		for (u32 i = 0; i <= n; ++i) {
			cfg.namespace[i] = palloc(eter_pool, sizeof(cfg.namespace[0][0]));
			cfg.namespace[i]->enabled = false;
		}

		cfg.namespace[n]->enabled = true;
		cfg.namespace[n]->cardinality = 4;
		cfg.namespace[n]->estimated_rows = 0;
		cfg.namespace[n]->index = palloc(eter_pool, 2 * sizeof(cfg.namespace[n]->index[0]));
		cfg.namespace[n]->index[0] =
			palloc(eter_pool, sizeof(cfg.namespace[n]->index[0][0]));
		cfg.namespace[n]->index[1] = NULL;
		cfg.namespace[n]->index[0]->type = "HASH";
		cfg.namespace[n]->index[0]->unique = 1;
		cfg.namespace[n]->index[0]->key_field =
			palloc(eter_pool, 2 * sizeof(cfg.namespace[n]->index[0]->key_field[0]));
		cfg.namespace[n]->index[0]->key_field[0] =
			palloc(eter_pool, sizeof(cfg.namespace[n]->index[0]->key_field[0][0]));
		cfg.namespace[n]->index[0]->key_field[1] = NULL;
		cfg.namespace[n]->index[0]->key_field[0]->fieldno = 0;
		cfg.namespace[n]->index[0]->key_field[0]->type = "STR";

		memcached_index = &namespace[n].index[0];
	}

	custom_init();

	if (init_storage)
		return;

	recover(recovery_state, 0);
	stat_cleanup(stat_base, messages_MAX);

	title("building indexes");

	build_indexes();

	title("orphan");

	if (cfg.local_hot_standby) {
		say_info("starting local hot standby");
		recover_follow(recovery_state, cfg.wal_dir_rescan_delay);
		status = "hot_standby";
		title("hot_standby");
	}

	if (cfg.memcached != 0) {
		fiber_server(tcp_server, cfg.primary_port, memcached_handler, NULL,
			     memcached_bound_to_primary);
	} else {
		if (cfg.secondary_port != 0)
			fiber_server(tcp_server, cfg.secondary_port, iproto_interact,
				     box_process_ro, NULL);

		if (cfg.primary_port != 0)
			fiber_server(tcp_server, cfg.primary_port, iproto_interact, box_process_rw,
				     box_bound_to_primary);
	}

	say_info("initialized");
}

int
mod_cat(const char *filename)
{
	return read_log(filename, box_snap_reader, xlog_print, snap_print, NULL);
}

void
mod_snapshot(struct log_io_iter *i)
{
	struct tbuf *row;
	struct box_snap_row header;
	struct box_tuple *tuple;
	khiter_t k;

	for (uint32_t n = 0; n < namespace_count; ++n) {
		if (!namespace[n].enabled)
			continue;

		assoc_foreach(namespace[n].index[0].idx.int_hash, k) {
			tuple = kh_value(namespace[n].index[0].idx.int_hash, k);

			if (tuple->flags & GHOST)	// do not save fictive rows
				continue;

			header.namespace = n;
			header.tuple_size = tuple->cardinality;
			header.data_size = tuple->bsize;

			row = tbuf_alloc(fiber->pool);
			tbuf_append(row, &header, sizeof(header));
			tbuf_append(row, tuple->data, tuple->bsize);

			snapshot_write_row(i, snap_tag, default_cookie, row);
		}
	}
}

void
mod_info(struct tbuf *out)
{
	tbuf_printf(out, "info:" CRLF);
	tbuf_printf(out, "  version: \"%s\"" CRLF, tarantool_version());
	tbuf_printf(out, "  uptime: %i" CRLF, (int)tarantool_uptime());
	tbuf_printf(out, "  pid: %i" CRLF, getpid());
	tbuf_printf(out, "  wal_writer_pid: %" PRIi64 CRLF,
		    (i64) recovery_state->wal_writer->pid);
	tbuf_printf(out, "  lsn: %" PRIi64 CRLF, recovery_state->confirmed_lsn);
	tbuf_printf(out, "  recovery_lag: %.3f" CRLF, recovery_state->recovery_lag);
	tbuf_printf(out, "  recovery_last_update: %.3f" CRLF,
		    recovery_state->recovery_last_update_tstamp);
	tbuf_printf(out, "  status: %s" CRLF, status);
}

void
mod_exec(char *str __attribute__((unused)), int len __attribute__((unused)),
	 struct tbuf *out)
{
	tbuf_printf(out, "unimplemented" CRLF);
}
