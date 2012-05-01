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
#include "tree.h"
#include "say.h"
#include "tuple.h"
#include "pickle.h"
#include "exception.h"
#include "box.h"
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

+ (Index *) alloc: (enum index_type) type
	 :(struct key_def *) key_def
	 :(struct space *) space
{
	switch (type) {
	case HASH:
		/* Hash index, check key type.
		 * Hash index always has a single-field key.
		 */
		switch (key_def->parts[0].type) {
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
		return [TreeIndex alloc: key_def :space];
	default:
		break;
	}
	panic("unsupported index type");
}

- (id) init: (struct key_def *) key_def_arg :(struct space *) space_arg
{
	self = [super init];
	key_def = key_def_arg;
	space = space_arg;
	position = [self allocIterator];
	[self enable];
	return self;
}

- (void) free
{
	position->free(position);
	[super free];
}

- (void) enable
{
	[self subclassResponsibility: _cmd];
}

- (void) build: (Index *) pk
{
	(void) pk;
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

- (struct box_tuple *) find: (void *) key :(int) key_cardinality
{
	(void) key;
	(void) key_cardinality;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct box_tuple *) findByTuple: (struct box_tuple *) pattern
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

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
{
	(void) iterator;
	(void) type;
	[self subclassResponsibility: _cmd];
}

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
                        :(void *) key
			:(int) key_cardinality
{
	(void) iterator;
	(void) type;
	(void) key;
	(void) key_cardinality;
	[self subclassResponsibility: _cmd];
}
@end

/* }}} */

/* {{{ HashIndex -- base class for all hashes. ********************/

@interface HashIndex: Index
- (void) reserve: (u32) n_tuples;
@end

struct hash_iterator {
	struct iterator base; /* Must be the first member. */
	struct mh_i32ptr_t *hash;
	mh_int_t h_pos;
};

static struct hash_iterator *
hash_iterator(struct iterator *it)
{
	return (struct hash_iterator *) it;
}

struct box_tuple *
hash_iterator_next(struct iterator *iterator)
{
	assert(iterator->next == hash_iterator_next);
	struct hash_iterator *it = hash_iterator(iterator);

	while (it->h_pos != mh_end(it->hash)) {
		if (mh_exist(it->hash, it->h_pos))
			return mh_value(it->hash, it->h_pos++);
		it->h_pos++;
	}
	return NULL;
}

void
hash_iterator_free(struct iterator *iterator)
{
	assert(iterator->next == hash_iterator_next);
	free(iterator);
}


@implementation HashIndex

- (void) reserve: (u32) n_tuples
{
	(void) n_tuples;
	[self subclassResponsibility: _cmd];
}

- (void) build: (Index *) pk
{
	u32 n_tuples = [pk size];

	if (n_tuples == 0)
		return;

	[self reserve: n_tuples];

	say_info("Adding %"PRIu32 " keys to HASH index %"
		 PRIu32 "...", n_tuples, index_n(self));

	struct iterator *it = pk->position;
	struct box_tuple *tuple;
	[pk initIterator: it :ITER_FORWARD];

	while ((tuple = it->next(it)))
	      [self replace: NULL :tuple];
}

- (void) free
{
	[super free];
}

- (struct box_tuple *) min
{
	tnt_raise(ClientError, :ER_UNSUPPORTED, "Hash index", "min()");
	return NULL;
}

- (struct box_tuple *) max
{
	tnt_raise(ClientError, :ER_UNSUPPORTED, "Hash index", "max()");
	return NULL;
}

- (struct box_tuple *) findByTuple: (struct box_tuple *) tuple
{
	/* Hash index currently is always single-part. */
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	if (field == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD,
			  key_def->parts[0].fieldno);
	return [self find :field :1];
}

- (struct iterator *) allocIterator
{
	struct hash_iterator *it = malloc(sizeof(struct hash_iterator));
	if (it) {
		memset(it, 0, sizeof(struct hash_iterator));
		it->base.next = hash_iterator_next;
		it->base.free = hash_iterator_free;
	}
	return (struct iterator *) it;
}
@end

/* }}} */

/* {{{ Hash32Index ************************************************/

static u32
int32_key_to_value(void *key, int key_cardinality)
{
	if (key_cardinality > 1)
		tnt_raise(ClientError, :ER_KEY_CARDINALITY,
			  key_cardinality, 1);

	if (key_cardinality < 1)
		tnt_raise(ClientError, :ER_EXACT_MATCH,
			  key_cardinality, 1);

	u32 key_size = load_varint32(&key);
	if (key_size != 4)
		tnt_raise(ClientError, :ER_KEY_FIELD_TYPE, "u32");

	return *((u32 *) key);
}


@interface Hash32Index: HashIndex {
	 struct mh_i32ptr_t *int_hash;
};
@end

@implementation Hash32Index

- (void) reserve: (u32) n_tuples
{
	mh_i32ptr_reserve(int_hash, n_tuples);
}

- (void) free
{
	mh_i32ptr_destroy(int_hash);
	[super free];
}

- (void) enable
{
	int_hash = mh_i32ptr_init();
}

- (size_t) size
{
	return mh_size(int_hash);
}

- (struct box_tuple *) find: (void *) key :(int) key_cardinality
{
	struct box_tuple *ret = NULL;
	u32 num = int32_key_to_value(key, key_cardinality);
	mh_int_t k = mh_i32ptr_get(int_hash, num);
	if (k != mh_end(int_hash))
		ret = mh_value(int_hash, k);
#ifdef DEBUG
	say_debug("Hash32Index find(self:%p, key:%i) = %p", self, num, ret);
#endif
	return ret;
}

- (void) remove: (struct box_tuple *) tuple
{
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	u32 num = int32_key_to_value(field, 1);
	mh_int_t k = mh_i32ptr_get(int_hash, num);
	if (k != mh_end(int_hash))
		mh_i32ptr_del(int_hash, k);
#ifdef DEBUG
	say_debug("Hash32Index remove(self:%p, key:%i)", self, num);
#endif
}

- (void) replace: (struct box_tuple *) old_tuple
	:(struct box_tuple *) new_tuple
{
	void *field = tuple_field(new_tuple, key_def->parts[0].fieldno);
	u32 num = int32_key_to_value(field, 1);

	if (old_tuple != NULL) {
		void *old_field = tuple_field(old_tuple,
					      key_def->parts[0].fieldno);
		load_varint32(&old_field);
		u32 old_num = *(u32 *)old_field;
		mh_int_t k = mh_i32ptr_get(int_hash, old_num);
		if (k != mh_end(int_hash))
			mh_i32ptr_del(int_hash, k);
	}

	mh_i32ptr_put(int_hash, num, new_tuple, NULL);

#ifdef DEBUG
	say_debug("Hash32Index replace(self:%p, old_tuple:%p, new_tuple:%p) key:%i",
		  self, old_tuple, new_tuple, num);
#endif
}

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
{
	assert(iterator->next == hash_iterator_next);
	struct hash_iterator *it = hash_iterator(iterator);

	if (type == ITER_REVERSE)
		tnt_raise(IllegalParams, :"hash iterator is forward only");

	it->base.next_equal = 0; /* Should not be used. */
	it->h_pos = mh_begin(int_hash);
	it->hash = int_hash;
}

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
                        :(void *) key
			:(int) key_cardinality
{
	assert(iterator->next == hash_iterator_next);
	struct hash_iterator *it = hash_iterator(iterator);

	if (type == ITER_REVERSE)
		tnt_raise(IllegalParams, :"hash iterator is forward only");

	u32 num = int32_key_to_value(key, key_cardinality);
	it->base.next_equal = iterator_first_equal;
	it->h_pos = mh_i32ptr_get(int_hash, num);
	it->hash = int_hash;
}
@end

/* }}} */

/* {{{ Hash64Index ************************************************/

static u64
int64_key_to_value(void *key, int key_cardinality)
{
	if (key_cardinality > 1)
		tnt_raise(ClientError, :ER_KEY_CARDINALITY,
			  key_cardinality, 1);

	if (key_cardinality < 1)
		tnt_raise(ClientError, :ER_EXACT_MATCH,
			  key_cardinality, 1);

	u32 key_size = load_varint32(&key);
	if (key_size != 8)
		tnt_raise(ClientError, :ER_KEY_FIELD_TYPE, "u64");

	return *((u64 *) key);
}

@interface Hash64Index: HashIndex {
	struct mh_i64ptr_t *int64_hash;
};
@end

@implementation Hash64Index
- (void) reserve: (u32) n_tuples
{
	mh_i64ptr_reserve(int64_hash, n_tuples);
}

- (void) free
{
	mh_i64ptr_destroy(int64_hash);
	[super free];
}

- (void) enable
{
	int64_hash = mh_i64ptr_init();
}

- (size_t) size
{
	return mh_size(int64_hash);
}

- (struct box_tuple *) find: (void *) key :(int) key_cardinality
{
	struct box_tuple *ret = NULL;
	u64 num = int64_key_to_value(key, key_cardinality);
	mh_int_t k = mh_i64ptr_get(int64_hash, num);
	if (k != mh_end(int64_hash))
		ret = mh_value(int64_hash, k);
#ifdef DEBUG
	say_debug("Hash64Index find(self:%p, key:%"PRIu64") = %p", self, num, ret);
#endif
	return ret;
}

- (void) remove: (struct box_tuple *) tuple
{
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	u64 num = int64_key_to_value(field, 1);

	mh_int_t k = mh_i64ptr_get(int64_hash, num);
	if (k != mh_end(int64_hash))
		mh_i64ptr_del(int64_hash, k);
#ifdef DEBUG
	say_debug("Hash64Index remove(self:%p, key:%"PRIu64")", self, num);
#endif
}

- (void) replace: (struct box_tuple *) old_tuple
	:(struct box_tuple *) new_tuple
{
	void *field = tuple_field(new_tuple, key_def->parts[0].fieldno);
	u64 num = int64_key_to_value(field, 1);

	if (old_tuple != NULL) {
		void *old_field = tuple_field(old_tuple,
					      key_def->parts[0].fieldno);
		load_varint32(&old_field);
		u64 old_num = *(u64 *)old_field;
		mh_int_t k = mh_i64ptr_get(int64_hash, old_num);
		if (k != mh_end(int64_hash))
			mh_i64ptr_del(int64_hash, k);
	}

	mh_i64ptr_put(int64_hash, num, new_tuple, NULL);
#ifdef DEBUG
	say_debug("Hash64Index replace(self:%p, old_tuple:%p, tuple:%p) key:%"PRIu64,
		  self, old_tuple, new_tuple, num);
#endif
}

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
{
	assert(iterator->next == hash_iterator_next);
	struct hash_iterator *it = hash_iterator(iterator);

	if (type == ITER_REVERSE)
		tnt_raise(IllegalParams, :"hash iterator is forward only");

	it->base.next_equal = 0; /* Should not be used if not positioned. */
	it->h_pos = mh_begin(int64_hash);
	it->hash = (struct mh_i32ptr_t *) int64_hash;
}

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
                        :(void *) key
			:(int) key_cardinality
{
	assert(iterator->next == hash_iterator_next);
	struct hash_iterator *it = hash_iterator(iterator);

	if (type == ITER_REVERSE)
		tnt_raise(IllegalParams, :"hash iterator is forward only");

	u64 num = int64_key_to_value(key, key_cardinality);

	it->base.next_equal = iterator_first_equal;
	it->h_pos = mh_i64ptr_get(int64_hash, num);
	it->hash = (struct mh_i32ptr_t *) int64_hash;
}
@end

/* }}} */

/* {{{ HashStrIndex ***********************************************/

static char *
str_key_to_value(void *key, int key_cardinality)
{
	if (key_cardinality > 1)
		tnt_raise(ClientError, :ER_KEY_CARDINALITY,
			  key_cardinality, 1);
	if (key_cardinality < 1)
		tnt_raise(ClientError, :ER_EXACT_MATCH,
			  key_cardinality, 1);
	return key;
}

@interface HashStrIndex: HashIndex {
	 struct mh_lstrptr_t *str_hash;
};
@end

@implementation HashStrIndex
- (void) reserve: (u32) n_tuples
{
	mh_lstrptr_reserve(str_hash, n_tuples);
}

- (void) free
{
	mh_lstrptr_destroy(str_hash);
	[super free];
}

- (void) enable
{
	str_hash = mh_lstrptr_init();
}

- (size_t) size
{
	return mh_size(str_hash);
}

- (struct box_tuple *) find: (void *) key :(int) key_cardinality
{
	struct box_tuple *ret = NULL;
	void *lstr = str_key_to_value(key, key_cardinality);
	mh_int_t k = mh_lstrptr_get(str_hash, lstr);
	if (k != mh_end(str_hash))
		ret = mh_value(str_hash, k);
#ifdef DEBUG
	u32 key_size = load_varint32(&key);
	say_debug("HashStrIndex find(self:%p, key:(%i)'%.*s') = %p",
		  self, key_size, key_size, (u8 *)key, ret);
#endif
	return ret;
}

- (void) remove: (struct box_tuple *) tuple
{
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);

	mh_int_t k = mh_lstrptr_get(str_hash, field);
	if (k != mh_end(str_hash))
		mh_lstrptr_del(str_hash, k);
#ifdef DEBUG
	u32 field_size = load_varint32(&field);
	say_debug("HashStrIndex remove(self:%p, key:'%.*s')",
		  self, field_size, (u8 *)field);
#endif
}

- (void) replace: (struct box_tuple *) old_tuple
	:(struct box_tuple *) new_tuple
{
	void *field = tuple_field(new_tuple, key_def->parts[0].fieldno);

	if (field == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD,
			  key_def->parts[0].fieldno);

	if (old_tuple != NULL) {
		void *old_field = tuple_field(old_tuple,
					      key_def->parts[0].fieldno);
		mh_int_t k = mh_lstrptr_get(str_hash, old_field);
		if (k != mh_end(str_hash))
			mh_lstrptr_del(str_hash, k);
	}

	mh_lstrptr_put(str_hash, field, new_tuple, NULL);
#ifdef DEBUG
	u32 field_size = load_varint32(&field);
	say_debug("HashStrIndex replace(self:%p, old_tuple:%p, tuple:%p) key:'%.*s'",
		  self, old_tuple, new_tuple, field_size, (u8 *)field);
#endif
}

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
{
	assert(iterator->next == hash_iterator_next);
	struct hash_iterator *it = hash_iterator(iterator);

	if (type == ITER_REVERSE)
		tnt_raise(IllegalParams, :"hash iterator is forward only");

	it->base.next_equal = 0; /* Should not be used if not positioned. */
	it->h_pos = mh_begin(str_hash);
	it->hash = (struct mh_i32ptr_t *) str_hash;
}

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
                        :(void *) key
			:(int) key_cardinality
{
	assert(iterator->next == hash_iterator_next);
	struct hash_iterator *it = hash_iterator(iterator);

	if (type == ITER_REVERSE)
		tnt_raise(IllegalParams, :"hash iterator is forward only");

	void *lstr = str_key_to_value(key, key_cardinality);
	it->base.next_equal = iterator_first_equal;
	it->h_pos = mh_lstrptr_get(str_hash, lstr);
	it->hash = (struct mh_i32ptr_t *) str_hash;
}
@end

/* }}} */

/**
 * vim: foldmethod=marker
 */
