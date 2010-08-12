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

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

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

#include <mod/silverbox/box.h>

bool box_updates_allowed = false;
static char *status = "unknown";

STRS(messages, MESSAGES);

const int MEMCACHED_NAMESPACE = 23;
static char *custom_proc_title;

/* hooks */
typedef int (*box_hook_t)(struct box_txn *txn);


struct namespace namespace[256];

struct box_snap_row {
        u32 namespace;
        u32 tuple_size;
        u32 data_size;
	u8 data[];
} __packed__;

static inline struct box_snap_row *
box_snap_row(const struct tbuf *t)
{
	return (struct box_snap_row *)t->data;
}


static void tuple_add_iov(struct box_txn *txn, struct box_tuple *tuple);


box_hook_t *before_commit_update_hook;

#define box_raise(n, err)						\
	({								\
		if (n != ERR_CODE_NODE_IS_RO)				\
			say_warn("box.c:%i %s/%s", __LINE__, error_codes_strs[(n)], err); \
		raise(n, err);						\
	})


static void
run_hooks(struct box_txn *txn, box_hook_t *hook)
{
	for (int i = 0; hook[i] != NULL; i++) {
		int result = (*hook[i])(txn);
		if (result != ERR_CODE_OK)
			box_raise(result, "hook returned error");
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

#define foreach_index(n, index_var)					\
	for (struct index *index_var = namespace[(n)].index;		\
	     index_var->key_position >= 0;				\
	     index_var++)

struct box_tuple *
index_find(struct index *index, int key_len, void *key)
{
	return index->find(index, key_len, key);
}

void
index_remove(struct index *index, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, index->key_position);
	index->remove(index, key);
}

void
index_replace(struct index *index, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, index->key_position);
	index->replace(index, key, tuple);
}


static void
lock_tuple(struct box_txn *txn, struct box_tuple *tuple)
{
        if (tuple->flags & WAL_WAIT)
		box_raise(ERR_CODE_NODE_IS_RO, "tuple is locked");

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
field_print(struct tbuf *buf,  void *f)
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

        for(size_t i = 0; i < cardinality; i++, f = next_field(f)) {
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
        size += sizeof(struct box_tuple);
        struct box_tuple *tuple = salloc(size);

        if (tuple == NULL)
		box_raise(ERR_CODE_MEMORY_ISSUE, "can't allocate tuple");

        tuple->flags = tuple->refs = 0;
	tuple->flags |= NEW;
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


static struct box_tuple *
index_find_hash_num(struct index *self, int key_len, void *key)
{
	struct box_tuple *ret = NULL;
	u32 key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_len != 1)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "key must be single valued");
	if (key_size != 4)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "key is not u32");

	assoc_find(int2ptr_map, self->map.int_map, num, ret);
#ifdef DEBUG
	say_debug("index_find_hash_num(%i, key:%i, tuple:%p)", self->namespace->n, num, ret);
#endif
	return ret;
}

static struct box_tuple *
index_find_hash_str(struct index *self, int key_len, void *key)
{
	struct box_tuple *ret = NULL;
	u32 size;

	if (key_len != 1)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "key must be single valued");

	assoc_find(lstr2ptr_map, self->map.str_map, key, ret);
	size = load_varint32(&key);
#ifdef DEBUG
	say_debug("index_find_hash_str(%i, key:(%i)'%.*s', tuple:%p)", self->namespace->n, size, size, (u8 *)key, ret);
#endif
	return ret;
}

static void
index_remove_hash_num(struct index *self, void *key)
{
	unsigned int key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

        if(key_size != 4)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "key is not u32");
	assoc_delete(int2ptr_map, self->map.int_map, num);
#ifdef DEBUG
	say_debug("index_remove_hash_num(%i, key:%i)", self->namespace->n, num);
#endif
}

static void
index_remove_hash_str(struct index *self, void *key)
{
	assoc_delete(lstr2ptr_map, self->map.str_map, key);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_remove_hash_str(%i, key:'%.*s')", self->namespace->n, size, (u8 *)key);
#endif
}

