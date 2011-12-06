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
#include "index.h"
#include "say.h"
#include "tuple.h"
#include "pickle.h"
#include "exception.h"
#include "box.h"
#include "salloc.h"
#include "assoc.h"

const char *field_data_type_strs[] = {"NUM", "NUM64", "STR", "\0"};
const char *index_type_strs[] = { "HASH", "TREE", "\0" };

static struct box_tuple *
iterator_next_equal(struct iterator *it __attribute__((unused)))
{
	return NULL;
}

static struct box_tuple *
iterator_first_equal(struct iterator *it)
{
	it->next_equal = iterator_next_equal;
	return it->next(it);
}

/* {{{ Index -- base class for all indexes. ********************/

@implementation Index

@class Hash32Index;
@class Hash64Index;
@class HashStrIndex;
@class TreeIndex;

+ (Index *) alloc: (enum index_type) type :(struct key *) key
{
	switch (type) {
	case HASH:
		/* Hash index, check key type.
		 * Hash indes always has a single-field key.
		 */
		switch (key->parts[0].type) {
		case NUM:
			return [Hash32Index alloc]; /* 32-bit integer hash */
		case NUM64:
			return [Hash64Index alloc]; /* 64-bit integer hash */
		case STRING:
			return [HashStrIndex alloc]; /* string hash */
		default:
			break;
		}
		break;
	case TREE:
		return [TreeIndex alloc];
	default:
		break;
	}
	panic("unsupported index type");
}

- (id) init: (enum index_type) type_arg :(struct key *) key_arg
	:(struct space *) space_arg :(u32) n_arg;
{
	self = [super init];
	key = *key_arg;
	type = type_arg;
	n = n_arg;
	space = space_arg;
	position = [self allocIterator];
	[self enable];
	return self;
}

- (void) free
{
	sfree(key.parts);
	sfree(key.cmp_order);
	sfree(position);
	[super free];
}

- (void) enable
{
	[self subclassResponsibility: _cmd];
}

- (size_t) size
{
	[self subclassResponsibility: _cmd];
	return 0;
}

