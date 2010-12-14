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
// #include <third_party/sptree.h>

#include <mod/silverbox/box.h>

bool box_updates_allowed = false;
static char *status = "unknown";

static int stat_base;
STRS(messages, MESSAGES);

const int MEMCACHED_NAMESPACE = 23;
static char *custom_proc_title;

const struct field ASTERISK = {
	.len = UINT32_MAX,
	{
		.data_ptr = NULL,
	}
};

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
			say_warn("%s/%s", error_codes_strs[(n)], err);	\
		raise(n, err);						\
	})

static void
run_hooks(struct box_txn *txn, box_hook_t * hook)
{
	if (hook != NULL) {
		for (int i = 0; hook[i] != NULL; i++) {
			int result = (*hook[i]) (txn);
			if (result != ERR_CODE_OK)
				box_raise(result, "hook returned error");
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

bool
field_is_num(void *field)
{
	u32 len = load_varint32(&field);

	if (len == sizeof(u32))
		return true;

	return false;
}

#define IS_ASTERISK(f) ((f)->len == ASTERISK.len && (f)->data_ptr == ASTERISK.data_ptr)
static i8
field_compare(struct field *f1, struct field *f2, enum field_data_type type)
{
	i8 r;

	if (IS_ASTERISK(f1) || IS_ASTERISK(f2))
		r = 0;
	else {
		if (type == NUM) {
			assert(f1->len == f2->len);
			assert(f1->len == sizeof(f1->u32));

			r = f1->u32 >f2->u32 ? 1 : f1->u32 == f2->u32 ? 0 : -1;
		} else {
			i32 cmp;
			void *f1_data, *f2_data;

			f1_data = f1->len <= sizeof(f1->data) ? f1->data : f1->data_ptr;
			f2_data = f2->len <= sizeof(f2->data) ? f2->data : f2->data_ptr;

			cmp = memcmp(f1_data, f2_data, MIN(f1->len, f2->len));

			if (cmp > 0)
				r = 1;
			else if (cmp < 0)
				r = -1;
			else if (f1->len == f2->len)
				r = 0;
			else if (f1->len > f2->len)
				r = 1;
			else
				r = -1;
		}
	}

	return r;
}

/*
 * Compare index_tree_members only by fields defined in index->field_cmp_order.
 * Return:
 *      Common meaning:
 *              < 0  - a is smaler than b
 *              == 0 - a is equal to b
 *              > 0  - a is greater than b
 *      Custom treatment (by absolute value):
 *              1 - differ in some key field
 *              2 - one tuple is a search pattern
 *              3 - differ in pointers
 */
static int
tree_index_member_compare(struct tree_index_member *member_a, struct tree_index_member *member_b,
			  struct index *index)
{
	i8 r = 0;

	for (i32 i = 0, end = index->key_cardinality; i < end; ++i) {
		r = field_compare(&member_a->key[i], &member_b->key[i], index->key_field[i].type);

		if (r != 0)
			break;
	}

	if (r != 0)
		return r;

	if (member_a->tuple == NULL)
		return -2;

	if (member_b->tuple == NULL)
		return 2;

	if (index->unique == false) {
		if (member_a->tuple > member_b->tuple)
			return 3;
		else if (member_a->tuple < member_b->tuple)
			return -3;
	}

	return 0;
}

#define foreach_index(n, index_var)					\
	for (struct index *index_var = namespace[(n)].index;		\
	     index_var->key_cardinality != 0;				\
	     index_var++)						\
		if (index_var->enabled)

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
		box_raise(ERR_CODE_MEMORY_ISSUE, "can't allocate tuple");

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

static struct box_tuple *
index_find_hash_by_tuple(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	if (key == NULL)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "invalid tuple, can't find key");
	return self->find(self, key);
}

static struct box_tuple *
index_find_hash_num(struct index *self, void *key)
{
	struct box_tuple *ret = NULL;
	u32 key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_size != 4)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "key is not u32");

	assoc_find(int2ptr_map, self->idx.int_hash, num, ret);
#ifdef DEBUG
	say_debug("index_find_hash_num(self:%p, key:%i) = %p", self, num, ret);
#endif
	return ret;
}