static void
index_replace_hash_num(struct index *self, void *key, void *value)
{
	u32 key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

        if(key_size != 4)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "key is not u32");
	assoc_replace(int2ptr_map, self->map.int_map, num, value);
#ifdef DEBUG
	say_debug("index_replace_hash_num(%i, key:%i, tuple:%p)", self->namespace->n, num, value);
#endif
}

static void
index_replace_hash_str(struct index *self, void *key, void *value)
{
	assoc_replace(lstr2ptr_map, self->map.str_map, key, value);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_replace_hash_str(%i, key:'%.*s', tuple:%p)", self->namespace->n, size, (u8 *)key, value);
#endif
}

static int __noinline__
prepare_replace(struct box_txn *txn, size_t cardinality, struct tbuf *data)
{
        uint8_t *key;

        assert(data != NULL);
	if (cardinality == 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "cardinality can't be equal to 0");
	if(data->len == 0 || data->len != valid_tuple(data, cardinality))
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "tuple encoding error");

        txn->tuple = tuple_alloc(data->len);
	tuple_txn_ref(txn, txn->tuple);
        txn->tuple->cardinality = cardinality;
        txn->tuple->bsize = data->len;
        memcpy(txn->tuple->data, data->data, data->len);
        key = tuple_field(txn->tuple, txn->index->key_position);
        if(key == NULL)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "invalid tuple: can't find key");

	txn->old_tuple = index_find(txn->index, 1, key);
	if (txn->old_tuple != NULL)
		tuple_txn_ref(txn, txn->old_tuple);

	if (namespace[txn->n].index[1].key_position >= 0) { /* there is more then one index */
		foreach_index(txn->n, index) {
			void *key = tuple_field(txn->tuple, index->key_position);
			if(key == NULL)
				box_raise(ERR_CODE_ILLEGAL_PARAMS, "invalid tuple, can't find key");

			struct box_tuple *tuple = index_find(index, 1, key);

			/*
			 * tuple referenced by secondary keys
			 * must be same as tuple referenced by index[0]
			 * if tuple nonexistent (NULL) - it must be nonexistent in all indeces
			 */
			if(tuple != txn->old_tuple) {
				box_raise(ERR_CODE_ILLEGAL_PARAMS, "index violation");
			}
		}
	}

        run_hooks(txn, before_commit_update_hook);

        if (txn->old_tuple != NULL) {
#ifndef NDEBUG
                void *ka, *kb;
		ka = tuple_field(txn->tuple, txn->index->key_position);
                kb = tuple_field(txn->old_tuple, txn->index->key_position);
		int kal, kab;
		kal = load_varint32(&ka);
		kab = load_varint32(&kb);
		assert(kal == kab && memcmp(ka, kb, kal) == 0);
#endif
		lock_tuple(txn, txn->old_tuple);
        } else {
		/*
		 * if tuple doesn't exist insert GHOST tuple in indeces
		 * in order to avoid race condition
		 * ref count will be incr in commit
		 */

		foreach_index(txn->n, index)
			index_replace(index, txn->tuple);

                lock_tuple(txn, txn->tuple);
		txn->tuple->flags |= GHOST;
        }

        return -1;
}

static void
commit_replace(struct box_txn *txn)
{
        int tuples_affected = 1;

        if (txn->old_tuple != NULL) {
		foreach_index(txn->n, index)
			index_replace(index, txn->tuple);

		tuple_ref(txn->old_tuple, -1);
        }

	txn->tuple->flags &= ~GHOST;
	tuple_ref(txn->tuple, +1);

        if (!(txn->flags & BOX_QUIET) && !txn->in_recover) {
                add_iov_dup(&tuples_affected, sizeof(uint32_t));

                if (txn->flags & BOX_RETURN_TUPLE)
                        tuple_add_iov(txn, txn->tuple);
        }
}