- (struct box_tuple *) min
{
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct box_tuple *) max
{
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct box_tuple *) find: (void *) key_arg
{
	(void) key_arg;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct box_tuple *) findBy: (struct box_tuple *) pattern
{
	(void) pattern;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (void) remove: (struct box_tuple *) tuple
{
	(void) tuple;
	[self subclassResponsibility: _cmd];
}

- (void) replace: (struct box_tuple *) old_tuple
	:(struct box_tuple *) new_tuple
{
	(void) old_tuple;
	(void) new_tuple;
	[self subclassResponsibility: _cmd];
}

- (struct iterator *) allocIterator
{
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (void) initIterator: (struct iterator *) iterator
{
	(void) iterator;
	[self subclassResponsibility: _cmd];
}

- (void) initIterator: (struct iterator *) iterator :(void *) key_arg
			:(int) part_count
{
	(void) iterator;
	(void) part_count;
	(void) key_arg;
	[self subclassResponsibility: _cmd];
}
@end

/* }}} */

/* {{{ HashIndex -- base class for all hashes. ********************/

@interface HashIndex: Index
@end

struct hash_iterator {
	struct iterator base; /* Must be the first member. */
	struct mh_i32ptr_t *hash;
	mh_int_t h_pos;
};

static inline struct hash_iterator *
hash_iterator(struct iterator *it)
{
	return (struct hash_iterator *) it;
}

struct box_tuple *
hash_iterator_next(struct iterator *iterator)
{
	assert(iterator->next = hash_iterator_next);

	struct hash_iterator *it = hash_iterator(iterator);

	while (it->h_pos != mh_end(it->hash)) {
		if (mh_exist(it->hash, it->h_pos))
			return mh_value(it->hash, it->h_pos++);
		it->h_pos++;
	}
	return NULL;
}


@implementation HashIndex
- (void) free
{
	[super free];
}

- (struct box_tuple *) min
{
	tnt_raise(ClientError, :ER_UNSUPPORTED);
	return NULL;
}

- (struct box_tuple *) max
{
	tnt_raise(ClientError, :ER_UNSUPPORTED);
	return NULL;
}

- (struct box_tuple *) findBy: (struct box_tuple *) tuple
{
	/* Hash index currently is always single-part. */
	void *field = tuple_field(tuple, key.parts[0].fieldno);
	if (field == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, key.parts[0].fieldno);
	return [self find: field];
}

- (struct iterator *) allocIterator
{
	struct hash_iterator *it = salloc(sizeof(hash_iterator));
	if (it) {
		memset(it, 0, sizeof(struct hash_iterator));
		it->base.next = hash_iterator_next;
	}
	return (struct iterator *) it;
}
@end

/* }}} */

/* {{{ Hash32Index ************************************************/

@interface Hash32Index: HashIndex {
	 struct mh_i32ptr_t *int_hash;
};
@end

@implementation Hash32Index
- (void) free
{
	mh_i32ptr_destroy(int_hash);
	[super free];
}

- (void) enable
{
	enabled = true;
	int_hash = mh_i32ptr_init();
}

- (size_t) size
{
	return mh_size(int_hash);
}

- (struct box_tuple *) find: (void *) field
{
	struct box_tuple *ret = NULL;
	u32 field_size = load_varint32(&field);
	u32 num = *(u32 *)field;

	if (field_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	mh_int_t k = mh_i32ptr_get(int_hash, num);
	if (k != mh_end(int_hash))
		ret = mh_value(int_hash, k);
#ifdef DEBUG
	say_debug("index_hash_num_find(self:%p, key:%i) = %p", self, num, ret);
#endif
	return ret;
}

- (void) remove: (struct box_tuple *) tuple
{
	void *field = tuple_field(tuple, self->key.parts[0].fieldno);
	unsigned int field_size = load_varint32(&field);
	u32 num = *(u32 *)field;

	if (field_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	mh_int_t k = mh_i32ptr_get(int_hash, num);
	if (k != mh_end(int_hash))
		mh_i32ptr_del(int_hash, k);
#ifdef DEBUG
	say_debug("index_hash_num_remove(self:%p, key:%i)", self, num);
#endif
}

- (void) replace: (struct box_tuple *) old_tuple
	:(struct box_tuple *) new_tuple
{
	void *field = tuple_field(new_tuple, key.parts[0].fieldno);
	u32 field_size = load_varint32(&field);
	u32 num = *(u32 *)field;

	if (field_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	if (old_tuple != NULL) {
		void *old_field = tuple_field(old_tuple, key.parts[0].fieldno);
		load_varint32(&old_field);
		u32 old_num = *(u32 *)old_field;
		mh_int_t k = mh_i32ptr_get(int_hash, old_num);
		if (k != mh_end(int_hash))
			mh_i32ptr_del(int_hash, k);
	}

	mh_i32ptr_put(int_hash, num, new_tuple, NULL);

#ifdef DEBUG
	say_debug("index_hash_num_replace(self:%p, old_tuple:%p, new_tuple:%p) key:%i",
		  self, old_tuple, new_tuple, num);
#endif
}

- (void) initIterator: (struct iterator *) iterator
{
	struct hash_iterator *it = hash_iterator(iterator);

	assert(iterator->next = hash_iterator_next);

	it->base.next_equal = 0; /* Should not be used. */
	it->h_pos = mh_begin(int_hash);
	it->hash = int_hash;
}

- (void) initIterator: (struct iterator *) iterator :(void *) key_arg
			:(int) part_count
{
	struct hash_iterator *it = hash_iterator(iterator);

	assert(part_count == 1);
	assert(iterator->next = hash_iterator_next);

	u32 field_size = load_varint32(&key_arg);
	u32 num = *(u32 *)key_arg;

	if (field_size != 4)
		tnt_raise(IllegalParams, :"key is not u32");

	it->base.next_equal = iterator_first_equal;
	it->h_pos = mh_i32ptr_get(int_hash, num);
	it->hash = int_hash;
}
@end

/* }}} */

/* {{{ Hash64Index ************************************************/

@interface Hash64Index: HashIndex {
	struct mh_i64ptr_t *int64_hash;
};
@end

@implementation Hash64Index
- (void) free
{
	mh_i64ptr_destroy(int64_hash);
	[super free];
}

- (void) enable
{
	enabled = true;
	int64_hash = mh_i64ptr_init();
}

- (size_t) size
{
	return mh_size(int64_hash);
}

- (struct box_tuple *) find: (void *) field
{
	struct box_tuple *ret = NULL;
	u32 field_size = load_varint32(&field);
	u64 num = *(u64 *)field;

	if (field_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	mh_int_t k = mh_i64ptr_get(int64_hash, num);
	if (k != mh_end(int64_hash))
		ret = mh_value(int64_hash, k);
#ifdef DEBUG
	say_debug("index_hash_num64_find(self:%p, key:%"PRIu64") = %p", self, num, ret);
#endif
	return ret;
}

- (void) remove: (struct box_tuple *) tuple
{
	void *field = tuple_field(tuple, key.parts[0].fieldno);
	unsigned int field_size = load_varint32(&field);
	u64 num = *(u64 *)field;

	if (field_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	mh_int_t k = mh_i64ptr_get(int64_hash, num);
	if (k != mh_end(int64_hash))
		mh_i64ptr_del(int64_hash, k);
#ifdef DEBUG
	say_debug("index_hash_num64_remove(self:%p, key:%"PRIu64")", self, num);
#endif
}

- (void) replace: (struct box_tuple *) old_tuple
	:(struct box_tuple *) new_tuple
{
	void *field = tuple_field(new_tuple, key.parts[0].fieldno);
	u32 field_size = load_varint32(&field);
	u64 num = *(u64 *)field;

	if (field_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	if (old_tuple != NULL) {
		void *old_field = tuple_field(old_tuple, key.parts[0].fieldno);
		load_varint32(&old_field);
		u64 old_num = *(u64 *)old_field;
		mh_int_t k = mh_i64ptr_get(int64_hash, old_num);
		if (k != mh_end(int64_hash))
			mh_i64ptr_del(int64_hash, k);
	}

	mh_i64ptr_put(int64_hash, num, new_tuple, NULL);
#ifdef DEBUG
	say_debug("index_hash_num64_replace(self:%p, old_tuple:%p, tuple:%p) key:%"PRIu64,
		  self, old_tuple, new_tuple, num);
#endif
}

- (void) initIterator: (struct iterator *) iterator
{
	assert(iterator->next = hash_iterator_next);

	struct hash_iterator *it = hash_iterator(iterator);


	it->base.next_equal = 0; /* Should not be used if not positioned. */
	it->h_pos = mh_begin(int64_hash);
	it->hash = (struct mh_i32ptr_t *) int64_hash;
}

- (void) initIterator: (struct iterator *) iterator :(void *) field
			:(int) part_count
{
	assert(iterator->next = hash_iterator_next);
	assert(part_count == 1);

	struct hash_iterator *it = hash_iterator(iterator);

	u32 field_size = load_varint32(&field);
	u64 num = *(u64 *)field;

	if (field_size != 8)
		tnt_raise(IllegalParams, :"key is not u64");

	it->base.next_equal = iterator_first_equal;
	it->h_pos = mh_i64ptr_get(int64_hash, num);
	it->hash = (struct mh_i32ptr_t *) int64_hash;
}
@end

/* }}} */

/* {{{ HashStrIndex ***********************************************/

@interface HashStrIndex: HashIndex {
	 struct mh_lstrptr_t *str_hash;
};
@end

@implementation HashStrIndex
- (void) free
{
	mh_lstrptr_destroy(str_hash);
	[super free];
}

- (void) enable
{
	enabled = true;
	str_hash = mh_lstrptr_init();
}

- (size_t) size
{
	return mh_size(str_hash);
}

- (struct box_tuple *) find: (void *) field
{
	struct box_tuple *ret = NULL;
	mh_int_t k = mh_lstrptr_get(str_hash, field);

	if (k != mh_end(str_hash))
		ret = mh_value(str_hash, k);
#ifdef DEBUG
	u32 field_size = load_varint32(&field);
	say_debug("index_hash_str_find(self:%p, key:(%i)'%.*s') = %p",
		  self, field_size, field_size, (u8 *)field, ret);
#endif
	return ret;
}

- (void) remove: (struct box_tuple *) tuple
{
	void *field = tuple_field(tuple, key.parts[0].fieldno);

	mh_int_t k = mh_lstrptr_get(str_hash, field);
	if (k != mh_end(str_hash))
		mh_lstrptr_del(str_hash, k);
#ifdef DEBUG
	u32 field_size = load_varint32(&field);
	say_debug("index_hash_str_remove(self:%p, key:'%.*s')",
		  self, field_size, (u8 *)field);
#endif
}

- (void) replace: (struct box_tuple *) old_tuple
	:(struct box_tuple *) new_tuple
{
	void *field = tuple_field(new_tuple, key.parts[0].fieldno);

	if (field == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, key.parts[0].fieldno);

	if (old_tuple != NULL) {
		void *old_field = tuple_field(old_tuple, key.parts[0].fieldno);
		mh_int_t k = mh_lstrptr_get(str_hash, old_field);
		if (k != mh_end(str_hash))
			mh_lstrptr_del(str_hash, k);
	}

	mh_lstrptr_put(str_hash, field, new_tuple, NULL);
#ifdef DEBUG
	u32 field_size = load_varint32(&field);
	say_debug("index_hash_str_replace(self:%p, old_tuple:%p, tuple:%p) key:'%.*s'",
		  self, old_tuple, new_tuple, field_size, (u8 *)field);
#endif
}

- (void) initIterator: (struct iterator *) iterator
{
	assert(iterator->next = hash_iterator_next);

	struct hash_iterator *it = hash_iterator(iterator);

	it->base.next_equal = 0; /* Should not be used if not positioned. */
	it->h_pos = mh_begin(str_hash);
	it->hash = (struct mh_i32ptr_t *) str_hash;
}

- (void) initIterator: (struct iterator *) iterator :(void *) field
			:(int) part_count
{
	assert(iterator->next = hash_iterator_next);
	assert(part_count== 1);

	struct hash_iterator *it = hash_iterator(iterator);

	it->base.next_equal = iterator_first_equal;
	it->h_pos = mh_lstrptr_get(str_hash, field);
	it->hash = (struct mh_i32ptr_t *) str_hash;
}
@end

/* }}} */

/* {{{ TreeIndex and auxiliary structures. ************************/
/**
 * A field reference used for TREE indexes. Either stores a copy
 * of the corresponding field in the tuple or points to that field
 * in the tuple (depending on field length).
 */
struct field {
	/** Field data length. */
	u32 len;
	/** Actual field data. For small fields we store the value
	 * of the field (u32, u64, strings up to 8 bytes), for
	 * longer fields, we store a pointer to field data in the
	 * tuple in the primary index.
	 */
	union {
		u32 u32;
		u64 u64;
		u8 data[sizeof(u64)];
		void *data_ptr;
	};
};

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

struct index_tree_el {
	struct box_tuple *tuple;
	struct field key[];
};

#define INDEX_TREE_EL_SIZE(key) \
	(sizeof(struct index_tree_el) + sizeof(struct field) * (key)->part_count)

void
index_tree_el_init(struct index_tree_el *elem,
		   struct key *key, struct box_tuple *tuple)
{
	void *tuple_data = tuple->data;

	for (i32 i = 0; i < key->max_fieldno; ++i) {
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

		u32 key_field_no = key->cmp_order[i];

		if (key_field_no == -1)
			continue;

		if (key->parts[key_field_no].type == NUM) {
			if (f.len != 4)
				tnt_raise(IllegalParams, :"key is not u32");
		} else if (key->parts[key_field_no].type == NUM64 && f.len != 8) {
				tnt_raise(IllegalParams, :"key is not u64");
		}

		elem->key[key_field_no] = f;
	}
	elem->tuple = tuple;
}

void
init_search_pattern(struct index_tree_el *pattern,
		    struct key *key, int part_count, void *key_field)
{
	assert(part_count <= key->part_count);

	for (i32 i = 0; i < key->part_count; ++i)
		pattern->key[i] = ASTERISK;
	for (int i = 0; i < part_count; i++) {
		u32 len;

		len = pattern->key[i].len = load_varint32(&key_field);
		if (key->parts[i].type == NUM) {
			if (len != 4)
				tnt_raise(IllegalParams, :"key is not u32");
		} else if (key->parts[i].type == NUM64 && len != 8) {
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
			 struct key *key)
{
	int r = 0;
	for (i32 i = 0, end = key->part_count; i < end; ++i) {
		r = field_compare(&elem_a->key[i], &elem_b->key[i],
				  key->parts[i].type);
		if (r != 0)
			break;
	}
	return r;
}

static int
index_tree_el_cmp(struct index_tree_el *elem_a, struct index_tree_el *elem_b,
		  struct key *key)
{
	int r = index_tree_el_unique_cmp(elem_a, elem_b, key);
	if (r == 0 && elem_a->tuple && elem_b->tuple)
		r = (elem_a->tuple < elem_b->tuple ?
		     -1 : elem_a->tuple > elem_b->tuple);
	return r;
}

#include <third_party/sptree.h>
SPTREE_DEF(str_t, realloc);

@interface TreeIndex: Index {
	sptree_str_t *tree;
	struct index_tree_el *pattern;
};
- (void) build: (Index *) pk;
@end

struct tree_iterator {
	struct iterator base;
	struct sptree_str_t_iterator *t_iter;
	struct index_tree_el *pattern;
	sptree_str_t *tree;
	struct key *key;
};

static inline struct tree_iterator *
tree_iterator(struct iterator *it)
{
	return (struct tree_iterator *) it;
}

static struct box_tuple *
tree_iterator_next(struct iterator *iterator)
{
	assert(iterator->next == tree_iterator_next);

	struct tree_iterator *it = tree_iterator(iterator);

	struct index_tree_el *elem = sptree_str_t_iterator_next(it->t_iter);

	return elem ? elem->tuple : NULL;
}

static struct box_tuple *
tree_iterator_next_equal(struct iterator *iterator)
{
	assert(iterator->next == tree_iterator_next);

	struct tree_iterator *it = tree_iterator(iterator);

	struct index_tree_el *elem =
		sptree_str_t_iterator_next(it->t_iter);

	if (elem != NULL && index_tree_el_unique_cmp(it->pattern, elem, it->key) == 0)
		return elem->tuple;

	return NULL;
}

@implementation TreeIndex

- (void) free
{
	sfree(pattern);
	sfree(tree);
	[super free];
}

- (void) enable
{
	enabled = false;
	pattern = salloc(INDEX_TREE_EL_SIZE(&key));
	tree = salloc(sizeof(*tree));
	memset(tree, 0, sizeof(*tree));
	if (n == 0) {/* pk */
		sptree_str_t_init(tree,
				  INDEX_TREE_EL_SIZE(&key),
				  NULL, 0, 0,
				  (void *)index_tree_el_unique_cmp, &key);
		enabled = true;
	}
}

- (size_t) size
{
	return tree->size;
}

- (struct box_tuple *) min
{
	struct index_tree_el *elem = sptree_str_t_first(tree);

	return elem ? elem->tuple : NULL;
}

- (struct box_tuple *) max
{
	struct index_tree_el *elem = sptree_str_t_last(tree);

	return elem ? elem->tuple : NULL;
}

- (struct box_tuple *) find: (void *) key_arg
{
	init_search_pattern(pattern, &key, 1, key_arg);
	struct index_tree_el *elem = sptree_str_t_find(tree, pattern);

	return elem ? elem->tuple : NULL;
}

- (struct box_tuple *) findBy: (struct box_tuple *) tuple
{
	index_tree_el_init(pattern, &key, tuple);

	struct index_tree_el *elem = sptree_str_t_find(tree, pattern);

	return elem ? elem->tuple : NULL;
}

- (void) remove: (struct box_tuple *) tuple
{
	index_tree_el_init(pattern, &key, tuple);
	sptree_str_t_delete(tree, pattern);
}

- (void) replace: (struct box_tuple *) old_tuple
	:(struct box_tuple *) new_tuple
{
	if (new_tuple->cardinality < key.max_fieldno)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, key.max_fieldno);

	if (old_tuple) {
		index_tree_el_init(pattern, &key, old_tuple);
		sptree_str_t_delete(tree, pattern);
	}
	index_tree_el_init(pattern, &key, new_tuple);
	sptree_str_t_insert(tree, pattern);
}

- (struct iterator *) allocIterator
{
	struct tree_iterator *it = salloc(sizeof(struct tree_iterator) +
					  INDEX_TREE_EL_SIZE(&key));
	it->pattern = (struct index_tree_el *) (it + 1);
	it->base.next = tree_iterator_next;
	it->key = &key;
	it->tree = tree;
	return (struct iterator *) it;
}

- (void) initIterator: (struct iterator *) iterator
{
	[self initIterator: iterator :NULL :0];
}

- (void) initIterator: (struct iterator *) iterator :(void *) key_arg
			:(int) part_count
{
	assert(iterator->next == tree_iterator_next);

	struct tree_iterator *it = tree_iterator(iterator);

	if (key.is_unique && part_count == key.part_count)
		it->base.next_equal = iterator_first_equal;
	else
		it->base.next_equal = tree_iterator_next_equal;

	init_search_pattern(it->pattern, &key, part_count, key_arg);
	sptree_str_t_iterator_init_set(tree, &it->t_iter, it->pattern);
}

- (void) build: (Index *) pk
{
	u32 n_tuples = [pk size];
	u32 estimated_tuples = n_tuples * 1.2;

	assert(enabled == false);

	struct index_tree_el *elem = NULL;
	if (n_tuples) {
		/*
		 * Allocate a little extra to avoid
		 * unnecessary realloc() when more data is
		 * inserted.
		*/
		size_t sz = estimated_tuples * INDEX_TREE_EL_SIZE(&key);
		elem = malloc(sz);
		if (elem == NULL)
			panic("malloc(): failed to allocate %"PRI_SZ" bytes", sz);
	}
	struct index_tree_el *m;
	u32 i = 0;

	struct iterator *it = pk->position;
	[pk initIterator: it];
	struct box_tuple *tuple;
	while ((tuple = it->next(it))) {

		m = (struct index_tree_el *)
			((char *)elem + i * INDEX_TREE_EL_SIZE(&key));

		index_tree_el_init(m, &key, tuple);
		++i;
	}

	if (n_tuples)
		say_info("Sorting %"PRIu32 " keys in index %" PRIu32 "...", n_tuples, self->n);

	/* If n_tuples == 0 then estimated_tuples = 0, elem == NULL, tree is empty */
	sptree_str_t_init(tree, INDEX_TREE_EL_SIZE(&key),
			  elem, n_tuples, estimated_tuples,
			  (void *) (key.is_unique ? index_tree_el_unique_cmp :
			  index_tree_el_cmp), &key);
	enabled = true;
}
@end

/* }}} */

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
			[(TreeIndex*) index build: pk];
		}
		say_info("Space %"PRIu32": done", n);
	}
}

/**
 * vim: foldmethod=marker
 */
