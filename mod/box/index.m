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

#include <mod/box/box.h>
#include <mod/box/index.h>
#include <mod/box/tuple.h>

const char *field_data_type_strs[] = {"NUM", "NUM64", "STR", "\0"};
const char *index_type_strs[] = { "HASH", "TREE", "\0" };

const struct field ASTERISK = {
	.len = UINT32_MAX,
	{
		.data_ptr = NULL,
	}
};

/* hooks */
typedef int (*box_hook_t) (struct box_txn * txn);



#define IS_ASTERISK(f) ((f)->len == ASTERISK.len && (f)->data_ptr == ASTERISK.data_ptr)

/** Compare two fields of an index key.
 *
 * @retval 0  two fields are equal
 * @retval -1 f2 is less than f1
 * @retval 1 f2 is greater than f1
 */

static i8
field_compare(struct field *f1, struct field *f2, enum field_data_type type)
{
	if (IS_ASTERISK(f1) || IS_ASTERISK(f2))
		return 0;

	if (type == NUM) {
		assert(f1->len == f2->len);
		assert(f1->len == sizeof(f1->u32));

		return f1->u32 >f2->u32 ? 1 : f1->u32 == f2->u32 ? 0 : -1;
	} else if (type == NUM64) {
		assert(f1->len == f2->len);
		assert(f1->len == sizeof(f1->u64));

		return f1->u64 >f2->u64 ? 1 : f1->u64 == f2->u64 ? 0 : -1;
	} else if (type == STRING) {
		i32 cmp;
		void *f1_data, *f2_data;

		f1_data = f1->len <= sizeof(f1->data) ? f1->data : f1->data_ptr;
		f2_data = f2->len <= sizeof(f2->data) ? f2->data : f2->data_ptr;

		cmp = memcmp(f1_data, f2_data, MIN(f1->len, f2->len));

		if (cmp > 0)
			return 1;
		else if (cmp < 0)
			return -1;
		else if (f1->len == f2->len)
			return 0;
		else if (f1->len > f2->len)
			return 1;
		else
			return -1;
	}

	panic("impossible happened");
}


/*
 * Compare index_tree_members only by fields defined in index->field_cmp_order.
 * Return:
 *      Common meaning:
 *              < 0  - a is smaller than b
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

static size_t
index_tree_size(struct index *index)
{
	return index->idx.tree->size;
}

static struct box_tuple *
index_hash_find_by_tuple(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	if (key == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, self->key_field->fieldno);

	return self->find(self, key);
}

size_t
index_hash_size(struct index *index)
{
	/* All kh_* structures have the same member layout */
	return kh_size(index->idx.hash);
}

static struct box_tuple *
index_hash_num_find(struct index *self, void *key)
{
	struct box_tuple *ret = NULL;
	u32 key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	assoc_find(int_ptr_map, self->idx.int_hash, num, ret);
#ifdef DEBUG
	say_debug("index_hash_num_find(self:%p, key:%i) = %p", self, num, ret);
#endif
	return ret;
}