static void
rollback_replace(struct box_txn *txn)
{
	say_debug("rollback_replace: txn->tuple:%p", txn->tuple);

        if (txn->tuple && txn->tuple->flags & GHOST) {
		foreach_index(txn->n, index)
			index_remove(index, txn->tuple);
        }
}


static int __noinline__
prepare_update_fields(struct box_txn *txn, struct tbuf *data, bool old_format)
{
        struct tbuf **fields;
        void *field;
        int i;
	void *key;
	u32 op_cnt;

	if (!old_format) {
		u32 key_len = read_u32(data);
		if (key_len != 1)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "key must be single valued");
	}
	key = read_field(data);
	op_cnt = read_u32(data);

	if(op_cnt > 128)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "too many ops");
	if(op_cnt == 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "no ops");
        if(key == NULL)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "invalid key");

        txn->old_tuple = index_find(txn->index, 1, key);
        if (txn->old_tuple == NULL) {
                if (!txn->in_recover) {
                        int tuples_affected = 0;
                        add_iov_dup(&tuples_affected, sizeof(uint32_t));
                }
                return ERR_CODE_OK;
        }

	lock_tuple(txn, txn->old_tuple);

	fields = palloc(fiber->pool, (txn->old_tuple->cardinality + 1) * sizeof(struct tbuf *));
	memset(fields, 0, (txn->old_tuple->cardinality + 1) * sizeof(struct tbuf *));

        for (i = 0, field = (uint8_t *)txn->old_tuple->data;
	     i < txn->old_tuple->cardinality;
	     i++)
	{
		fields[i] = tbuf_alloc(fiber->pool);

		u32 field_size = load_varint32(&field);
		tbuf_append(fields[i], field, field_size);
		field += field_size;
        }

	if (old_format) {
		while (op_cnt-- > 0) {
			u8 field_no;
			void *new_field;
			u32 and, xor;
			i32 add;
			u32 new_field_size;

			field_no = read_u8(data);
			new_field = read_field(data);
			and = read_u32(data);
			xor = read_u32(data);
			add = read_u32(data);

			foreach_index(txn->n, index) {
				if(index->key_position == field_no)
					box_raise(ERR_CODE_ILLEGAL_PARAMS, "update of indexed field");
			}

			if(field_no >= txn->old_tuple->cardinality)
				box_raise(ERR_CODE_ILLEGAL_PARAMS, "update of field beyond tuple cardinality");

			struct tbuf *sptr_field = fields[field_no];

			new_field_size = load_varint32(&new_field);
			if (new_field_size) {
				if(and != 0 || xor != 0 || add != 0)
					box_raise(ERR_CODE_ILLEGAL_PARAMS, "and or xor or add != 0");
				tbuf_ensure(sptr_field, new_field_size);
				sptr_field->len = new_field_size;
				memcpy(sptr_field->data, new_field, new_field_size);
			} else {
				uint32_t *num;
				if(sptr_field->len != 4)
					box_raise(ERR_CODE_ILLEGAL_PARAMS, "num op on field with length != 4");
				num = (uint32_t *)sptr_field->data; /* FIXME: align && endianes */

				*num &= and;
				*num ^= xor;
				*num += add;
			}
		}
	} else {
		while (op_cnt-- > 0) {
			u8 op;
			u32 field_no, arg_size;
			void *arg;

			field_no = read_u32(data);

			foreach_index(txn->n, index) {
				if(index->key_position == field_no)
					box_raise(ERR_CODE_ILLEGAL_PARAMS, "update of indexed field");
			}

			if(field_no >= txn->old_tuple->cardinality)
				box_raise(ERR_CODE_ILLEGAL_PARAMS, "update of field beyond tuple cardinality");

			struct tbuf *sptr_field = fields[field_no];

			op = read_u8(data);
			if (op > 4)
				box_raise(ERR_CODE_ILLEGAL_PARAMS, "op is not 0, 1, 2, 3 or 4");
			arg = read_field(data);
			arg_size = load_varint32(&arg);

			if (op == 0) {
				tbuf_ensure(sptr_field, arg_size);
				sptr_field->len = arg_size;
				memcpy(sptr_field->data, arg, arg_size);
			} else {
				if (sptr_field->len != 4)
					box_raise(ERR_CODE_ILLEGAL_PARAMS, "num op on field with length != 4");
				if (arg_size != 4)
					box_raise(ERR_CODE_ILLEGAL_PARAMS, "num op with arg not u32");

				switch(op) {
				case 1:
					*(i32 *)sptr_field->data += *(i32 *)arg;
					break;
				case 2:
					*(u32 *)sptr_field->data &= *(u32 *)arg;
					break;
				case 3:
					*(u32 *)sptr_field->data ^= *(u32 *)arg;
					break;
				case 4:
					*(u32 *)sptr_field->data |= *(u32 *)arg;
					break;
				}
			}
		}
	}

        if(data->len != 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "can't unpack request");

        size_t bsize = 0;
        for (int i = 0; i < txn->old_tuple->cardinality; i++)
		bsize += fields[i]->len + varint32_sizeof(fields[i]->len);
        txn->tuple = tuple_alloc(bsize);
	tuple_txn_ref(txn, txn->tuple);
        txn->tuple->bsize = bsize;
        txn->tuple->cardinality = txn->old_tuple->cardinality;

        uint8_t *p = txn->tuple->data;
        for (int i = 0; i < txn->old_tuple->cardinality; i++) {
		p = save_varint32(p, fields[i]->len);
                memcpy(p, fields[i]->data, fields[i]->len);
                p += fields[i]->len;
        }

        run_hooks(txn, before_commit_update_hook);

	if(data->len != 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "can't unpack request");
        return -1;
}

