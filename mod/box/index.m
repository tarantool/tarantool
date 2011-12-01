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
 * Compare index_tree elements only by fields defined in
 * index->field_cmp_order.
 * Return:
 *      Common meaning:
 *              < 0  - a is smaller than b
 *              == 0 - a is equal to b
 *              > 0  - a is greater than b
 */
static int
index_tree_el_unique_cmp(struct index_tree_el *elem_a,
			 struct index_tree_el *elem_b,
			 Index *index)
{
	int r = 0;
	for (i32 i = 0, end = index->key.part_count; i < end; ++i) {
		r = field_compare(&elem_a->key[i], &elem_b->key[i],
				  index->key.parts[i].type);
		if (r != 0)
			break;
	}
	return r;
}

static int
index_tree_el_cmp(struct index_tree_el *elem_a, struct index_tree_el *elem_b,
		  Index *index)
{
	int r = index_tree_el_unique_cmp(elem_a, elem_b, index);
	if (r == 0 && elem_a->tuple && elem_b->tuple)
		r = (elem_a->tuple < elem_b->tuple ?
		     -1 : elem_a->tuple > elem_b->tuple);
	return r;
}

static size_t
index_tree_size(Index *index)
{
	return index->idx.tree->size;
}

static struct box_tuple *
index_hash_find_by_tuple(Index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key.parts[0].fieldno);
	if (key == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, self->key.parts[0].fieldno);

	return self->find(self, key);
}

size_t
index_hash_size(Index *index)
{
	/* All mh_* structures have the same elem layout */
	return mh_size(index->idx.hash);
}

static struct box_tuple *
index_iterator_next_equal(Index *index __attribute__((unused)))
{
	return NULL;
}

static struct box_tuple *
index_min_max_unsupported(Index *index __attribute__((unused)))
{
	tnt_raise(ClientError, :ER_UNSUPPORTED);
	return NULL;
}

static struct box_tuple *
index_iterator_first_equal(Index *index)
{
	index->iterator.next_equal = index_iterator_next_equal;
	return index->iterator.next(index);
}

struct box_tuple *
index_hash_iterator_next(Index *index)
{
	struct iterator *it = &index->iterator;
	while (it->h_iter != mh_end(index->idx.hash)) {
		if (mh_exist(index->idx.hash, it->h_iter))
			return mh_value(index->idx.hash, it->h_iter++);
		it->h_iter++;
	}
	return NULL;
}

void
index_hash_iterator_init(Index *index,
			 int cardinality, void *key)
{
	struct iterator *it = &index->iterator;
	it->next = index_hash_iterator_next;
	it->next_equal = index_iterator_first_equal;

	if (cardinality == 0 && key == NULL) {

		it->h_iter = mh_begin(index->idx.hash);
	} else if (index->key.parts[0].type == NUM) {
		u32 key_size = load_varint32(&key);
		u32 num = *(u32 *)key;

		if (key_size != 4)
			tnt_raise(IllegalParams, :"key is not u32");

		it->h_iter = mh_i32ptr_get(index->idx.int_hash, num);
	} else if (index->key.parts[0].type == NUM64) {
		u32 key_size = load_varint32(&key);
		u64 num = *(u64 *)key;

		if (key_size != 8)
			tnt_raise(IllegalParams, :"key is not u64");

		it->h_iter = mh_i64ptr_get(index->idx.int64_hash, num);
	} else {
		it->h_iter = mh_lstrptr_get(index->idx.str_hash, key);
	}
}


static struct box_tuple *
index_hash_num_find(Index *self, void *key)
{
	struct box_tuple *ret = NULL;
	u32 key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	mh_int_t k = mh_i32ptr_get(self->idx.int_hash, num);
	if (k != mh_end(self->idx.int_hash))
		ret = mh_value(self->idx.int_hash, k);
#ifdef DEBUG
	say_debug("index_hash_num_find(self:%p, key:%i) = %p", self, num, ret);
#endif
	return ret;
}

