/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "index.h"
#include "tree.h"
#include "say.h"
#include "tuple.h"
#include "pickle.h"
#include "exception.h"
#include "space.h"
#include "assoc.h"
#include "errinj.h"

static struct index_traits index_traits = {
	.allows_partial_key = true,
};

static struct index_traits hash_index_traits = {
	.allows_partial_key = false,
};

const char *field_data_type_strs[] = {"NUM", "NUM64", "STR", "\0"};
const char *index_type_strs[] = { "HASH", "TREE", "\0" };

STRS(iterator_type, ITERATOR_TYPE);

void
check_key_parts(struct key_def *key_def,
		int part_count, bool partial_key_allowed)
{
	if (part_count > key_def->part_count)
		tnt_raise(ClientError, :ER_KEY_PART_COUNT,
			  part_count, key_def->part_count);
	if (!partial_key_allowed && part_count < key_def->part_count)
		tnt_raise(ClientError, :ER_EXACT_MATCH,
			  part_count, key_def->part_count);
}

/**
 * Check if replacement of an old tuple with a new one is
 * allowed.
 */
uint32_t
replace_check_dup(struct tuple *old_tuple,
		  struct tuple *dup_tuple,
		  enum dup_replace_mode mode)
{
	if (dup_tuple == NULL) {
		if (mode == DUP_REPLACE) {
			/*
			 * dup_replace_mode is DUP_REPLACE, and
			 * a tuple with the same key is not found.
			 */
			return ER_TUPLE_NOT_FOUND;
		}
	} else { /* dup_tuple != NULL */
		if (dup_tuple != old_tuple &&
		    (old_tuple != NULL || mode == DUP_INSERT)) {
			/*
			 * There is a duplicate of new_tuple,
			 * and it's not old_tuple: we can't
			 * possibly delete more than one tuple
			 * at once.
			 */
			return ER_TUPLE_FOUND;
		}
	}
	return 0;
}


/* {{{ Index -- base class for all indexes. ********************/

@interface HashIndex: Index
- (void) reserve: (u32) n_tuples;
@end

@interface HashStrIndex: HashIndex {
	 struct mh_lstrptr_t *str_hash;
};
@end

@interface Hash64Index: HashIndex {
	struct mh_i64ptr_t *int64_hash;
};
@end

@interface Hash32Index: HashIndex {
	 struct mh_i32ptr_t *int_hash;
};
@end


@implementation Index

@class Hash32Index;
@class Hash64Index;
@class HashStrIndex;
@class TreeIndex;

+ (struct index_traits *) traits
{
	return &index_traits;
}

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
	if (self) {
		traits = [object_getClass(self) traits];
		key_def = key_def_arg;
		space = space_arg;
		position = [self allocIterator];
	}
	return self;
}

- (void) free
{
	position->free(position);
	[super free];
}

- (void) beginBuild
{
	[self subclassResponsibility: _cmd];
}

- (void) buildNext: (struct tuple *)tuple
{
	(void) tuple;
	[self subclassResponsibility: _cmd];
}

- (void) endBuild
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

- (struct tuple *) min
{
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct tuple *) max
{
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct tuple *) findByKey: (void *) key :(int) part_count
{
	check_key_parts(key_def, part_count, false);
	return [self findUnsafe: key :part_count];
}

- (struct tuple *) findUnsafe: (void *) key :(int) part_count
{
	(void) key;
	(void) part_count;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct tuple *) replace: (struct tuple *) old_tuple
			  : (struct tuple *) new_tuple
			  : (enum dup_replace_mode) mode
{
	(void) old_tuple;
	(void) new_tuple;
	(void) mode;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct iterator *) allocIterator
{
	[self subclassResponsibility: _cmd];
	return NULL;
}


- (void) initIterator: (struct iterator *) iterator
	:(enum iterator_type) type
	:(void *) key :(int) part_count
{
	(void) iterator;
	(void) type;
	(void) key;
	(void) part_count;
	[self subclassResponsibility: _cmd];
}

@end

/* }}} */

/* {{{ HashIndex -- base class for all hashes. ********************/

struct hash_i32_iterator {
	struct iterator base; /* Must be the first member. */
	struct mh_i32ptr_t *hash;
	mh_int_t h_pos;
};

struct hash_i64_iterator {
	struct iterator base;
	struct mh_i64ptr_t *hash;
	mh_int_t h_pos;
};

struct hash_lstr_iterator {
	struct iterator base;
	struct mh_lstrptr_t *hash;
	mh_int_t h_pos;
};