static void
tuple_add_iov(struct box_txn *txn, struct box_tuple *tuple)
{
        tuple_txn_ref(txn, tuple);

	if (txn->old_format) {
                add_iov_dup(&tuple->bsize, sizeof(uint16_t));
                add_iov_dup(&tuple->cardinality, sizeof(uint8_t));
                add_iov(tuple->data, tuple->bsize);
        } else
                add_iov(&tuple->bsize,
			tuple->bsize +
			field_sizeof(struct box_tuple, bsize) +
			field_sizeof(struct box_tuple, cardinality));
}


static int __noinline__
process_select(struct box_txn *txn, u32 limit, u32 offset, struct tbuf *data, bool old_format)
{
        struct box_tuple *tuple;
        uint32_t *found;
	u32 count = read_u32(data);

	found = palloc(fiber->pool, sizeof(*found));
	add_iov(found, sizeof(*found));
	*found = 0;

        for (u32 i = 0; i < count; i++) {
		if (!old_format) {
			u32 key_len = read_u32(data);
			if (key_len != 1)
				box_raise(ERR_CODE_ILLEGAL_PARAMS, "key must be single valued");
		}
		void *key = read_field(data);
		tuple = index_find(txn->index, 1, key);
                if (tuple == NULL || tuple->flags & GHOST)
                        continue;

		if (offset > 0) {
			offset--;
			continue;
		}

                (*found)++;
                tuple_add_iov(txn, tuple);

		if (--limit == 0)
			break;
        }

	if(data->len != 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "can't unpack request");

        return ERR_CODE_OK;
}


static int __noinline__
prepare_delete(struct box_txn *txn, void *key)
{
        txn->old_tuple = index_find(txn->index, 1, key);

        if(txn->old_tuple == NULL) {
                if (!txn->in_recover) {
                        u32 tuples_affected = 0;
                        add_iov_dup(&tuples_affected, sizeof(tuples_affected));
                }
                return ERR_CODE_OK;
        } else {
		tuple_txn_ref(txn, txn->old_tuple);
	}

        lock_tuple(txn, txn->old_tuple);
        return -1;
}