static struct box_tuple *
index_find_hash_str(struct index *self, void *key)
{
	struct box_tuple *ret = NULL;

	assoc_find(lstr2ptr_map, self->idx.str_hash, key, ret);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_find_hash_str(self:%p, key:(%i)'%.*s') = %p", self, size, size, (u8 *)key,
		  ret);
#endif
	return ret;
}

static struct tree_index_member *
tuple2tree_index_member(struct index *index,
			struct box_tuple *tuple, struct tree_index_member **member_p)
{
	struct tree_index_member *member;
	void *tuple_data = tuple->data;

	if (member_p == NULL || *member_p == NULL)
		member = palloc(fiber->pool, SIZEOF_TREE_INDEX_MEMBER(index));
	else
		member = *member_p;

	for (i32 i = 0; i < index->field_cmp_order_cnt; ++i) {
		struct field f;

		if (i < tuple->cardinality) {
			f.len = load_varint32(&tuple_data);
			if (f.len <= sizeof(f.data)) {
				memset(f.data, 0, sizeof(f.data));
				memcpy(f.data, tuple_data, f.len);
			} else
				f.data_ptr = tuple_data;
			tuple_data += f.len;
		} else
			f = ASTERISK;

		if (index->field_cmp_order[i] == -1)
			continue;

		member->key[index->field_cmp_order[i]] = f;
	}

	member->tuple = tuple;

	if (member_p)
		*member_p = member;

	return member;
}

static struct tree_index_member *
alloc_search_pattern(struct index *index, int key_cardinality, void *key)
{
	struct tree_index_member *pattern = index->search_pattern;
	void *key_field = key;

	assert(key_cardinality <= index->key_cardinality);

	for (i32 i = 0; i < index->key_cardinality; ++i)
		pattern->key[i] = ASTERISK;
	for (int i = 0; i < key_cardinality; i++) {
		u32 len;

		len = pattern->key[i].len = load_varint32(&key_field);
		if (len <= sizeof(pattern->key[i].data)) {
			memset(pattern->key[i].data, 0, sizeof(pattern->key[i].data));
			memcpy(pattern->key[i].data, key_field, len);
		} else
			pattern->key[i].data_ptr = key_field;

		key_field += len;
	}

	pattern->tuple = NULL;

	return pattern;
}

static struct box_tuple *
index_find_tree(struct index *self, void *key)
{
	struct tree_index_member *member = (struct tree_index_member *)key;

	return sptree_str_t_find(self->idx.tree, member);
}

static struct box_tuple *
index_find_tree_by_tuple(struct index *self, struct box_tuple *tuple)
{
	struct tree_index_member *member = tuple2tree_index_member(self, tuple, NULL);

	return self->find(self, member);
}

static void
index_remove_hash_num(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	unsigned int key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_size != 4)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "key is not u32");
	assoc_delete(int2ptr_map, self->idx.int_hash, num);
#ifdef DEBUG
	say_debug("index_remove_hash_num(self:%p, key:%i)", self, num);
#endif
}

static void
index_remove_hash_str(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	assoc_delete(lstr2ptr_map, self->idx.str_hash, key);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_remove_hash_str(self:%p, key:'%.*s')", self, size, (u8 *)key);
#endif
}

static void
index_remove_tree_str(struct index *self, struct box_tuple *tuple)
{
	struct tree_index_member *member = tuple2tree_index_member(self, tuple, NULL);
	sptree_str_t_delete(self->idx.tree, member);
}

static void
index_replace_hash_num(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	u32 key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (old_tuple != NULL) {
		void *old_key = tuple_field(old_tuple, self->key_field->fieldno);
		load_varint32(&old_key);
		u32 old_num = *(u32 *)old_key;
		assoc_delete(int2ptr_map, self->idx.int_hash, old_num);
	}

	if (key_size != 4)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "key is not u32");
	assoc_replace(int2ptr_map, self->idx.int_hash, num, tuple);
#ifdef DEBUG
	say_debug("index_replace_hash_num(self:%p, old_tuple:%p, tuple:%p) key:%i", self, old_tuple,
		  tuple, num);
#endif
}