void
hash_iterator_free(struct iterator *iterator)
{
	assert(iterator->free == hash_iterator_free);
	free(iterator);
}

struct tuple *
hash_iterator_i32_ge(struct iterator *ptr)
{
	assert(ptr->free == hash_iterator_free);
	struct hash_i32_iterator *it = (struct hash_i32_iterator *) ptr;

	while (it->h_pos < mh_end(it->hash)) {
		if (mh_exist(it->hash, it->h_pos))
			return mh_i32ptr_node(it->hash, it->h_pos++)->val;
		it->h_pos++;
	}
	return NULL;
}

struct tuple *
hash_iterator_i64_ge(struct iterator *ptr)
{
	assert(ptr->free == hash_iterator_free);
	struct hash_i64_iterator *it = (struct hash_i64_iterator *) ptr;

	while (it->h_pos < mh_end(it->hash)) {
		if (mh_exist(it->hash, it->h_pos))
			return mh_i64ptr_node(it->hash, it->h_pos++)->val;
		it->h_pos++;
	}
	return NULL;
}

struct tuple *
hash_iterator_lstr_ge(struct iterator *ptr)
{
	assert(ptr->free == hash_iterator_free);
	struct hash_lstr_iterator *it = (struct hash_lstr_iterator *) ptr;

	while (it->h_pos < mh_end(it->hash)) {
		if (mh_exist(it->hash, it->h_pos))
			return mh_lstrptr_node(it->hash, it->h_pos++)->val;
		it->h_pos++;
	}
	return NULL;
}

static struct tuple *
hash_iterator_eq_next(struct iterator *it __attribute__((unused)))
{
	return NULL;
}

static struct tuple *
hash_iterator_i32_eq(struct iterator *it)
{
	it->next = hash_iterator_eq_next;
	return hash_iterator_i32_ge(it);
}

static struct tuple *
hash_iterator_i64_eq(struct iterator *it)
{
	it->next = hash_iterator_eq_next;
	return hash_iterator_i64_ge(it);
}

static struct tuple *
hash_iterator_lstr_eq(struct iterator *it)
{
	it->next = hash_iterator_eq_next;
	return hash_iterator_lstr_ge(it);
}

@implementation HashIndex

+ (struct index_traits *) traits
{
	return &hash_index_traits;
}

- (void) reserve: (u32) n_tuples
{
	(void) n_tuples;
	[self subclassResponsibility: _cmd];
}

- (void) beginBuild
{
}

- (void) buildNext: (struct tuple *)tuple
{
	[self replace: NULL :tuple :DUP_INSERT];
}

- (void) endBuild
{
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
	struct tuple *tuple;
	[pk initIterator: it :ITER_ALL :NULL :0];

	while ((tuple = it->next(it)))
	      [self replace: NULL :tuple :DUP_INSERT];
}

- (void) free
{
	[super free];
}

- (struct tuple *) min
{
	tnt_raise(ClientError, :ER_UNSUPPORTED, "Hash index", "min()");
	return NULL;
}

- (struct tuple *) max
{
	tnt_raise(ClientError, :ER_UNSUPPORTED, "Hash index", "max()");
	return NULL;
}

@end

/* }}} */

/* {{{ Hash32Index ************************************************/

static inline struct mh_i32ptr_node_t
int32_key_to_node(void *key)
{
	u32 key_size = load_varint32(&key);
	if (key_size != 4)
		tnt_raise(ClientError, :ER_KEY_FIELD_TYPE, "u32");
	struct mh_i32ptr_node_t node = { .key = *(u32 *) key };
	return node;
}

static inline struct mh_i32ptr_node_t
int32_tuple_to_node(struct tuple *tuple, struct key_def *key_def)
{
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	struct mh_i32ptr_node_t node = int32_key_to_node(field);
	node.val = tuple;
	return node;
}

@implementation Hash32Index

- (void) reserve: (u32) n_tuples
{
	mh_i32ptr_reserve(int_hash, n_tuples, NULL, NULL);
}

- (void) free
{
	mh_i32ptr_destroy(int_hash);
	[super free];
}

- (id) init: (struct key_def *) key_def_arg :(struct space *) space_arg
{
	self = [super init: key_def_arg :space_arg];
	if (self) {
		int_hash = mh_i32ptr_init();
	}
	return self;
}

- (size_t) size
{
	return mh_size(int_hash);
}