static void
commit_delete(struct box_txn *txn)
{
        if (!(txn->flags & BOX_QUIET) && !txn->in_recover) {
                int tuples_affected = 1;
                add_iov_dup(&tuples_affected, sizeof(tuples_affected));
        }

	foreach_index(txn->n, index)
		index_remove(index, txn->old_tuple);
        tuple_ref(txn->old_tuple, -1);

        return;
}

struct box_txn *
txn_alloc(u32 flags)
{
	struct box_txn *txn = palloc(fiber->pool, sizeof(*txn));
	memset(txn, 0, sizeof(*txn));
	txn->ref_tuples = tbuf_alloc(fiber->pool);
	txn->flags |= flags; /* note - select will overwrite this flags */
	return txn;
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

        say_debug("box_commit(op:%s)", messages_strs[txn->op]);

	unlock_tuples(txn);

        if (txn->op == DELETE)
                commit_delete(txn);
        else
                commit_replace(txn);
}

static void
txn_abort(struct box_txn *txn)
{
        if (txn->op == 0)
		return;
        say_debug("box_rollback(op:%s)", messages_strs[txn->op]);

	unlock_tuples(txn);

	if (txn->op == DELETE)
                return;

        if (txn->op == INSERT)
                rollback_replace(txn);
}

static bool
op_is_select(u32 op)
{
	return  op == SELECT || op == SELECT_LIMIT;
}

u32
box_dispach(struct box_txn *txn, enum box_mode mode, u32 op, struct tbuf *data)
{
        u32 cardinality;
        int ret_code;
	void *data__data = data->data;
	u32 data__len = data->len;
	int saved_iov_cnt = fiber->iov_cnt;
	ev_tstamp start = ev_now(), stop;

	if ((ret_code = setjmp(fiber->exc)) != 0)
		goto abort;

	say_debug("box_dispach(%i)", op);

	if (!txn->in_recover) {
                if (!op_is_select(op) && (mode == RO || !box_updates_allowed)) {
                        say_error("can't process %i command on RO port", op);
                        return ERR_CODE_NONMASTER;
                }

		fiber_register_cleanup((void *)txn_cleanup, txn);
	}

	txn->op = op;
	txn->n = read_u32(data);
	txn->index = &namespace[txn->n].index[0];

	if(!namespace[txn->n].enabled) {
		say_warn("namespace %i is not enabled", txn->n);
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "namespace is not enabled");
	}

	txn->namespace = &namespace[txn->n];

	void *key;
	u32 key_len;

        switch (op) {
        case INSERT:
                txn->flags = read_u32(data);
		cardinality = read_u32(data);
		if (namespace[txn->n].cardinality > 0 && namespace[txn->n].cardinality != cardinality)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "tuple cardinality must match namespace cardinality");
		if (!txn->in_recover && txn->n == 3 && cardinality != 14)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "tuple cardinality must match namespace cardinality");
                ret_code = prepare_replace(txn, cardinality, data);
		stat_collect(messages_strs[op], 1);
                break;

	case DELETE:
		key_len = read_u32(data);
		if (key_len != 1)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "key must be single valued");

		key = read_field(data);
		if(data->len != 0)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "can't unpack request");

		ret_code = prepare_delete(txn, key);
		stat_collect(messages_strs[op], 1);
                break;

	case SELECT: {
		u32 i = read_u32(data);
		u32 offset = read_u32(data);
		u32 limit = read_u32(data);

		if (i > MAX_IDX)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "index too big");
		txn->index = &namespace[txn->n].index[i];
		if (txn->index->key_position < 0)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "index is invalid");

		stat_collect(messages_strs[op], 1);
                return process_select(txn, limit, offset, data, false);
	}

        case UPDATE_FIELDS:
		txn->flags = read_u32(data);
		stat_collect(messages_strs[op], 1);
                ret_code = prepare_update_fields(txn, data, false);
                break;

	default:
                say_error("silverbox_dispach: unsupported command = %"PRIi32"", op);
                return ERR_CODE_ILLEGAL_PARAMS;
        }

	if (ret_code == -1) {
                if (!txn->in_recover) {
			struct tbuf *t = tbuf_alloc(fiber->pool);
			tbuf_append(t, &op, sizeof(op));
			tbuf_append(t, data__data, data__len);

			if (!wal_write_v04(recovery_state, op, data__data, data__len)) {
				ret_code = ERR_CODE_UNKNOWN_ERROR;
				goto abort;
			}
		}
		txn_commit(txn);

		stop = ev_now();
		if (stop - start > cfg.too_long_threshold)
			say_warn("too long %s: %.3f sec", messages_strs[op], stop - start);
		return 0;
        }


	return ret_code;