static struct box_tuple *
index_hash_num64_find(Index *self, void *key)
{
	struct box_tuple *ret = NULL;
	u32 key_size = load_varint32(&key);
	u64 num = *(u64 *)key;

	if (key_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	mh_int_t k = mh_i64ptr_get(self->idx.int64_hash, num);
	if (k != mh_end(self->idx.int64_hash))
		ret = mh_value(self->idx.int64_hash, k);
#ifdef DEBUG
	say_debug("index_hash_num64_find(self:%p, key:%"PRIu64") = %p", self, num, ret);
#endif
	return ret;
}

static struct box_tuple *
index_hash_str_find(Index *self, void *key)
{
	struct box_tuple *ret = NULL;

	mh_int_t k = mh_lstrptr_get(self->idx.str_hash, key);
	if (k != mh_end(self->idx.str_hash))
		ret = mh_value(self->idx.str_hash, k);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_hash_str_find(self:%p, key:(%i)'%.*s') = %p", self, size, size, (u8 *)key,
		  ret);
#endif
	return ret;
}

void
index_tree_el_init(struct index_tree_el *elem,
		   Index *index, struct box_tuple *tuple)
{
	void *tuple_data = tuple->data;

	for (i32 i = 0; i < index->key.max_fieldno; ++i) {
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

		u32 key_field_no = index->key.cmp_order[i];

		if (key_field_no == -1)
			continue;

		if (index->key.parts[key_field_no].type == NUM) {
			if (f.len != 4)
				tnt_raise(IllegalParams, :"key is not u32");
		} else if (index->key.parts[key_field_no].type == NUM64 && f.len != 8) {
				tnt_raise(IllegalParams, :"key is not u64");
		}

		elem->key[key_field_no] = f;
	}
	elem->tuple = tuple;
}

void
init_search_pattern(Index *index, int key_cardinality, void *key)
{
	struct index_tree_el *pattern = index->position[POS_READ];
	void *key_field = key;

	assert(key_cardinality <= index->key.part_count);

	for (i32 i = 0; i < index->key.part_count; ++i)
		pattern->key[i] = ASTERISK;
	for (int i = 0; i < key_cardinality; i++) {
		u32 len;

		len = pattern->key[i].len = load_varint32(&key_field);
		if (index->key.parts[i].type == NUM) {
			if (len != 4)
				tnt_raise(IllegalParams, :"key is not u32");
		} else if (index->key.parts[i].type == NUM64 && len != 8) {
				tnt_raise(IllegalParams, :"key is not u64");
		}
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
index_tree_find(Index *self, void *key)
{
	init_search_pattern(self, 1, key);
	struct index_tree_el *elem = self->position[POS_READ];
	elem = sptree_str_t_find(self->idx.tree, elem);
	if (elem != NULL)
		return elem->tuple;
	return NULL;
}

static struct box_tuple *
index_tree_min(Index *index)
{
	struct index_tree_el *elem = sptree_str_t_first(index->idx.tree);
	if (elem != NULL)
		return elem->tuple;
	return NULL;
}

static struct box_tuple *
index_tree_max(Index *index)
{
	struct index_tree_el *elem = sptree_str_t_last(index->idx.tree);
	if (elem != NULL)
		return elem->tuple;
	return NULL;
}

static struct box_tuple *
index_tree_find_by_tuple(Index *self, struct box_tuple *tuple)
{
	struct index_tree_el *elem = self->position[POS_WRITE];

	index_tree_el_init(elem, self, tuple);

	elem = sptree_str_t_find(self->idx.tree, elem);
	if (elem != NULL)
		return elem->tuple;
	return NULL;
}

static void
index_hash_num_remove(Index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key.parts[0].fieldno);
	unsigned int key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	mh_int_t k = mh_i32ptr_get(self->idx.int_hash, num);
	if (k != mh_end(self->idx.int_hash))
		mh_i32ptr_del(self->idx.int_hash, k);
#ifdef DEBUG
	say_debug("index_hash_num_remove(self:%p, key:%i)", self, num);
#endif
}

static void
index_hash_num64_remove(Index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key.parts[0].fieldno);
	unsigned int key_size = load_varint32(&key);
	u64 num = *(u64 *)key;

	if (key_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	mh_int_t k = mh_i64ptr_get(self->idx.int64_hash, num);
	if (k != mh_end(self->idx.int64_hash))
		mh_i64ptr_del(self->idx.int64_hash, k);
#ifdef DEBUG
	say_debug("index_hash_num64_remove(self:%p, key:%"PRIu64")", self, num);
#endif
}

static void
index_hash_str_remove(Index *self, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key.parts[0].fieldno);

	mh_int_t k = mh_lstrptr_get(self->idx.str_hash, key);
	if (k != mh_end(self->idx.str_hash))
		mh_lstrptr_del(self->idx.str_hash, k);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_hash_str_remove(self:%p, key:'%.*s')", self, size, (u8 *)key);
#endif
}