- (struct tuple *) findUnsafe: (void *) key :(int) part_count
{
	(void) part_count;

	struct tuple *ret = NULL;
	struct mh_i32ptr_node_t node = int32_key_to_node(key);
	mh_int_t k = mh_i32ptr_get(int_hash, &node, NULL, NULL);
	if (k != mh_end(int_hash))
		ret = mh_i32ptr_node(int_hash, k)->val;
#ifdef DEBUG
	say_debug("Hash32Index find(self:%p, key:%i) = %p", self, node.key, ret);
#endif
	return ret;
}

- (struct tuple *) replace: (struct tuple *) old_tuple
			  :(struct tuple *) new_tuple
			  :(enum dup_replace_mode) mode
{
	struct mh_i32ptr_node_t new_node, old_node;
	uint32_t errcode;

	if (new_tuple) {
		struct mh_i32ptr_node_t *dup_node = &old_node;
		new_node = int32_tuple_to_node(new_tuple, key_def);
		mh_int_t pos = mh_i32ptr_replace(int_hash, &new_node,
						 &dup_node, NULL, NULL);

		ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		{
			mh_i32ptr_del(int_hash, pos, NULL, NULL);
			pos = mh_end(int_hash);
		});

		if (pos == mh_end(int_hash)) {
			tnt_raise(LoggedError, :ER_MEMORY_ISSUE, (ssize_t) pos,
				  "int hash", "key");
		}
		struct tuple *dup_tuple = dup_node ? dup_node->val : NULL;
		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			mh_i32ptr_remove(int_hash, &new_node, NULL, NULL);
			if (dup_node) {
				pos = mh_i32ptr_replace(int_hash, dup_node,
							NULL, NULL, NULL);
				if (pos == mh_end(int_hash)) {
					panic("Failed to allocate memory in "
					      "recover of int hash");
				}
			}
			tnt_raise(ClientError, :errcode, index_n(self));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		old_node = int32_tuple_to_node(old_tuple, key_def);
		mh_i32ptr_remove(int_hash, &old_node, NULL, NULL);
	}
	return old_tuple;
}


- (struct iterator *) allocIterator
{
	struct hash_i32_iterator *it = malloc(sizeof(struct hash_i32_iterator));
	if (it) {
		memset(it, 0, sizeof(*it));
		it->base.next = hash_iterator_i32_ge;
		it->base.free = hash_iterator_free;
	}
	return (struct iterator *) it;
}

- (void) initIterator: (struct iterator *) ptr: (enum iterator_type) type
                        :(void *) key :(int) part_count
{
	(void) part_count;
	assert(ptr->free == hash_iterator_free);
	struct hash_i32_iterator *it = (struct hash_i32_iterator *) ptr;
	struct mh_i32ptr_node_t node;

	switch (type) {
	case ITER_GE:
		if (key != NULL) {
			check_key_parts(key_def, part_count,
					traits->allows_partial_key);
			node = int32_key_to_node(key);
			it->h_pos = mh_i32ptr_get(int_hash, &node, NULL, NULL);
			it->base.next = hash_iterator_i32_ge;
			break;
		}
		/* Fall through. */
	case ITER_ALL:
		it->h_pos = mh_begin(int_hash);
		it->base.next = hash_iterator_i32_ge;
		break;
	case ITER_EQ:
		check_key_parts(key_def, part_count,
				traits->allows_partial_key);
		node = int32_key_to_node(key);
		it->h_pos = mh_i32ptr_get(int_hash, &node, NULL, NULL);
		it->base.next = hash_iterator_i32_eq;
		break;
	default:
		tnt_raise(ClientError, :ER_UNSUPPORTED,
			  "Hash index", "requested iterator type");
	}
	it->hash = int_hash;
}
@end

/* }}} */

/* {{{ Hash64Index ************************************************/

static inline struct mh_i64ptr_node_t
int64_key_to_node(void *key)
{
	u32 key_size = load_varint32(&key);
	if (key_size != 8)
		tnt_raise(ClientError, :ER_KEY_FIELD_TYPE, "u64");
	struct mh_i64ptr_node_t node = { .key = *(u64 *) key };
	return node;
}

static inline struct mh_i64ptr_node_t
int64_tuple_to_node(struct tuple *tuple, struct key_def *key_def)
{
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	struct mh_i64ptr_node_t node = int64_key_to_node(field);
	node.val = tuple;
	return node;
}

@implementation Hash64Index
- (void) reserve: (u32) n_tuples
{
	mh_i64ptr_reserve(int64_hash, n_tuples, NULL, NULL);
}