abort:
	fiber->iov_cnt = saved_iov_cnt;
	txn_abort(txn);
	return ret_code;
}


static int
box_xlog_sprint(struct tbuf *buf, const struct tbuf *t)
{
	struct row_v04 *row = row_v04(t);

	struct tbuf *b = palloc(fiber->pool, sizeof(*b));
	b->data = row->data;
	b->len = row->len;
	u32 op = row->type;

        u32 n, key_len;
        void *key;
        u32 cardinality, field_no;
        u32 flags;
        u32 op_cnt;

	tbuf_printf(buf, "lsn:%"PRIi64" ", row->lsn);

	say_debug("b->len:%"PRIu32, b->len);
        n = read_u32(b);

        tbuf_printf(buf, "%s ", messages_strs[op]);
        tbuf_printf(buf, "n:%i ", n);

        switch (op) {
        case INSERT:
                flags = read_u32(b);
		cardinality = read_u32(b);
		if (b->len != valid_tuple(b, cardinality)) abort();
                tuple_print(buf, cardinality, b->data);
                break;

	case DELETE:
		key_len = read_u32(b);
		key = read_field(b);
		if (b->len != 0) abort();
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
			switch(op) {
			case 0: tbuf_printf(buf, "set "); break;
			case 1: tbuf_printf(buf, "add "); break;
			case 2: tbuf_printf(buf, "and "); break;
			case 3: tbuf_printf(buf, "xor "); break;
			case 4: tbuf_printf(buf, "or "); break;
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
box_snap_reader(FILE * f, struct palloc_pool *pool)
{
	struct tbuf *row = tbuf_alloc(pool);
	const int header_size = sizeof(*box_snap_row(row));

	tbuf_reserve(row, header_size);
	if (fread(row->data, header_size, 1, f) != 1)
		return NULL;

	tbuf_reserve(row, box_snap_row(row)->data_size);
	if (fread(box_snap_row(row)->data, box_snap_row(row)->data_size, 1, f) != 1)
		return NULL;

	return row;
}


static int
snap_apply(struct recovery_state *r __unused__, const struct tbuf *t)
{
	struct box_snap_row *row = box_snap_row(t);
        struct box_txn *txn = txn_alloc(0);
	txn->in_recover = true;
        txn->n = row->namespace;

	if (txn->n == 25)
		return 0;

	if (!namespace[txn->n].enabled) {
		say_error("namespace %i is not configured", txn->n);
		return -1;
	}
	txn->index = &namespace[txn->n].index[0];
	assert(txn->index->key_position >= 0);

	struct tbuf *b = palloc(fiber->pool, sizeof(*b));
	b->data = row->data;
	b->len = row->data_size;

        if (prepare_replace(txn, row->tuple_size, b) != -1) {
                say_error("unable prepare");
                return -1;
        }

        txn->op = INSERT;
	txn_commit(txn);
	txn_cleanup(txn);
	return 0;
}

static int
xlog_apply(struct recovery_state *r __unused__, const struct tbuf *t)
{
	struct row_v04 *row = row_v04(t);
        struct box_txn *txn = txn_alloc(0);
	txn->in_recover = true;

	assert(row->lsn > confirmed_lsn(r));

	struct tbuf *b = palloc(fiber->pool, sizeof(*b));
	b->data = row->data;
	b->len = row->len;

	if (box_dispach(txn, RW, row->type, b) != 0)
		return -1;

	txn_cleanup(txn);
	return 0;
}

static int
snap_print(struct recovery_state *r __unused__, const struct tbuf *t)
{
	struct tbuf *out = tbuf_alloc(t->pool);
	struct box_snap_row *row = box_snap_row(t);

	tuple_print(out, row->tuple_size , row->data);
	printf("n:%i %*s\n", row->namespace, (int)out->len, (char *)out->data);
	return 0;
}

static int
xlog_print(struct recovery_state *r __unused__, const struct tbuf *t)
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
		panic("at least one namespace should be configured");

	for (int i = 0; i < nelem(namespace); i++) {
		if (cfg.namespace[i] == NULL)
			break;

		namespace[i].enabled = !!cfg.namespace[i]->enabled;

		if (!namespace[i].enabled)
			continue;

		namespace[i].cardinality = cfg.namespace[i]->cardinality;
		int estimated_rows = cfg.namespace[i]->estimated_rows;

		for (int j = 0; j < nelem(namespace[i].index); j++) {
			if (cfg.namespace[i]->index[j] == NULL)
				break;

			namespace[i].index[j].key_position = cfg.namespace[i]->index[j]->key_position;
			if (namespace[i].index[j].key_position == -1)
				continue;


			if (strcmp(cfg.namespace[i]->index[j]->type, "NUM") == 0) {
				namespace[i].index[j].find = index_find_hash_num;
				namespace[i].index[j].remove = index_remove_hash_num;
				namespace[i].index[j].replace = index_replace_hash_num;
				namespace[i].index[j].namespace = &namespace[i];
				namespace[i].index[j].type = INDEX_NUM;
				namespace[i].index[j].map.int_map = kh_init(int2ptr_map, NULL);
				if (estimated_rows > 0)
					kh_resize(int2ptr_map, namespace[i].index[j].map.int_map, estimated_rows);
			} else if (strcmp(cfg.namespace[i]->index[j]->type, "STR") == 0) {
				namespace[i].index[j].find = index_find_hash_str;
				namespace[i].index[j].remove = index_remove_hash_str;
				namespace[i].index[j].replace = index_replace_hash_str;
				namespace[i].index[j].namespace = &namespace[i];
				namespace[i].index[j].type = INDEX_STR;
				namespace[i].index[j].map.str_map = kh_init(lstr2ptr_map, NULL);
				if (estimated_rows > 0)
					kh_resize(lstr2ptr_map, namespace[i].index[j].map.str_map, estimated_rows);
			} else {
				say_warn("unknown index type `%s'", cfg.namespace[i]->index[j]->type);
				continue;
			}
		}
		namespace[i].enabled = true;
		namespace[i].n = i;
		say_info("namespace %i successfully configured", i);
	}
}


static u32
box_process_ro(u32 op, struct tbuf *request_data)
{
	return box_dispach(txn_alloc(0), RO, op, request_data);
}

static u32
box_process(u32 op, struct tbuf *request_data)
{
	return box_dispach(txn_alloc(0), RW, op, request_data);
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
		set_proc_title("memcached:%s%s pri:%i adm:%i",
			       buf, custom_proc_title,
			       cfg.primary_port, cfg.admin_port);
	else
		set_proc_title("box:%s%s pri:%i sec:%i adm:%i",
			       buf, custom_proc_title,
			       cfg.primary_port, cfg.secondary_port, cfg.admin_port);
}

static void
box_bound_to_primary(void *data __unused__)
{
	recover_finalize(recovery_state);

	if (cfg.remote_hot_standby) {
		say_info("starting remote hot standby");
		status = palloc(eter_pool, 64);
		snprintf(status, 64, "hot_standby/%s:%i%s", cfg.wal_feeder_ipaddr, cfg.wal_feeder_port, custom_proc_title);
		recover_follow_remote(recovery_state, cfg.wal_feeder_ipaddr, cfg.wal_feeder_port);

		title("hot_standby/%s:%i", cfg.wal_feeder_ipaddr, cfg.wal_feeder_port);
	} else {
		say_info("I am primary");
		status = "primary";
		box_updates_allowed = true;
		title("primary");
	}
}

static void
memcached_bound_to_primary(void *data __unused__)
{
	box_bound_to_primary(NULL);

	if (!cfg.remote_hot_standby) {
		struct fiber *expire = fiber_create("memecached_expire", -1, -1, memcached_expire, NULL);
		if (expire == NULL)
			panic("can't stared expire fiber");
		fiber_call(expire);
	}
}


void
mod_init(void)
{
	for (int i = 0; i < nelem(namespace); i++) {
		namespace[i].enabled = false;
		for (int j = 0; j < MAX_IDX; j++)
			namespace[i].index[j].key_position = -1;
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
	}

	title("loading");

	if (cfg.remote_hot_standby) {
		if (cfg.wal_feeder_ipaddr == NULL || cfg.wal_feeder_port == 0)
			panic("wal_feeder_ipaddr & wal_feeder_port must be provided in remote_hot_standby mode");
	}

	recovery_state = recover_init(cfg.snap_dir, cfg.wal_dir,
				      box_snap_reader, snap_apply, xlog_apply,
				      cfg.rows_per_wal, cfg.wal_fsync_delay, cfg.snap_io_rate_limit,
				      cfg.wal_writer_inbox_size, init_storage ? RECOVER_READONLY : 0, NULL);

	custom_init(); /* initialize hashes _after_ starting wal writer */

	if (init_storage)
		return;

	recover(recovery_state, 0);

	title("orphan");

	if (cfg.local_hot_standby) {
		say_info("starting local hot standby");
		recover_follow(recovery_state, cfg.wal_dir_rescan_delay);
		status = "hot_standby/local";
		title("hot_standby/local");
	}

	if (cfg.memcached != 0) {
		int n = cfg.memcached_namespace > 0 ? cfg.memcached_namespace : MEMCACHED_NAMESPACE;
		memcached_index = &namespace[n].index[0];
		memcached_index->key_position = 0;
		memcached_index->type = INDEX_STR;

		fiber_server(tcp_server, cfg.primary_port, memcached_handler, NULL, memcached_bound_to_primary);
	} else {
		if (cfg.secondary_port != 0)
			fiber_server(tcp_server, cfg.secondary_port, iproto_interact, box_process_ro, NULL);

		if (cfg.primary_port != 0)
			fiber_server(tcp_server, cfg.primary_port, iproto_interact, box_process, box_bound_to_primary);
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
	struct tbuf *row = tbuf_alloc(fiber->pool);
	struct box_snap_row header;
	struct box_tuple *tuple;
        khiter_t k;

	for(uint32_t n = 0; n < nelem(namespace); ++n) {
                if (!namespace[n].enabled)
			continue;

		assoc_foreach(namespace[n].index[0].map.int_map, k) {
			tuple = kh_value(namespace[n].index[0].map.int_map, k);

			if (tuple->flags & GHOST) // do not save fictive rows
				continue;

			header.namespace = n;
			header.tuple_size = tuple->cardinality;
			header.data_size = tuple->bsize;

			tbuf_reset(row);
			tbuf_append(row, &header, sizeof(header));
			tbuf_append(row, tuple->data, tuple->bsize);

			snapshot_write_row(i, row);
		}
        }
}

void
mod_info(struct tbuf *out)
{
	tbuf_printf(out, "info:\n");
	tbuf_printf(out, "  version: \"%s\"\r\n", tarantool_version());
	tbuf_printf(out, "  uptime: %i\r\n", (int)tarantool_uptime());
	tbuf_printf(out, "  pid: %i\r\n", getpid());
	tbuf_printf(out, "  wal_writer_pid: %"PRIi64"\r\n", (i64)wal_writer(recovery_state)->pid);
	tbuf_printf(out, "  lsn: %"PRIi64"\r\n", confirmed_lsn(recovery_state));
	tbuf_printf(out, "  status: %s\r\n", status);
}