static void
index_tree_remove(Index *self, struct box_tuple *tuple)
{
	struct index_tree_el *elem = self->position[POS_WRITE];
	index_tree_el_init(elem, self, tuple);
	sptree_str_t_delete(self->idx.tree, elem);
}

static void
index_hash_num_replace(Index *self,
		       struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key.parts[0].fieldno);
	u32 key_size = load_varint32(&key);
	u32 num = *(u32 *)key;

	if (key_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	if (old_tuple != NULL) {
		void *old_key = tuple_field(old_tuple, self->key.parts[0].fieldno);
		load_varint32(&old_key);
		u32 old_num = *(u32 *)old_key;
		mh_int_t k = mh_i32ptr_get(self->idx.int_hash, old_num);
		if (k != mh_end(self->idx.int_hash))
			mh_i32ptr_del(self->idx.int_hash, k);
	}

	mh_i32ptr_put(self->idx.int_hash, num, tuple, NULL);

#ifdef DEBUG
	say_debug("index_hash_num_replace(self:%p, old_tuple:%p, tuple:%p) key:%i", self, old_tuple,
		  tuple, num);
#endif
}

static void
index_hash_num64_replace(Index *self,
			 struct box_tuple *old_tuple,
			 struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key.parts[0].fieldno);
	u32 key_size = load_varint32(&key);
	u64 num = *(u64 *)key;

	if (key_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	if (old_tuple != NULL) {
		void *old_key = tuple_field(old_tuple, self->key.parts[0].fieldno);
		load_varint32(&old_key);
		u64 old_num = *(u64 *)old_key;
		mh_int_t k = mh_i64ptr_get(self->idx.int64_hash, old_num);
		if (k != mh_end(self->idx.int64_hash))
			mh_i64ptr_del(self->idx.int64_hash, k);
	}

	mh_i64ptr_put(self->idx.int64_hash, num, tuple, NULL);
#ifdef DEBUG
	say_debug("index_hash_num64_replace(self:%p, old_tuple:%p, tuple:%p) key:%"PRIu64, self, old_tuple,
		  tuple, num);
#endif
}

static void
index_hash_str_replace(Index *self,
		       struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	void *key = tuple_field(tuple, self->key.parts[0].fieldno);

	if (key == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, self->key.parts[0].fieldno);

	if (old_tuple != NULL) {
		void *old_key = tuple_field(old_tuple, self->key.parts[0].fieldno);
		mh_int_t k = mh_lstrptr_get(self->idx.str_hash, old_key);
		if (k != mh_end(self->idx.str_hash))
			mh_lstrptr_del(self->idx.str_hash, k);
	}

	mh_lstrptr_put(self->idx.str_hash, key, tuple, NULL);
#ifdef DEBUG
	u32 size = load_varint32(&key);
	say_debug("index_hash_str_replace(self:%p, old_tuple:%p, tuple:%p) key:'%.*s'", self,
		  old_tuple, tuple, size, (u8 *)key);
#endif
}

static void
index_tree_replace(Index *self,
		   struct box_tuple *old_tuple, struct box_tuple *tuple)
{
	if (tuple->cardinality < self->key.max_fieldno)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, self->key.max_fieldno);

	struct index_tree_el *elem = self->position[POS_WRITE];

	if (old_tuple) {
		index_tree_el_init(elem, self, old_tuple);
		sptree_str_t_delete(self->idx.tree, elem);
	}
	index_tree_el_init(elem, self, tuple);
	sptree_str_t_insert(self->idx.tree, elem);
}