static struct box_tuple *
index_hash_num64_find(struct index *self, void *key)
{
	struct box_tuple *ret = NULL;
	u32 key_size = load_varint32(&key);
	u64 num = *(u64 *)key;

	if (key_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	assoc_find(int64_ptr_map, self->idx.int64_hash, num, ret);
#ifdef DEBUG
	say_debug("index_hash_num64_find(self:%p, key:%"PRIu64") = %p", self, num, ret);
#endif
	return ret;
}

static struct box_tuple *
index_hash_str_find(struct index *self, void *key)
{
	struct box_tuple *ret = NULL;

	assoc_find(lstr_ptr_map, self->idx.str_hash, key, ret);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_hash_str_find(self:%p, key:(%i)'%.*s') = %p", self, size, size, (u8 *)key,
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
		member = palloc(fiber->gc_pool, SIZEOF_TREE_INDEX_MEMBER(index));
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

void
init_search_pattern(struct index *index, int key_cardinality, void *key)
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
}

static struct box_tuple *
index_tree_find(struct index *self, void *key)
{
	init_search_pattern(self, 1, key);
	struct tree_index_member *member = self->search_pattern;
	member->tuple = (void *)1; /* HACK: otherwise tree_index_member_compare returns -2,
				      which is plain wrong */
	member = sptree_str_t_find(self->idx.tree, member);
	if (member != NULL)
		return member->tuple;
	return NULL;
}

static struct box_tuple *
index_tree_find_by_tuple(struct index *self, struct box_tuple *tuple)
{
	struct tree_index_member *member = tuple2tree_index_member(self, tuple, NULL);

	member = sptree_str_t_find(self->idx.tree, member);
	if (member != NULL)
		return member->tuple;
	return NULL;
}

static void
index_hash_num_remove(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	unsigned int key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");
	assoc_delete(int_ptr_map, self->idx.int_hash, num);
#ifdef DEBUG
	say_debug("index_hash_num_remove(self:%p, key:%i)", self, num);
#endif
}

static void
index_hash_num64_remove(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	unsigned int key_size = load_varint32(&key);
	u64 num = *(u64 *)key;

	if (key_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");
	assoc_delete(int64_ptr_map, self->idx.int64_hash, num);
#ifdef DEBUG
	say_debug("index_hash_num64_remove(self:%p, key:%"PRIu64")", self, num);
#endif
}

static void
index_hash_str_remove(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	assoc_delete(lstr_ptr_map, self->idx.str_hash, key);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_hash_str_remove(self:%p, key:'%.*s')", self, size, (u8 *)key);
#endif
}

static void
index_tree_remove(struct index *self, struct box_tuple *tuple)
{
	struct tree_index_member *member = tuple2tree_index_member(self, tuple, NULL);
	sptree_str_t_delete(self->idx.tree, member);
}

static void
index_hash_num_replace(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	u32 key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	if (old_tuple != NULL) {
		void *old_key = tuple_field(old_tuple, self->key_field->fieldno);
		load_varint32(&old_key);
		u32 old_num = *(u32 *)old_key;
		assoc_delete(int_ptr_map, self->idx.int_hash, old_num);
	}

	assoc_replace(int_ptr_map, self->idx.int_hash, num, tuple);
#ifdef DEBUG
	say_debug("index_hash_num_replace(self:%p, old_tuple:%p, tuple:%p) key:%i", self, old_tuple,
		  tuple, num);
#endif
}

static void
index_hash_num64_replace(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	u32 key_size = load_varint32(&key);
	u64 num = *(u64 *)key;

	if (key_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	if (old_tuple != NULL) {
		void *old_key = tuple_field(old_tuple, self->key_field->fieldno);
		load_varint32(&old_key);
		u64 old_num = *(u64 *)old_key;
		assoc_delete(int64_ptr_map, self->idx.int64_hash, old_num);
	}

	assoc_replace(int64_ptr_map, self->idx.int64_hash, num, tuple);
#ifdef DEBUG
	say_debug("index_hash_num64_replace(self:%p, old_tuple:%p, tuple:%p) key:%"PRIu64, self, old_tuple,
		  tuple, num);
#endif
}

static void
index_hash_str_replace(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);

	if (key == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, self->key_field->fieldno);

	if (old_tuple != NULL) {
		void *old_key = tuple_field(old_tuple, self->key_field->fieldno);
		assoc_delete(lstr_ptr_map, self->idx.str_hash, old_key);
	}

	assoc_replace(lstr_ptr_map, self->idx.str_hash, key, tuple);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_hash_str_replace(self:%p, old_tuple:%p, tuple:%p) key:'%.*s'", self,
		  old_tuple, tuple, size, (u8 *)key);
#endif
}

static void
index_tree_replace(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	if (tuple->cardinality < self->field_cmp_order_cnt)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, self->field_cmp_order_cnt);

	struct tree_index_member *member = tuple2tree_index_member(self, tuple, NULL);

	if (old_tuple)
		index_tree_remove(self, old_tuple);
	sptree_str_t_insert(self->idx.tree, member);
}

void
index_tree_iterator_init(struct index *index, int cardinality, void *key)
{
	init_search_pattern(index, cardinality, key);
	sptree_str_t_iterator_init_set(index->idx.tree,
				       (struct sptree_str_t_iterator **)&index->iterator, index->search_pattern);
}

struct box_tuple *
index_tree_iterator_next(struct index *self)
{
	struct tree_index_member *member =
		sptree_str_t_iterator_next((struct sptree_str_t_iterator *)self->iterator);

	if (member == NULL)
		return NULL;

	i32 r = tree_index_member_compare(self->search_pattern, member, self);
	if (r == -2)
		return member->tuple;

	return NULL;
}

