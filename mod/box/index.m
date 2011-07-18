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



static struct box_tuple *
index_find_hash_by_tuple(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	if (key == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, self->key_field->fieldno);

	return self->find(self, key);
}

static struct box_tuple *
index_find_hash_num(struct index *self, void *key)
{
	struct box_tuple *ret = NULL;
	u32 key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	assoc_find(int_ptr_map, self->idx.int_hash, num, ret);
#ifdef DEBUG
	say_debug("index_find_hash_num(self:%p, key:%i) = %p", self, num, ret);
#endif
	return ret;
}

static struct box_tuple *
index_find_hash_num64(struct index *self, void *key)
{
	struct box_tuple *ret = NULL;
	u32 key_size = load_varint32(&key);
	u64 num = *(u64 *)key;

	if (key_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	assoc_find(int64_ptr_map, self->idx.int64_hash, num, ret);
#ifdef DEBUG
	say_debug("index_find_hash_num(self:%p, key:%"PRIu64") = %p", self, num, ret);
#endif
	return ret;
}

static struct box_tuple *
index_find_hash_str(struct index *self, void *key)
{
	struct box_tuple *ret = NULL;

	assoc_find(lstr_ptr_map, self->idx.str_hash, key, ret);
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

struct tree_index_member *
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

	member = sptree_str_t_find(self->idx.tree, member);
	if (member != NULL)
		return member->tuple;

	return NULL;
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
		tnt_raise(IllegalParams, :"key is not u32");
	assoc_delete(int_ptr_map, self->idx.int_hash, num);
#ifdef DEBUG
	say_debug("index_remove_hash_num(self:%p, key:%i)", self, num);
#endif
}

static void
index_remove_hash_num64(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	unsigned int key_size = load_varint32(&key);
	u64 num = *(u64 *)key;

	if (key_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");
	assoc_delete(int64_ptr_map, self->idx.int64_hash, num);
#ifdef DEBUG
	say_debug("index_remove_hash_num(self:%p, key:%"PRIu64")", self, num);
#endif
}

static void
index_remove_hash_str(struct index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key_field->fieldno);
	assoc_delete(lstr_ptr_map, self->idx.str_hash, key);
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
	say_debug("index_replace_hash_num(self:%p, old_tuple:%p, tuple:%p) key:%i", self, old_tuple,
		  tuple, num);
#endif
}

static void
index_replace_hash_num64(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
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
	say_debug("index_replace_hash_num(self:%p, old_tuple:%p, tuple:%p) key:%"PRIu64, self, old_tuple,
		  tuple, num);
#endif
}

static void
index_replace_hash_str(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
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
	say_debug("index_replace_hash_str(self:%p, old_tuple:%p, tuple:%p) key:'%.*s'", self,
		  old_tuple, tuple, size, (u8 *)key);
#endif
}

static void
index_replace_tree_str(struct index *self, struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	if (tuple->cardinality < self->field_cmp_order_cnt)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, self->field_cmp_order_cnt);

	struct tree_index_member *member = tuple2tree_index_member(self, tuple, NULL);

	if (old_tuple)
		index_remove_tree_str(self, old_tuple);
	sptree_str_t_insert(self->idx.tree, member);
}

void
index_iterator_init_tree_str(struct index *self, struct tree_index_member *pattern)
{
	sptree_str_t_iterator_init_set(self->idx.tree,
				       (struct sptree_str_t_iterator **)&self->iterator, pattern);
}

struct box_tuple *
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

void
validate_indexes(struct box_txn *txn)
{
	if (namespace[txn->n].index[1].key_cardinality != 0) {	/* there is more then one index */
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

static void
index_hash_num(struct index *index, struct namespace *namespace, size_t estimated_rows)
{
	index->type = HASH;
	index->namespace = namespace;
	index->find = index_find_hash_num;
	index->find_by_tuple = index_find_hash_by_tuple;
	index->remove = index_remove_hash_num;
	index->replace = index_replace_hash_num;
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
index_hash_num64(struct index *index, struct namespace *namespace, size_t estimated_rows)
{
	index->type = HASH;
	index->namespace = namespace;
	index->find = index_find_hash_num64;
	index->find_by_tuple = index_find_hash_by_tuple;
	index->remove = index_remove_hash_num64;
	index->replace = index_replace_hash_num64;
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
index_hash_str(struct index *index, struct namespace *namespace, size_t estimated_rows)
{
	index->type = HASH;
	index->namespace = namespace;
	index->find = index_find_hash_str;
	index->find_by_tuple = index_find_hash_by_tuple;
	index->remove = index_remove_hash_str;
	index->replace = index_replace_hash_str;
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
index_tree(struct index *index, struct namespace *namespace,
	   size_t estimated_rows __attribute__((unused)))
{
	index->type = TREE;
	index->namespace = namespace;
	index->find = index_find_tree;
	index->find_by_tuple = index_find_tree_by_tuple;
	index->remove = index_remove_tree_str;
	index->replace = index_replace_tree_str;
	index->iterator_init = index_iterator_init_tree_str;
	index->iterator_next = index_iterator_next_tree_str;
	index->idx.tree = palloc(eter_pool, sizeof(*index->idx.tree));
}

static void
index_tree_free(struct index *index)
{
	palloc_destroy_pool((void*)index->idx.tree);
}

void
index_init(struct index *index, struct namespace *namespace, size_t estimated_rows)
{
	switch (index->type) {
	case HASH:
		/* Hash index, check key type. */
		/* Hash index has single-field key*/
		index->enabled = true;
		switch (index->key_field[0].type) {
		case NUM:
			/* 32-bit integer hash */
			index_hash_num(index, namespace, estimated_rows);
			break;
		case NUM64:
			/* 64-bit integer hash */
			index_hash_num64(index, namespace, estimated_rows);
			break;
		case STRING:
			/* string hash */
			index_hash_str(index, namespace, estimated_rows);
			break;
		default:
			panic("unsupported field type in index");
			break;
		}
		break;
	case TREE:
		/* tree index */
		index->enabled = false;
		index_tree(index, namespace, estimated_rows);
		break;
	default:
		panic("unsupported index type");
		break;
	}
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
