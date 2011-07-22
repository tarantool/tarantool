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
#include <salloc.h>
#include <say.h>
#include <stat.h>
#include <tarantool.h>
#include <tbuf.h>
#include <util.h>

#include <cfg/tarantool_box_cfg.h>
#include <mod/box/index.h>
#include "memcached.h"

static void box_process_ro(u32 op, struct tbuf *request_data);
static void box_process_rw(u32 op, struct tbuf *request_data);

const char *mod_name = "Box";

iproto_callback rw_callback = box_process_ro;
static char status[64] = "unknown";

static int stat_base;
STRS(messages, MESSAGES);

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

struct box_snap_row {
	u32 namespace;
	u32 tuple_size;
	u32 data_size;
	u8 data[];
} __attribute__((packed));


static inline struct box_snap_row *
box_snap_row(const struct tbuf *t)
{
	return (struct box_snap_row *)t->data;
}

static void tuple_add_iov(struct box_txn *txn, struct box_tuple *tuple);


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
	size_t total = sizeof(struct box_tuple) + size;
	struct box_tuple *tuple = salloc(total);

	if (tuple == NULL)
		tnt_raise(LoggedError, :ER_MEMORY_ISSUE, total, "slab allocator", "tuple");

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
		tnt_raise(IllegalParams, :"tuple cardinality is 0");

	if (data->len == 0 || data->len != valid_tuple(data, cardinality))
		tnt_raise(IllegalParams, :"incorrect tuple length");

	txn->tuple = tuple_alloc(data->len);
	tuple_txn_ref(txn, txn->tuple);
	txn->tuple->cardinality = cardinality;
	memcpy(txn->tuple->data, data->data, data->len);

	txn->old_tuple = txn->index->find_by_tuple(txn->index, txn->tuple);

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
		tnt_raise(IllegalParams, :"numeric operation on a field with length != 4");

	if (arg_size != 4)
		tnt_raise(IllegalParams, :"the argument of a numeric operation is not a 4-byte int");

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
		tnt_raise(IllegalParams, :"field splice: bad arguments");

	offset_size = load_varint32(&offset_field);
	if (offset_size == 0)
		noffset = 0;
	else if (offset_size == sizeof(offset)) {
		offset = pick_u32(offset_field, &offset_field);
		if (offset < 0) {
			if (field->len < -offset)
				tnt_raise(IllegalParams,
					  :"field splice: offset is negative");
			noffset = offset + field->len;
		} else
			noffset = offset;
	} else
		tnt_raise(IllegalParams, :"field splice: wrong size of offset");

	if (noffset > field->len)
		noffset = field->len;

	length_size = load_varint32(&length_field);
	if (length_size == 0)
		nlength = field->len - noffset;
	else if (length_size == sizeof(length)) {
		if (offset_size == 0)
			tnt_raise(IllegalParams,
				  :"field splice: offset is empty but length is not");

		length = pick_u32(length_field, &length_field);
		if (length < 0) {
			if ((field->len - noffset) < -length)
				nlength = 0;
			else
				nlength = length + field->len - noffset;
		} else
			nlength = length;
	} else
		tnt_raise(IllegalParams, :"field splice: wrong size of length");

	if (nlength > (field->len - noffset))
		nlength = field->len - noffset;

	list_size = load_varint32(&list_field);
	if (list_size > 0 && length_size == 0)
		tnt_raise(IllegalParams,
			  :"field splice: length field is empty but list is not");
	if (list_size > (UINT32_MAX - (field->len - nlength)))
		tnt_raise(IllegalParams, :"field splice: list_size is too long");

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
		tnt_raise(IllegalParams, :"key must be single valued");

	key = read_field(data);
	op_cnt = read_u32(data);

	if (op_cnt > 128)
		tnt_raise(IllegalParams, :"too many operations for update");
	if (op_cnt == 0)
		tnt_raise(IllegalParams, :"no operations for update");
	if (key == NULL)
		tnt_raise(IllegalParams, :"invalid key");

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
			tnt_raise(ClientError, :ER_NO_SUCH_FIELD, field_no);

		struct tbuf *sptr_field = fields[field_no];

		op = read_u8(data);
		if (op > 5)
			tnt_raise(IllegalParams, :"op is not 0, 1, 2, 3, 4 or 5");

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
		tnt_raise(IllegalParams, :"can't unpack request");

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

	if (data->len != 0)
		tnt_raise(IllegalParams, :"can't unpack request");

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
		tnt_raise(IllegalParams, :"tuple count must be positive");

	found = palloc(fiber->pool, sizeof(*found));
	add_iov(found, sizeof(*found));
	*found = 0;

	if (txn->index->type == TREE) {
		for (u32 i = 0; i < count; i++) {

			/* End the loop if reached the limit. */
			if (limit == *found)
				return;

			u32 key_len = read_u32(data);
			void *key = NULL;

			if (key_len != 0)
				key = read_field(data);

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
				tnt_raise(IllegalParams, :"key must be single valued");

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
		tnt_raise(IllegalParams, :"can't unpack request");
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

	if (!(txn->flags & BOX_QUIET)) {
		add_iov_dup(&tuples_affected, sizeof(tuples_affected));

		if (txn->old_tuple && (txn->flags & BOX_RETURN_TUPLE))
			tuple_add_iov(txn, txn->old_tuple);
	}
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
  * Validate the request and start a transaction associated with
  * that request:
  *
  * - parse the request,
  * - perform a "name resolution", i.e. find the namespace object
  *   associated with the request.
  */

static void
txn_begin(struct box_txn *txn, u16 op, struct tbuf *data)
{
	txn->op = op;
	txn->req = (struct tbuf){ .data = data->data, .len = data->len };
	txn->n = read_u32(data);

	if (txn->n < 0 || txn->n >= BOX_NAMESPACE_MAX)
		tnt_raise(ClientError, :ER_NO_SUCH_NAMESPACE, txn->n);

	txn->namespace = &namespace[txn->n];

	if (!txn->namespace->enabled)
		tnt_raise(ClientError, :ER_NAMESPACE_DISABLED, txn->n);

	txn->index = txn->namespace->index;
}

void
txn_cleanup(struct box_txn *txn)
{
	/*
	 * txn_cleanup() may get called twice in the following
	 * scenario: Several requests are processed by a single
	 * iproto loop iteration. The first few requests are
	 * handled successfully, but the next one fails with an
	 * OOM. In this case, fiber performs fiber_cleanup() for
	 * every registered callback. We should not run
	 * txn_cleanup() twice.
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
			bool res = !wal_write(recovery_state, wal_tag,
					      fiber->cookie, lsn, t);
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

	if (!op_is_select(txn->op)) {
		say_debug("box_rollback(op:%s)", messages_strs[txn->op]);

		unlock_tuples(txn);

		if (txn->op == INSERT)
			rollback_replace(txn);
	}

	txn_cleanup(txn);
}

static void
box_dispatch(struct box_txn *txn, struct tbuf *data)
{
	u32 cardinality;
	void *key;
	u32 key_len;

	say_debug("box_dispatch(%i)", txn->op);

	switch (txn->op) {
	case INSERT:
		txn->flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
		cardinality = read_u32(data);
		if (namespace[txn->n].cardinality > 0
		    && namespace[txn->n].cardinality != cardinality)
			tnt_raise(IllegalParams, :"tuple cardinality must match namespace cardinality");
		prepare_replace(txn, cardinality, data);
		break;

	case DELETE:
		txn->flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
	case DELETE_1_3:
		key_len = read_u32(data);
		if (key_len != 1)
			tnt_raise(IllegalParams, :"key must be single valued");

		key = read_field(data);
		if (data->len != 0)
			tnt_raise(IllegalParams, :"can't unpack request");

		prepare_delete(txn, key);
		break;

	case SELECT:{
			u32 i = read_u32(data);
			u32 offset = read_u32(data);
			u32 limit = read_u32(data);

			if (i >= BOX_INDEX_MAX ||
			    namespace[txn->n].index[i].key_cardinality == 0) {

				tnt_raise(LoggedError, :ER_NO_SUCH_INDEX, i, txn->n);
			}
			txn->index = &namespace[txn->n].index[i];

			process_select(txn, limit, offset, data);
			break;
		}

	case UPDATE_FIELDS:
		txn->flags |= read_u32(data) & BOX_ALLOWED_REQUEST_FLAGS;
		prepare_update_fields(txn, data);
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
		flags = read_u32(b);
	case DELETE_1_3:
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

void
namespace_init(void)
{
	namespace = palloc(eter_pool, sizeof(struct namespace) * BOX_NAMESPACE_MAX);
	for (int i = 0; i < BOX_NAMESPACE_MAX; i++) {
		namespace[i].enabled = false;
		for (int j = 0; j < BOX_INDEX_MAX; j++) {
			namespace[i].index[j].key_cardinality = 0;
		}
	}
	/* fill box namespaces */
	for (int i = 0; cfg.namespace[i] != NULL; ++i) {
		tarantool_cfg_namespace *cfg_namespace = cfg.namespace[i];

		if (!CNF_STRUCT_DEFINED(cfg_namespace) || !cfg_namespace->enabled)
			continue;

		assert(cfg.memcached_port == 0 || i != cfg.memcached_namespace);

		namespace[i].enabled = true;

		namespace[i].cardinality = cfg_namespace->cardinality;
		/* fill namespace indexes */
		for (int j = 0; cfg_namespace->index[j] != NULL; ++j) {
			typeof(cfg_namespace->index[j]) cfg_index = cfg_namespace->index[j];
			struct index *index = &namespace[i].index[j];
			u32 max_key_fieldno = 0;

			/* clean-up index struct */
			memset(index, 0, sizeof(*index));

			/* calculate key cardinality and maximal field number */
			for (int k = 0; cfg_index->key_field[k] != NULL; ++k) {
				typeof(cfg_index->key_field[k]) cfg_key = cfg_index->key_field[k];

				if (cfg_key->fieldno == -1) {
					/* last filled key reached */
					break;
				}

				max_key_fieldno = MAX(max_key_fieldno, cfg_key->fieldno);
				++index->key_cardinality;
			}

			/* init key array */
			index->key_field = salloc(sizeof(index->key_field[0]) * index->key_cardinality);
			if (index->key_field == NULL) {
				panic("can't allocate key_field for index");
			}

			/* init compare order array */
			index->field_cmp_order_cnt = max_key_fieldno + 1;
			index->field_cmp_order = salloc(index->field_cmp_order_cnt * sizeof(u32));
			if (index->field_cmp_order == NULL) {
				panic("can't allocate field_cmp_order for index");
			}
			memset(index->field_cmp_order, -1, index->field_cmp_order_cnt * sizeof(u32));

			/* fill fields and compare order */
			for (int k = 0; cfg_index->key_field[k] != NULL; ++k) {
				typeof(cfg_index->key_field[k]) cfg_key = cfg_index->key_field[k];

				if (cfg_key->fieldno == -1) {
					/* last filled key reached */
					break;
				}

				/* fill keys */
				index->key_field[k].fieldno = cfg_key->fieldno;
				index->key_field[k].type = STR2ENUM(field_data_type, cfg_key->type);
				/* fill compare order */
				index->field_cmp_order[cfg_key->fieldno] = k;
			}

			index->search_pattern = palloc(eter_pool, SIZEOF_TREE_INDEX_MEMBER(index));
			index->unique = cfg_index->unique;
			index->type = STR2ENUM(index_type, cfg_index->type);
			index_init(index, &namespace[i], cfg_namespace->estimated_rows);
		}

		namespace[i].enabled = true;
		namespace[i].n = i;

		say_info("namespace %i successfully configured", i);
	}
	memcached_namespace_init();
}

void
box_process(struct box_txn *txn, u32 op, struct tbuf *request_data)
{
	ev_tstamp start = ev_now(), stop;

	stat_collect(stat_base, op, 1);

	@try {
		txn_begin(txn, op, request_data);
		box_dispatch(txn, request_data);
		txn_commit(txn);
	}
	@catch (id e) {
		txn_abort(txn);
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
	if (!op_is_select(op))
		tnt_raise(LoggedError, :ER_NONMASTER);

	return box_process(txn_alloc(0), op, request_data);
}

static void
box_process_rw(u32 op, struct tbuf *request_data)
{
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

	@try {
		box_process(txn, op, t);
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

	/* check configured namespaces */
	for (size_t i = 0; conf->namespace[i] != NULL; ++i) {
		typeof(conf->namespace[i]) namespace = conf->namespace[i];

		if (!CNF_STRUCT_DEFINED(namespace)) {
			/* namespace undefined, skip it */
			continue;
		}

		if (namespace->enabled) {
			/* namespace disabled, skip it */
			continue;
		}

		/* check namespace bound */
		if (i >= BOX_NAMESPACE_MAX) {
			/* maximum namespace is reached */
			out_warning(0, "(namespace = %zu) "
				    "too many namespaces (%i maximum)", i, namespace);
			return -1;
		}

		if (conf->memcached_port && i == conf->memcached_namespace) {
			out_warning(0, "Namespace %i is already used as "
				    "memcached_namespace.", i);
			return -1;
		}

		/* at least one index in namespace must be defined
		 * */
		if (namespace->index == NULL) {
			out_warning(0, "(namespace = %zu) "
				    "at least one index must be defined", i);
			return -1;
		}

		/* check namespaces indexes */
		for (size_t j = 0; namespace->index[j] != NULL; ++j) {
			typeof(namespace->index[j]) index = namespace->index[j];
			u32 index_cardinality = 0;
			enum index_type index_type;

			/* check index bound */
			if (j >= BOX_INDEX_MAX) {
				/* maximum index in namespace reached */
				out_warning(0, "(namespace = %zu index = %zu) "
					    "too many indexed (%i maximum)", i, j, BOX_INDEX_MAX);
				return -1;
			}

			/* at least one key in index must be defined */
			if (index->key_field == NULL) {
				out_warning(0, "(namespace = %zu index = %zu) "
					    "at least one field must be defined", i, j);
				return -1;
			}

			/* check unique property */
			if (index->unique == -1) {
				/* unique property undefined */
				out_warning(0, "(namespace = %zu index = %zu) "
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
					out_warning(0, "(namespace = %zu index = %zu) "
						    "unknown field data type: `%s'", i, j, key->type);
					return -1;
				}

				++index_cardinality;
			}

			/* check index cardinality */
			if (index_cardinality == 0) {
				out_warning(0, "(namespace = %zu index = %zu) "
					    "at least one field must be defined", i, j);
				return -1;
			}

			index_type = STR2ENUM(index_type, index->type);

			/* check index type */
			if (index_type == index_type_MAX) {
				out_warning(0, "(namespace = %zu index = %zu) "
					    "unknown index type '%s'", i, j, index->type);
				return -1;
			}

			/* first namespace index must has hash type */
			if (j == 0 && index_type != HASH) {
				out_warning(0, "(namespace = %zu) namespace first index must has HASH type", i);
				return -1;
			}

			switch (index_type) {
			case HASH:
				/* check hash index */
				/* hash index must has single-field key */
				if (index_cardinality != 0) {
					out_warning(0, "(namespace = %zu index = %zu) "
					            "hash index must has a single-field key", i, j);
					return -1;
				}
				/* hash index must be unique */
				if (!index->unique) {
					out_warning(0, "(namespace = %zu index = %zu) "
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
mod_init(void)
{
	static iproto_callback ro_callback = box_process_ro;

	title("loading");

	/* initialization namespaces */
	namespace_init();

	/* recovery initialization */
	recovery_state = recover_init(cfg.snap_dir, cfg.wal_dir,
				      recover_row, cfg.rows_per_wal, cfg.wal_fsync_delay,
				      cfg.wal_writer_inbox_size,
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

	build_indexes();

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

void
mod_snapshot(struct log_io_iter *i)
{
	struct tbuf *row;
	struct box_snap_row header;
	struct box_tuple *tuple;
	khiter_t k;

	for (uint32_t n = 0; n < BOX_NAMESPACE_MAX; ++n) {
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

struct lua_State *
mod_lua_init(struct lua_State *L)
{
	return L;
}