static void
index_replace_hash_str(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);

	if (old_tuple != NULL) {
		void *old_key = tuple_field(old_tuple, self->key_field->fieldno);
		assoc_delete(lstr2ptr_map, self->idx.str_hash, old_key);
	}

	assoc_replace(lstr2ptr_map, self->idx.str_hash, key, tuple);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_replace_hash_str(self:%p, old_tuple:%p, tuple:%p) key:'%.*s'", self,
		  old_tuple, tuple, size, (u8 *)key);
#endif
}

static void
index_replace_tree_str(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	struct tree_index_member *member = tuple2tree_index_member(self, tuple, NULL);

	if (old_tuple)
		index_remove_tree_str(self, old_tuple);
	sptree_str_t_insert(self->idx.tree, member);
}

static void
index_iterator_init_tree_str(struct index *self, struct tree_index_member *pattern)
{
	sptree_str_t_iterator_init_set(self->idx.tree,
				       (struct sptree_str_t_iterator **)&self->iterator, pattern);
}

static struct box_tuple *
index_iterator_next_tree_str(struct index *self, struct tree_index_member *pattern)
{
	struct tree_index_member *member =
		sptree_str_t_iterator_next((struct sptree_str_t_iterator *)self->iterator);

	if (member == NULL)
		return NULL;

	i32 r = tree_index_member_compare(pattern, member, self);
	if (r == -2)
		return member->tuple;

	return NULL;
}

static void
validate_indeces(struct box_txn *txn)
{
	if (namespace[txn->n].index[1].key_cardinality != 0) {	/* there is more then one index */
		foreach_index(txn->n, index) {
			for (u32 f = 0; f < index->key_cardinality; ++f) {
				void *field;

				if (index->key_field[f].type == STR)
					continue;

				field = tuple_field(txn->tuple, index->key_field[f].fieldno);
				if (!field_is_num(field))
					box_raise(ERR_CODE_ILLEGAL_PARAMS, "field must be NUM");
			}
			if (index->type == TREE && index->unique == false)
				/* Don't check non unique indexes */
				continue;

			struct box_tuple *tuple = index->find_by_tuple(index, txn->tuple);

			if (tuple != NULL && tuple != txn->old_tuple)
				box_raise(ERR_CODE_INDEX_VIOLATION, "unique index violation");
		}
	}
}

static int __noinline__
prepare_replace(struct box_txn *txn, size_t cardinality, struct tbuf *data)
{
	assert(data != NULL);
	if (cardinality == 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "cardinality can't be equal to 0");
	if (data->len == 0 || data->len != valid_tuple(data, cardinality))
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "tuple encoding error");

	txn->tuple = tuple_alloc(data->len);
	tuple_txn_ref(txn, txn->tuple);
	txn->tuple->cardinality = cardinality;
	memcpy(txn->tuple->data, data->data, data->len);

	txn->old_tuple = txn->index->find_by_tuple(txn->index, txn->tuple);

	if (txn->old_tuple != NULL)
		tuple_txn_ref(txn, txn->old_tuple);

	if (txn->flags & BOX_ADD && txn->old_tuple != NULL)
		box_raise(ERR_CODE_NODE_FOUND, "tuple found");

	if (txn->flags & BOX_REPLACE && txn->old_tuple == NULL)
		box_raise(ERR_CODE_NODE_NOT_FOUND, "tuple not found");

	validate_indeces(txn);
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
		/*
		 * if tuple doesn't exist insert GHOST tuple in indeces
		 * in order to avoid race condition
		 * ref count will be incr in commit
		 */

		foreach_index(txn->n, index)
			index->replace(index, NULL, txn->tuple);

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
			index->replace(index, txn->old_tuple, txn->tuple);

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
			index->remove(index, txn->tuple);
	}
}