struct box_tuple *
index_tree_iterator_next_equal(Index *self)
{
	struct iterator *it = &self->iterator;
	struct index_tree_el *elem =
		sptree_str_t_iterator_next(it->t_iter);

	if (elem != NULL && index_tree_el_unique_cmp(self->position[POS_READ],
						     elem, self) == 0)
		return elem->tuple;

	return NULL;
}

static struct box_tuple *
index_tree_iterator_next(Index *self)
{
	struct index_tree_el *elem =
		sptree_str_t_iterator_next(self->iterator.t_iter);

	return elem ? elem->tuple : NULL;
}

void
index_tree_iterator_init(Index *index,
			 int cardinality, void *key)
{
	struct iterator *it = &index->iterator;
	it->next = index_tree_iterator_next;
	if (index->unique && cardinality == index->key.part_count)
		it->next_equal = index_iterator_first_equal;
	else
		it->next_equal = index_tree_iterator_next_equal;
	init_search_pattern(index, cardinality, key);
	sptree_str_t_iterator_init_set(index->idx.tree,
				       &it->t_iter,
				       index->position[POS_READ]);
}

void
validate_indexes(struct box_txn *txn)
{
	if (space[txn->n].index[1] != nil) {	/* there is more than one index */
		foreach_index(txn->n, index) {
			for (u32 f = 0; f < index->key.part_count; ++f) {
				if (index->key.parts[f].fieldno >= txn->tuple->cardinality)
					tnt_raise(IllegalParams, :"tuple must have all indexed fields");

				if (index->key.parts[f].type == STRING)
					continue;

				void *field = tuple_field(txn->tuple, index->key.parts[f].fieldno);
				u32 len = load_varint32(&field);

				if (index->key.parts[f].type == NUM && len != sizeof(u32))
					tnt_raise(IllegalParams, :"field must be NUM");

				if (index->key.parts[f].type == NUM64 && len != sizeof(u64))
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
build_index(Index *pk, Index *index)
{
	u32 n_tuples = pk->size(pk);
	u32 estimated_tuples = n_tuples * 1.2;

	struct index_tree_el *elem = NULL;
	if (n_tuples) {
		/*
		 * Allocate a little extra to avoid
		 * unnecessary realloc() when more data is
		 * inserted.
		*/
		size_t sz = estimated_tuples * INDEX_TREE_EL_SIZE(index);
		elem = malloc(sz);
		if (elem == NULL)
			panic("malloc(): failed to allocate %"PRI_SZ" bytes", sz);
	}
	struct index_tree_el *m;
	u32 i = 0;

	pk->iterator_init(pk, 0, NULL);
	struct box_tuple *tuple;
	while ((tuple = pk->iterator.next(pk))) {

		m = (struct index_tree_el *)
			((char *)elem + i * INDEX_TREE_EL_SIZE(index));

		index_tree_el_init(m, index, tuple);
		++i;
	}

	if (n_tuples)
		say_info("Sorting %"PRIu32 " keys in index %" PRIu32 "...", n_tuples, index->n);

	/* If n_tuples == 0 then estimated_tuples = 0, elem == NULL, tree is empty */
	sptree_str_t_init(index->idx.tree, INDEX_TREE_EL_SIZE(index),
			  elem, n_tuples, estimated_tuples,
			  (void*) (index->unique ? index_tree_el_unique_cmp :
			  index_tree_el_cmp), index);
	index->enabled = true;
}

void
build_indexes(void)
{
	for (u32 n = 0; n < BOX_SPACE_MAX; ++n) {
		if (space[n].enabled == false)
			continue;
		/* A shortcut to avoid unnecessary log messages. */
		if (space[n].index[1] == nil)
			continue; /* no secondary keys */
		say_info("Building secondary keys in space %" PRIu32 "...", n);
		Index *pk = space[n].index[0];
		for (u32 idx = 1;; idx++) {
			Index *index = space[n].index[idx];
			if (index == nil)
				break;

			if (index->type != TREE)
				continue;
			assert(index->enabled == false);

			build_index(pk, index);
		}
		say_info("Space %"PRIu32": done", n);
	}
}

static void
index_hash_num(Index *index, struct space *space)
{
	index->type = HASH;
	index->space = space;
	index->size = index_hash_size;
	index->find = index_hash_num_find;
	index->min = index_min_max_unsupported;
	index->max = index_min_max_unsupported;
	index->find_by_tuple = index_hash_find_by_tuple;
	index->remove = index_hash_num_remove;
	index->replace = index_hash_num_replace;
	index->iterator_init = index_hash_iterator_init;
	index->idx.int_hash = mh_i32ptr_init();
}

static void
index_hash_num_free(Index *index)
{
	mh_i32ptr_destroy(index->idx.int_hash);
}

static void
index_hash_num64(Index *index, struct space *space)
{
	index->type = HASH;
	index->space = space;
	index->size = index_hash_size;
	index->find = index_hash_num64_find;
	index->min = index_min_max_unsupported;
	index->max = index_min_max_unsupported;
	index->find_by_tuple = index_hash_find_by_tuple;
	index->remove = index_hash_num64_remove;
	index->replace = index_hash_num64_replace;
	index->iterator_init = index_hash_iterator_init;
	index->idx.int64_hash = mh_i64ptr_init();
}

static void
index_hash_num64_free(Index *index)
{
	mh_i64ptr_destroy(index->idx.int64_hash);
}

static void
index_hash_str(Index *index, struct space *space)
{
	index->type = HASH;
	index->space = space;
	index->size = index_hash_size;
	index->find = index_hash_str_find;
	index->min = index_min_max_unsupported;
	index->max = index_min_max_unsupported;
	index->find_by_tuple = index_hash_find_by_tuple;
	index->remove = index_hash_str_remove;
	index->replace = index_hash_str_replace;
	index->iterator_init = index_hash_iterator_init;
	index->idx.str_hash = mh_lstrptr_init();
}

static void
index_hash_str_free(Index *index)
{
	mh_lstrptr_destroy(index->idx.str_hash);
}

static void
index_tree(Index *index, struct space *space)
{
	index->type = TREE;
	index->space = space;
	index->size = index_tree_size;
	index->find = index_tree_find;
	index->min = index_tree_min;
	index->max = index_tree_max;
	index->find_by_tuple = index_tree_find_by_tuple;
	index->remove = index_tree_remove;
	index->replace = index_tree_replace;
	index->iterator_init = index_tree_iterator_init;
	index->idx.tree = calloc(1, sizeof(*index->idx.tree));
}

static void
index_tree_free(Index *index)
{
	free(index->idx.tree);
}

@implementation Index

- (void) init: (struct space *) space_arg
{
	space = space_arg;
	switch (type) {
	case HASH:
		/* Hash index, check key type. */
		/* Hash index has single-field key*/
		enabled = true;
		switch (key.parts[0].type) {
		case NUM:
			/* 32-bit integer hash */
			index_hash_num(self, space);
			break;
		case NUM64:
			/* 64-bit integer hash */
			index_hash_num64(self, space);
			break;
		case STRING:
			/* string hash */
			index_hash_str(self, space);
			break;
		default:
			panic("unsupported field type in index");
			break;
		}
		break;
	case TREE:
		/* tree index */
		enabled = false;
		index_tree(self, space);
		if (n == 0) {/* pk */
			sptree_str_t_init(idx.tree,
					  INDEX_TREE_EL_SIZE(self),
					  NULL, 0, 0,
					  (void *)index_tree_el_unique_cmp, self);
			enabled = true;
		}
		break;
	default:
		panic("unsupported index type");
		break;
	}
	position[POS_READ] = palloc(eter_pool, INDEX_TREE_EL_SIZE(self));
	position[POS_WRITE] = palloc(eter_pool, INDEX_TREE_EL_SIZE(self));
}

- (void) free
{
	switch (type) {
	case HASH:
		switch (key.parts[0].type) {
		case NUM:
			index_hash_num_free(self);
			break;
		case NUM64:
			index_hash_num64_free(self);
			break;
		case STRING:
			index_hash_str_free(self);
			break;
		default:
			break;
		}
		break;
	case TREE:
		index_tree_free(self);
		break;
	default:
		break;
	}
	sfree(key.parts);
	sfree(key.cmp_order);

	[super free];
}

@end