- (void) free
{
	mh_i64ptr_destroy(int64_hash);
	[super free];
}

- (id) init: (struct key_def *) key_def_arg :(struct space *) space_arg
{
	self = [super init: key_def_arg :space_arg];
	if (self) {
		int64_hash = mh_i64ptr_init();
	}
	return self;
}

- (size_t) size
{
	return mh_size(int64_hash);
}

- (struct tuple *) findUnsafe: (void *) key :(int) part_count
{
	(void) part_count;

	struct tuple *ret = NULL;
	struct mh_i64ptr_node_t node = int64_key_to_node(key);
	mh_int_t k = mh_i64ptr_get(int64_hash, &node, NULL, NULL);
	if (k != mh_end(int64_hash))
		ret = mh_i64ptr_node(int64_hash, k)->val;
#ifdef DEBUG
	say_debug("Hash64Index find(self:%p, key:%"PRIu64") = %p", self, node.key, ret);
#endif
	return ret;
}

- (struct tuple *) replace: (struct tuple *) old_tuple
			  :(struct tuple *) new_tuple
			  :(enum dup_replace_mode) mode
{
	struct mh_i64ptr_node_t new_node, old_node;
	uint32_t errcode;

	if (new_tuple) {
		struct mh_i64ptr_node_t *dup_node = &old_node;
		new_node = int64_tuple_to_node(new_tuple, key_def);
		mh_int_t pos = mh_i64ptr_replace(int64_hash, &new_node,
						 &dup_node, NULL, NULL);

		ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		{
			mh_i64ptr_del(int64_hash, pos, NULL, NULL);
			pos = mh_end(int64_hash);
		});
		if (pos == mh_end(int64_hash)) {
			tnt_raise(LoggedError, :ER_MEMORY_ISSUE, (ssize_t) pos,
				  "int64 hash", "key");
		}
		struct tuple *dup_tuple = dup_node ? dup_node->val : NULL;
		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			mh_i64ptr_remove(int64_hash, &new_node, NULL, NULL);
			if (dup_node) {
				pos = mh_i64ptr_replace(int64_hash, dup_node,
							NULL, NULL, NULL);
				if (pos == mh_end(int64_hash)) {
					panic("Failed to allocate memory in "
					      "recover of int64 hash");
				}
			}
			tnt_raise(ClientError, :errcode, index_n(self));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		old_node = int64_tuple_to_node(old_tuple, key_def);
		mh_i64ptr_remove(int64_hash, &old_node, NULL, NULL);
	}
	return old_tuple;
}


- (struct iterator *) allocIterator
{
	struct hash_i64_iterator *it = malloc(sizeof(struct hash_i64_iterator));
	if (it) {
		memset(it, 0, sizeof(*it));
		it->base.next = hash_iterator_i64_ge;
		it->base.free = hash_iterator_free;
	}
	return (struct iterator *) it;
}


- (void) initIterator: (struct iterator *) ptr: (enum iterator_type) type
                        :(void *) key :(int) part_count
{
	(void) part_count;
	assert(ptr->free == hash_iterator_free);
	struct hash_i64_iterator *it = (struct hash_i64_iterator *) ptr;
	struct mh_i64ptr_node_t node;

	switch (type) {
	case ITER_GE:
		if (key != NULL) {
			check_key_parts(key_def, part_count,
					traits->allows_partial_key);
			node = int64_key_to_node(key);
			it->h_pos = mh_i64ptr_get(int64_hash, &node, NULL, NULL);
			it->base.next = hash_iterator_i64_ge;
			break;
		}
		/* Fallthrough. */
	case ITER_ALL:
		it->base.next = hash_iterator_i64_ge;
		it->h_pos = mh_begin(int64_hash);
		break;
	case ITER_EQ:
		check_key_parts(key_def, part_count,
				traits->allows_partial_key);
		node = int64_key_to_node(key);
		it->h_pos = mh_i64ptr_get(int64_hash, &node, NULL, NULL);
		it->base.next = hash_iterator_i64_eq;
		break;
	default:
		tnt_raise(ClientError, :ER_UNSUPPORTED,
			  "Hash index", "requested iterator type");
	}
	it->hash = int64_hash;
}
@end

/* }}} */

/* {{{ HashStrIndex ***********************************************/

static inline struct mh_lstrptr_node_t
lstrptr_tuple_to_node(struct tuple *tuple, struct key_def *key_def)
{
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	if (field == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD,
			  key_def->parts[0].fieldno);

	struct mh_lstrptr_node_t node = { .key = field, .val = tuple };
	return node;
}