static struct box_tuple *
index_tree_iterator_next_nocompare(struct index *self)
{
	struct tree_index_member *member =
		sptree_str_t_iterator_next((struct sptree_str_t_iterator *)self->iterator);

	if (member == NULL)
		return NULL;

	return member->tuple;
}

void
validate_indexes(struct box_txn *txn)
{
	if (space[txn->n].index[1].key_cardinality != 0) {	/* there is more than one index */
		foreach_index(txn->n, index) {
			for (u32 f = 0; f < index->key_cardinality; ++f) {
				if (index->key_field[f].fieldno >= txn->tuple->cardinality)
					tnt_raise(IllegalParams, :"tuple must have all indexed fields");

				if (index->key_field[f].type == STRING)
					continue;

				void *field = tuple_field(txn->tuple, index->key_field[f].fieldno);
				u32 len = load_varint32(&field);

				if (index->key_field[f].type == NUM && len != sizeof(u32))
					tnt_raise(IllegalParams, :"field must be NUM");

				if (index->key_field[f].type == NUM64 && len != sizeof(u64))
					tnt_raise(IllegalParams, :"field must be NUM64");
			}
			if (index->type == TREE && index->unique == false)
				/* Don't check non unique indexes */
				continue;

			struct box_tuple *tuple = index->find_by_tuple(index, txn->tuple);

			if (tuple != NULL && tuple != txn->old_tuple)
				tnt_raise(ClientError, :ER_INDEX_VIOLATION);
		}
	}
}