static void
do_field_arith(u8 op, struct tbuf *field, void *arg, u32 arg_size)
{
	if (field->len != 4)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "num op on field with length != 4");
	if (arg_size != 4)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "num op with arg not u32");

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
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "do_field_splice: bad args");

	offset_size = load_varint32(&offset_field);
	if (offset_size == 0)
		noffset = 0;
	else if (offset_size == sizeof(offset)) {
		offset = pick_u32(offset_field, &offset_field);
		if (offset < 0) {
			if (field->len < -offset)
				box_raise(ERR_CODE_ILLEGAL_PARAMS,
					  "do_field_splice: noffset is negative");
			noffset = offset + field->len;
		} else
			noffset = offset;
	} else
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "do_field_splice: bad size of offset field");
	if (noffset > field->len)
		noffset = field->len;

	length_size = load_varint32(&length_field);
	if (length_size == 0)
		nlength = field->len - noffset;
	else if (length_size == sizeof(length)) {
		if (offset_size == 0)
			box_raise(ERR_CODE_ILLEGAL_PARAMS,
				  "do_field_splice: offset field is empty but length is not");

		length = pick_u32(length_field, &length_field);
		if (length < 0) {
			if ((field->len - noffset) < -length)
				nlength = 0;
			else
				nlength = length + field->len - noffset;
		} else
			nlength = length;
	} else
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "do_field_splice: bad size of length field");
	if (nlength > (field->len - noffset))
		nlength = field->len - noffset;

	list_size = load_varint32(&list_field);
	if (list_size > 0 && length_size == 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS,
			  "do_field_splice: length field is empty but list is not");
	if (list_size > (UINT32_MAX - (field->len - nlength)))
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "do_field_splice: list_size is too long");

	say_debug("do_field_splice: noffset = %i, nlength = %i, list_size = %u",
		  noffset, nlength, list_size);

	new_field->len = 0;
	tbuf_append(new_field, field->data, noffset);
	tbuf_append(new_field, list_field, list_size);
	tbuf_append(new_field, field->data + noffset + nlength, field->len - (noffset + nlength));

	*field = *new_field;
}

static int __noinline__
prepare_update_fields(struct box_txn *txn, struct tbuf *data)
{
	struct tbuf **fields;
	void *field;
	int i;
	void *key;
	u32 op_cnt;

	u32 key_len = read_u32(data);
	if (key_len != 1)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "key must be single valued");
	key = read_field(data);
	op_cnt = read_u32(data);

	if (op_cnt > 128)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "too many ops");
	if (op_cnt == 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "no ops");
	if (key == NULL)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "invalid key");

	txn->old_tuple = txn->index->find(txn->index, key);
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
			box_raise(ERR_CODE_ILLEGAL_PARAMS,
				  "update of field beyond tuple cardinality");

		struct tbuf *sptr_field = fields[field_no];

		op = read_u8(data);
		if (op > 5)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "op is not 0, 1, 2, 3, 4 or 5");
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
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "can't unpack request");

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

	validate_indeces(txn);
	run_hooks(txn, before_commit_update_hook);

	if (data->len != 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "can't unpack request");
	return -1;
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

static int __noinline__
process_select(struct box_txn *txn, u32 limit, u32 offset, struct tbuf *data)
{
	struct box_tuple *tuple;
	uint32_t *found;
	u32 count = read_u32(data);

	found = palloc(fiber->pool, sizeof(*found));
	add_iov(found, sizeof(*found));
	*found = 0;

	if (txn->index->type == TREE) {
		for (u32 i = 0; i < count; i++) {
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

				(*found)++;
				tuple_add_iov(txn, tuple);

				if (--limit == 0)
					break;
			}
			if (limit == 0)
				break;
		}
	} else {
		for (u32 i = 0; i < count; i++) {
			u32 key_len = read_u32(data);
			if (key_len != 1)
				box_raise(ERR_CODE_ILLEGAL_PARAMS,
					  "key must be single valued");
			void *key = read_field(data);
			tuple = txn->index->find(txn->index, key);
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
	}

	if (data->len != 0)
		box_raise(ERR_CODE_ILLEGAL_PARAMS, "can't unpack request");

	return ERR_CODE_OK;
}