@implementation HashStrIndex
- (void) reserve: (u32) n_tuples
{
	mh_lstrptr_reserve(str_hash, n_tuples, NULL, NULL);
}

- (void) free
{
	mh_lstrptr_destroy(str_hash);
	[super free];
}

- (id) init: (struct key_def *) key_def_arg :(struct space *) space_arg
{
	self = [super init: key_def_arg :space_arg];
	if (self) {
		str_hash = mh_lstrptr_init();
	}
	return self;
}

- (size_t) size
{
	return mh_size(str_hash);
}

- (struct tuple *) findUnsafe: (void *) key :(int) part_count
{
	(void) part_count;
	struct tuple *ret = NULL;
	const struct mh_lstrptr_node_t node = { .key = key };
	mh_int_t k = mh_lstrptr_get(str_hash, &node, NULL, NULL);
	if (k != mh_end(str_hash))
		ret = mh_lstrptr_node(str_hash, k)->val;
#ifdef DEBUG
	u32 key_size = load_varint32(&key);
	say_debug("HashStrIndex find(self:%p, key:(%i)'%.*s') = %p",
		  self, key_size, key_size, (u8 *)key, ret);
#endif
	return ret;
}

- (struct tuple *) replace: (struct tuple *) old_tuple
			  :(struct tuple *) new_tuple
			  :(enum dup_replace_mode) mode
{
	struct mh_lstrptr_node_t new_node, old_node;
	uint32_t errcode;

	if (new_tuple) {
		struct mh_lstrptr_node_t *dup_node = &old_node;
		new_node = lstrptr_tuple_to_node(new_tuple, key_def);
		mh_int_t pos = mh_lstrptr_replace(str_hash, &new_node,
						  &dup_node, NULL, NULL);

		ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		{
			mh_lstrptr_del(str_hash, pos, NULL, NULL);
			pos = mh_end(str_hash);
		});

		if (pos == mh_end(str_hash)) {
			tnt_raise(LoggedError, :ER_MEMORY_ISSUE, (ssize_t) pos,
				  "str hash", "key");
		}
		struct tuple *dup_tuple = dup_node ? dup_node->val : NULL;
		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			mh_lstrptr_remove(str_hash, &new_node, NULL, NULL);
			if (dup_node) {
				pos = mh_lstrptr_replace(str_hash, dup_node,
							 NULL, NULL, NULL);
				if (pos == mh_end(str_hash)) {
					panic("Failed to allocate memory in "
					      "recover of str hash");
				}
			}
			tnt_raise(ClientError, :errcode, index_n(self));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		old_node = lstrptr_tuple_to_node(old_tuple, key_def);
		mh_lstrptr_remove(str_hash, &old_node, NULL, NULL);
	}
	return old_tuple;
}

- (struct iterator *) allocIterator
{
	struct hash_lstr_iterator *it = malloc(sizeof(struct hash_lstr_iterator));
	if (it) {
		memset(it, 0, sizeof(*it));
		it->base.next = hash_iterator_lstr_ge;
		it->base.free = hash_iterator_free;
	}
	return (struct iterator *) it;
}


- (void) initIterator: (struct iterator *) ptr
			:(enum iterator_type) type
                        :(void *) key :(int) part_count
{
	(void) part_count;

	assert(ptr->free == hash_iterator_free);
	struct hash_lstr_iterator *it = (struct hash_lstr_iterator *) ptr;
	struct mh_lstrptr_node_t node;

	switch (type) {
	case ITER_GE:
		if (key != NULL) {
			check_key_parts(key_def, part_count,
					traits->allows_partial_key);
			node.key = key;
			it->h_pos = mh_lstrptr_get(str_hash, &node, NULL, NULL);
			it->base.next = hash_iterator_lstr_ge;
			break;
		}
		/* Fall through. */
	case ITER_ALL:
		it->base.next = hash_iterator_lstr_ge;
		it->h_pos = mh_begin(str_hash);
		break;
	case ITER_EQ:
		check_key_parts(key_def, part_count,
				traits->allows_partial_key);
		node.key = key;
		it->h_pos = mh_lstrptr_get(str_hash, &node, NULL, NULL);
		it->base.next = hash_iterator_lstr_eq;
		break;
	default:
		tnt_raise(ClientError, :ER_UNSUPPORTED,
			  "Hash index", "requested iterator type");
	}
	it->hash = str_hash;
}
@end

/* }}} */