void
build_indexes(void)
{
	for (u32 n = 0; n < BOX_NAMESPACE_MAX; ++n) {
		u32 n_tuples, estimated_tuples;
		struct tree_index_member *members[nelem(space[n].index)] = { NULL };

		if (space[n].enabled == false)
			continue;

		struct index *pk = &space[n].index[0];
		n_tuples = pk->size(pk);
		estimated_tuples = n_tuples * 1.2;

		say_info("build_indexes: n = %" PRIu32 ": build arrays", n);

		for (u32 idx = 0;; idx++) {
			struct index *index = &space[n].index[idx];
			if (index->key_cardinality == 0)
				break;

			if (index->type != TREE || index == pk)
				continue;

			if (members[idx] == NULL) {
				members[idx] = malloc(estimated_tuples *
						      SIZEOF_TREE_INDEX_MEMBER(index));
				if (members[idx] == NULL)
					panic("build_indexes: malloc failed: %m");
			}
		}

		if (pk->type == HASH) {
			khiter_t k;
			u32 i = 0;
			assoc_foreach(space[n].index[0].idx.hash, k) {
				for (u32 idx = 0;; idx++) {
					struct index *index = &space[n].index[idx];
					struct tree_index_member *member = members[idx];
					struct tree_index_member *m;

					if (index->key_cardinality == 0)
						break;

					if (index->type != TREE || index == pk)
						continue;

					m = (struct tree_index_member *)
						((char *)member + i * SIZEOF_TREE_INDEX_MEMBER(index));

					tuple2tree_index_member(index,
								kh_value(space[n].index[0].idx.hash,
									 k),
								&m);
				}
				++i;
			}
		} else {
			pk->iterator_init(pk, 0, NULL);
			struct box_tuple *tuple;
			u32 i = 0;
			while ((tuple = pk->iterator_next_nocompare(pk))) {
				for (u32 idx = 0;; idx++) {
					struct index *index = &space[n].index[idx];
					struct tree_index_member *member = members[idx];
					struct tree_index_member *m;

					if (index->key_cardinality == 0)
						break;

					if (index->type != TREE || index == pk)
						continue;

					m = (struct tree_index_member *)
						((char *)member + i * SIZEOF_TREE_INDEX_MEMBER(index));

					tuple2tree_index_member(index, tuple, &m);
				}
				++i;
			}
		}

		say_info("build_indexes: n = %" PRIu32 ": build trees", n);

		for (u32 idx = 0;; idx++) {
			struct index *index = &space[n].index[idx];
			struct tree_index_member *member = members[idx];

			if (index->key_cardinality == 0)
				break;

			if (index->type != TREE || index == &space[n].index[0])
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

static void
index_hash_num(struct index *index, struct space *space, size_t estimated_rows)
{
	index->type = HASH;
	index->space = space;
	index->size = index_hash_size;
	index->find = index_hash_num_find;
	index->find_by_tuple = index_hash_find_by_tuple;
	index->remove = index_hash_num_remove;
	index->replace = index_hash_num_replace;
	index->idx.int_hash = kh_init(int_ptr_map, NULL);
	if (estimated_rows > 0)
		kh_resize(int_ptr_map, index->idx.int_hash, estimated_rows);
}

static void
index_hash_num_free(struct index *index)
{
	kh_destroy(int_ptr_map, index->idx.int_hash);
}

static void
index_hash_num64(struct index *index, struct space *space, size_t estimated_rows)
{
	index->type = HASH;
	index->space = space;
	index->size = index_hash_size;
	index->find = index_hash_num64_find;
	index->find_by_tuple = index_hash_find_by_tuple;
	index->remove = index_hash_num64_remove;
	index->replace = index_hash_num64_replace;
	index->idx.int64_hash = kh_init(int64_ptr_map, NULL);
	if (estimated_rows > 0)
		kh_resize(int64_ptr_map, index->idx.int64_hash, estimated_rows);
}

static void
index_hash_num64_free(struct index *index)
{
	kh_destroy(int64_ptr_map, index->idx.int64_hash);
}

static void
index_hash_str(struct index *index, struct space *space, size_t estimated_rows)
{
	index->type = HASH;
	index->space = space;
	index->size = index_hash_size;
	index->find = index_hash_str_find;
	index->find_by_tuple = index_hash_find_by_tuple;
	index->remove = index_hash_str_remove;
	index->replace = index_hash_str_replace;
	index->idx.str_hash = kh_init(lstr_ptr_map, NULL);
	if (estimated_rows > 0)
		kh_resize(lstr_ptr_map, index->idx.str_hash, estimated_rows);
}

static void
index_hash_str_free(struct index *index)
{
	kh_destroy(lstr_ptr_map, index->idx.str_hash);
}

static void
index_tree(struct index *index, struct space *space,
	   size_t estimated_rows __attribute__((unused)))
{
	index->type = TREE;
	index->space = space;
	index->size = index_tree_size;
	index->find = index_tree_find;
	index->find_by_tuple = index_tree_find_by_tuple;
	index->remove = index_tree_remove;
	index->replace = index_tree_replace;
	index->iterator_init = index_tree_iterator_init;
	index->iterator_next = index_tree_iterator_next;
	index->iterator_next_nocompare = index_tree_iterator_next_nocompare;
	index->idx.tree = palloc(eter_pool, sizeof(*index->idx.tree));
}

static void
index_tree_free(struct index *index)
{
	(void)index;
	/* sptree_free? */
}

void
index_init(struct index *index, struct space *space, size_t estimated_rows)
{
	switch (index->type) {
	case HASH:
		/* Hash index, check key type. */
		/* Hash index has single-field key*/
		index->enabled = true;
		switch (index->key_field[0].type) {
		case NUM:
			/* 32-bit integer hash */
			index_hash_num(index, space, estimated_rows);
			break;
		case NUM64:
			/* 64-bit integer hash */
			index_hash_num64(index, space, estimated_rows);
			break;
		case STRING:
			/* string hash */
			index_hash_str(index, space, estimated_rows);
			break;
		default:
			panic("unsupported field type in index");
			break;
		}
		break;
	case TREE:
		/* tree index */
		index->enabled = false;
		index_tree(index, space, estimated_rows);
		if (index->n == 0) {/* pk */
			sptree_str_t_init(index->idx.tree,
					  SIZEOF_TREE_INDEX_MEMBER(index),
					  NULL, 0, 0,
					  (void *)tree_index_member_compare, index);
			index->enabled = true;
		}
		break;
	default:
		panic("unsupported index type");
		break;
	}
	index->search_pattern = palloc(eter_pool, SIZEOF_TREE_INDEX_MEMBER(index));
}

void
index_free(struct index *index)
{
	switch (index->type) {
	case HASH:
		switch (index->key_field[0].type) {
		case NUM:
			index_hash_num_free(index);
			break;
		case NUM64:
			index_hash_num64_free(index);
			break;
		case STRING:
			index_hash_str_free(index);
			break;
		default:
			break;
		}
		break;
	case TREE:
		index_tree_free(index);
		break;
	default:
		break;
	}
}