static int __noinline__
prepare_delete(struct box_txn *txn, void *key)
{
	txn->old_tuple = txn->index->find(txn->index, key);

	if (txn->old_tuple == NULL) {
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
		index->remove(index, txn->old_tuple);
	tuple_ref(txn->old_tuple, -1);

	return;
}

struct box_txn *
txn_alloc(u32 flags)
{
	struct box_txn *txn = p0alloc(fiber->pool, sizeof(*txn));
	txn->ref_tuples = tbuf_alloc(fiber->pool);
	txn->flags |= flags;	/* note - select will overwrite this flags */
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
	return op == SELECT || op == SELECT_LIMIT;
}

u32
box_dispach(struct box_txn *txn, enum box_mode mode, u16 op, struct tbuf *data)
{
	u32 cardinality;
	int ret_code;
	struct tbuf req = { .data = data->data, .len = data->len };
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

	if (!namespace[txn->n].enabled) {
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
		if (namespace[txn->n].cardinality > 0
		    && namespace[txn->n].cardinality != cardinality)
			box_raise(ERR_CODE_ILLEGAL_PARAMS,
				  "tuple cardinality must match namespace cardinality");
		ret_code = prepare_replace(txn, cardinality, data);
		stat_collect(stat_base, op, 1);
		break;

	case DELETE:
		key_len = read_u32(data);
		if (key_len != 1)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "key must be single valued");

		key = read_field(data);
		if (data->len != 0)
			box_raise(ERR_CODE_ILLEGAL_PARAMS, "can't unpack request");

		ret_code = prepare_delete(txn, key);
		stat_collect(stat_base, op, 1);
		break;

	case SELECT:{
			u32 i = read_u32(data);
			u32 offset = read_u32(data);
			u32 limit = read_u32(data);

			if (i > MAX_IDX)
				box_raise(ERR_CODE_ILLEGAL_PARAMS, "index too big");
			txn->index = &namespace[txn->n].index[i];
			if (txn->index->key_cardinality == 0)
				box_raise(ERR_CODE_ILLEGAL_PARAMS, "index is invalid");

			stat_collect(stat_base, op, 1);
			return process_select(txn, limit, offset, data);
		}

	case UPDATE_FIELDS:
		txn->flags = read_u32(data);
		stat_collect(stat_base, op, 1);
		ret_code = prepare_update_fields(txn, data);
		break;

	default:
		say_error("silverbox_dispach: unsupported command = %" PRIi32 "", op);
		return ERR_CODE_ILLEGAL_PARAMS;
	}

	if (ret_code == -1) {
		if (!txn->in_recover) {
			fiber_peer_name(fiber); /* fill the cookie */
			struct tbuf *t = tbuf_alloc(fiber->pool);
			tbuf_append(t, &op, sizeof(op));
			tbuf_append(t, req.data, req.len);

			i64 lsn = next_lsn(recovery_state, 0);
			if (!wal_write(recovery_state, wal_tag, fiber->cookie, lsn, t)) {
				ret_code = ERR_CODE_UNKNOWN_ERROR;
				goto abort;
			}
			confirm_lsn(recovery_state, lsn);
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

	tbuf_printf(buf, "tm:%.3f t:%"PRIu16 " %s:%d %s n:%i",
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
snap_apply(struct box_txn *txn, struct tbuf *t)
{
	struct box_snap_row *row;

	read_u64(t); /* drop cookie */

	row = box_snap_row(t);
	txn->n = row->namespace;

	if (!namespace[txn->n].enabled) {
		say_error("namespace %i is not configured", txn->n);
		return -1;
	}
	txn->index = &namespace[txn->n].index[0];
	assert(txn->index->key_cardinality > 0);

	struct tbuf *b = palloc(fiber->pool, sizeof(*b));
	b->data = row->data;
	b->len = row->data_size;

	if (prepare_replace(txn, row->tuple_size, b) != -1) {
		say_error("unable prepare");
		return -1;
	}

	txn->op = INSERT;
	txn_commit(txn);
	return 0;
}

static int
wal_apply(struct box_txn *txn, struct tbuf *t)
{
	read_u64(t); /* drop cookie */

	u16 type = read_u16(t);
	if (box_dispach(txn, RW, type, t) != 0)
		return -1;

	txn_cleanup(txn);
	return 0;
}

static int
recover_row(struct recovery_state *r __unused__, struct tbuf *t)
{
	struct box_txn *txn = txn_alloc(0);
	int result = -1;
	txn->in_recover = true;

	/* drop wal header */
	if (tbuf_peek(t, sizeof(struct row_v11)) == NULL)
		return -1;

	u16 tag = read_u16(t);
	if (tag == wal_tag) {
		result = wal_apply(txn, t);
	} else if (tag == snap_tag) {
		result = snap_apply(txn, t);
	} else {
		say_error("unknown row tag: %i", (int)tag);
		return -1;
	}

	txn_cleanup(txn);
	return result;
}


static int
snap_print(struct recovery_state *r __unused__, struct tbuf *t)
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
xlog_print(struct recovery_state *r __unused__, struct tbuf *t)
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

		if (cfg.namespace[i]->index == NULL)
			panic("(namespace = %" PRIu32 ") at least one index must be defined", i);

		for (int j = 0; j < nelem(namespace[i].index); j++) {
			struct index *index = &namespace[i].index[j];
			u32 max_key_fieldno = 0;

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
				if (cfg.namespace[i]->index[j]->key_field[k]->fieldno == -1)
					break;

				index->key_field[k].fieldno =
					cfg.namespace[i]->index[j]->key_field[k]->fieldno;
				if (strcmp(cfg.namespace[i]->index[j]->key_field[k]->type, "NUM") ==
				    0)
					index->key_field[k].type = NUM;
				else if (strcmp(cfg.namespace[i]->index[j]->key_field[k]->type,
						"STR") == 0)
					index->key_field[k].type = STR;
				else
					panic("(namespace = %" PRIu32 " index = %" PRIu32 ") "
					      "unknown field data type: `%s'",
					      i, j, cfg.namespace[i]->index[j]->key_field[k]->type);

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
					      "hash index must have single-filed key", i, j);

				index->enabled = true;
				index->type = HASH;

				if (index->unique == false)
					panic("(namespace = %" PRIu32 " index = %" PRIu32 ") "
					      "hash index must be unique", i, j);

				if (index->key_field->type == NUM) {
					index->find = index_find_hash_num;
					index->find_by_tuple = index_find_hash_by_tuple;
					index->remove = index_remove_hash_num;
					index->replace = index_replace_hash_num;
					index->namespace = &namespace[i];
					index->idx.int_hash = kh_init(int2ptr_map, NULL);

					if (estimated_rows > 0)
						kh_resize(int2ptr_map, index->idx.int_hash,
							  estimated_rows);
				} else {
					index->find = index_find_hash_str;
					index->find_by_tuple = index_find_hash_by_tuple;
					index->remove = index_remove_hash_str;
					index->replace = index_replace_hash_str;
					index->namespace = &namespace[i];
					index->idx.str_hash = kh_init(lstr2ptr_map, NULL);

					if (estimated_rows > 0)
						kh_resize(lstr2ptr_map, index->idx.str_hash,
							  estimated_rows);
				}
			} else if (strcmp(cfg.namespace[i]->index[j]->type, "TREE") == 0) {
				index->enabled = false;
				index->type = TREE;

				index->find = index_find_tree;
				index->find_by_tuple = index_find_tree_by_tuple;
				index->remove = index_remove_tree_str;
				index->replace = index_replace_tree_str;
				index->iterator_init = index_iterator_init_tree_str;
				index->iterator_next = index_iterator_next_tree_str;
				index->namespace = &namespace[i];

				index->idx.tree = palloc(eter_pool, sizeof(*index->idx.tree));
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
			       buf, custom_proc_title, cfg.primary_port, cfg.admin_port);
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
		snprintf(status, 64, "hot_standby/%s:%i%s", cfg.wal_feeder_ipaddr,
			 cfg.wal_feeder_port, custom_proc_title);
		recover_follow_remote(recovery_state, cfg.wal_feeder_ipaddr, cfg.wal_feeder_port,
				      default_remote_row_handler);

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

	struct fiber *expire =
		fiber_create("memecached_expire", -1, -1, memcached_expire, NULL);
	if (expire == NULL)
		panic("can't start the expire fiber");
	fiber_call(expire);
}

static void
build_indexes(void)
{
	for (u32 n = 0; n < nelem(namespace); ++n) {
		u32 n_tuples, estimated_tuples;
		struct tree_index_member *members[nelem(namespace[n].index)] = { NULL };

		if (namespace[n].enabled == false)
			continue;

		n_tuples = kh_size(namespace[n].index[0].idx.hash);
		estimated_tuples = n_tuples * 1.2;

		say_info("build_indexes: n = %" PRIu32 ": build arrays", n);

		khiter_t k;
		u32 i = 0;
		assoc_foreach(namespace[n].index[0].idx.hash, k) {
			for (u32 idx = 0;; idx++) {
				struct index *index = &namespace[n].index[idx];
				struct tree_index_member *member;
				struct tree_index_member *m;

				if (index->key_cardinality == 0)
					break;

				if (index->type != TREE)
					continue;

				member = members[idx];
				if (member == NULL) {
					member = malloc(estimated_tuples *
							SIZEOF_TREE_INDEX_MEMBER(index));
					if (member == NULL)
						panic("build_indexes: malloc failed: %m");

					members[idx] = member;
				}

				m = (struct tree_index_member *)
					((char *)member + i * SIZEOF_TREE_INDEX_MEMBER(index));

				tuple2tree_index_member(index,
							kh_value(namespace[n].index[0].idx.hash,
								 k),
							&m);
			}

			++i;
		}

		say_info("build_indexes: n = %" PRIu32 ": build trees", n);

		for (u32 idx = 0;; idx++) {
			struct index *index = &namespace[n].index[idx];
			struct tree_index_member *member = members[idx];

			if (index->key_cardinality == 0)
				break;

			if (index->type != TREE)
				continue;

			assert(index->enabled == false);

			say_info("build_indexes: n = %" PRIu32 " idx = %" PRIu32 ": build tree", n,
				 idx);

			/* if n_tuples == 0 then estimated_tuples = 0, member == NULL, tree is empty */
			sptree_str_t_init(index->idx.tree,
					  SIZEOF_TREE_INDEX_MEMBER(index),
					  member, n_tuples, estimated_tuples,
					  (void *)tree_index_member_compare, index);
			index->enabled = true;

			say_info("build_indexes: n = %" PRIu32 " idx = %" PRIu32 ": end", n, idx);
		}
	}
}

void
mod_init(void)
{
	stat_base = stat_register(messages_strs, messages_MAX);
	for (int i = 0; i < nelem(namespace); i++) {
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

	title("build_indexes");

	build_indexes();

	title("orphan");

	if (cfg.local_hot_standby) {
		say_info("starting local hot standby");
		recover_follow(recovery_state, cfg.wal_dir_rescan_delay);
		status = "hot_standby/local";
		title("hot_standby/local");
	}

	if (cfg.memcached != 0) {
		fiber_server(tcp_server, cfg.primary_port, memcached_handler, NULL,
			     memcached_bound_to_primary);
	} else {
		if (cfg.secondary_port != 0)
			fiber_server(tcp_server, cfg.secondary_port, iproto_interact,
				     box_process_ro, NULL);

		if (cfg.primary_port != 0)
			fiber_server(tcp_server, cfg.primary_port, iproto_interact, box_process,
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

	for (uint32_t n = 0; n < nelem(namespace); ++n) {
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
	tbuf_printf(out, "info:\n");
	tbuf_printf(out, "  version: \"%s\"\r\n", tarantool_version());
	tbuf_printf(out, "  uptime: %i\r\n", (int)tarantool_uptime());
	tbuf_printf(out, "  pid: %i\r\n", getpid());
	tbuf_printf(out, "  wal_writer_pid: %" PRIi64 "\r\n", (i64)recovery_state->wal_writer->pid);
	tbuf_printf(out, "  lsn: %" PRIi64 "\r\n", recovery_state->confirmed_lsn);
	tbuf_printf(out, "  recovery_lag: %.3f\r\n", recovery_state->recovery_lag);
	tbuf_printf(out, "  recovery_last_update: %.3f\r\n", recovery_state->recovery_last_update_tstamp);
	tbuf_printf(out, "  status: %s\r\n", status);
}

void
mod_exec(char *str __unused__, int len __unused__, struct tbuf *out)
{
	tbuf_printf(out, "unimplemented\r\n");
}
