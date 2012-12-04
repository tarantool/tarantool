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
#include "request.h" /* for BOX_ADD, BOX_REPLACE */
#include "space.h"
#include "assoc.h"

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

/* {{{ Index -- base class for all indexes. ********************/

@interface HashIndex: Index {
	void *hash;
}

- (void) reserve: (u32) n_tuples;
- (size_t) node_size;

- (void) replaceNode: (void *) h: (void *) node: (void **) pprev;
- (void) deleteNode: (void *) h: (void *) node: (void **) pprev;
- (void) fold: (void *) node :(struct tuple *) tuple;
- (struct tuple *) unfold: (const void *) node;
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

- (struct tuple *) findByTuple: (struct tuple *) pattern
{
	(void) pattern;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct tuple *) replace: (struct tuple *) old_tuple
			  : (struct tuple *) new_tuple
			  : (u32) flags
{
	(void) old_tuple;
	(void) new_tuple;
	(void) flags;
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

- (size_t) node_size;
{
	[self subclassResponsibility: _cmd];
	return 0;
}

- (void) fold: (void *) node :(struct tuple *) tuple
{
	(void) node;
	(void) tuple;
	[self subclassResponsibility: _cmd];
}

- (struct tuple *) unfold: (const void *) node
{
	(void) node;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (void) beginBuild
{
}

- (void) buildNext: (struct tuple *)tuple
{
	[self replace: NULL: tuple: BOX_ADD];
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
	      [self replace: NULL: tuple: BOX_ADD];
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

- (struct tuple *) findByTuple: (struct tuple *) tuple
{
	/* Hash index currently is always single-part. */
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	if (field == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD,
			  key_def->parts[0].fieldno);
	return [self findUnsafe :field :1];
}

- (void) replaceNode: (void *) h: (void *) node: (void **) pprev
{
	(void) h;
	(void) node;
	(void) pprev;
	[self subclassResponsibility: _cmd];
}

- (void) deleteNode: (void *) h: (void *) node: (void **) pprev
{
	(void) h;
	(void) node;
	(void) pprev;
	[self subclassResponsibility: _cmd];
}

- (struct tuple *) replace: (struct tuple *) old_tuple
			  : (struct tuple *) new_tuple
			  : (u32) flags
{
	assert (old_tuple != NULL || new_tuple != NULL);
	assert(key_def->is_unique);

	void *node1 = alloca([self node_size]);
	void *node2 = alloca([self node_size]);

	if (new_tuple && !old_tuple)  {
		/* Case #1: replace(new_tuple); */

		void *new_node = node1;
		void *old_node = node2;

		[self fold: new_node: new_tuple];
		[self replaceNode: hash: new_node: &old_node];

		if (unlikely(old_node && (flags & BOX_ADD))) {
			/* Rollback changes */
			[self replaceNode: hash: old_node: NULL];
			tnt_raise(ClientError, :ER_TUPLE_FOUND);
		}

		if (unlikely(!old_node && (flags & BOX_REPLACE))) {
			/* Rollback changes */
			[self deleteNode: hash: new_node: NULL];
			tnt_raise(ClientError, :ER_TUPLE_NOT_FOUND);
		}

		/* Return a removed node */
		return [self unfold: old_node];
	} else if (!new_tuple && old_tuple) {
		/* Case #2: remove(old_tuple) */

		void *old_node  = node1;
		void *old_node2 = node2;

		[self fold: old_node: old_tuple];
		[self deleteNode: hash: old_node: &old_node2];
#if 0		/* TODO: A new undocumented feature */
		if (unlikely((flags & BOX_REPLACE) && !old_node2)) {
			/* Rollback changes */
			[self replaceNode: hash: old_node: NULL];
			tnt_raise(ClientError, :ER_TUPLE_NOT_FOUND);
		}
#endif
		return [self unfold: old_node2];
	} else /* (old_tuple != NULL && new_tuple != NULL) */ {
		/* Case #3: remove(old_tuple); insert(new_tuple) */

		/* BOX_ADD is only supported in this case */
		assert(flags & BOX_ADD);

		void *new_node = node1;
		void *old_node = node2;

		[self fold: new_node: new_tuple];
		[self replaceNode: hash: new_node: &old_node];

		struct tuple *ret = NULL;
		if (old_node) {
			ret = [self unfold: old_node];

			if (unlikely(ret != old_tuple)) {
				/* Rollback changes */
				[self replaceNode: hash: old_node: NULL];
				tnt_raise(ClientError, :ER_TUPLE_FOUND);
			}
		} else {
			void *old_node  = node1;
			void *old_node2 = node2;
			[self fold: old_node: old_tuple];
			[self deleteNode: hash: old_node: &old_node2];

			ret = [self unfold: old_node2];
		}

		return ret;
	}

	return NULL;
}

@end

/* }}} */

/* {{{ Hash32Index ************************************************/

static u32
int32_key_to_value(void *key)
{
	u32 key_size = load_varint32(&key);
	if (key_size != 4)
		tnt_raise(ClientError, :ER_KEY_FIELD_TYPE, "u32");
	return *((u32 *) key);
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
		hash = int_hash = mh_i32ptr_init();
	}
	return self;
}

- (size_t) node_size
{
	return sizeof(struct mh_i32ptr_node_t);
}

- (void) fold: (void *) node :(struct tuple *) tuple
{
	struct mh_i32ptr_node_t *node_x = node;
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	u32 num = int32_key_to_value(field);
	node_x->key = num;
	node_x->val = tuple;
}

- (struct tuple *) unfold: (const void *) node
{
	const struct mh_i32ptr_node_t *node_x = node;
	return node_x ? node_x->val : NULL;
}

- (size_t) size
{
	return mh_size(int_hash);
}

- (struct tuple *) findUnsafe: (void *) key :(int) part_count
{
	(void) part_count;

	struct tuple *ret = NULL;
	u32 num = int32_key_to_value(key);
	const struct mh_i32ptr_node_t node = { .key = num };
	mh_int_t k = mh_i32ptr_get(int_hash, &node, NULL, NULL);
	if (k != mh_end(int_hash))
		ret = mh_i32ptr_node(int_hash, k)->val;
#ifdef DEBUG
	say_debug("Hash32Index find(self:%p, key:%i) = %p", self, num, ret);
#endif
	return ret;
}

- (void) replaceNode: (void *) h: (void *) node: (void **) pprev
{
	struct mh_i32ptr_node_t *node_x = (struct mh_i32ptr_node_t *) node;
	struct mh_i32ptr_node_t **pprev_x = (struct mh_i32ptr_node_t **) pprev;
	mh_i32ptr_replace(h, node_x, pprev_x, NULL, NULL);
}

- (void) deleteNode: (void *) h: (void *) node: (void **) pprev
{
	const struct mh_i32ptr_node_t *node_x = (struct mh_i32ptr_node_t *) node;

	mh_int_t k = mh_i32ptr_get(h, node_x, NULL, NULL);
	if (k != mh_end(int_hash) &&
			mh_i32ptr_node(int_hash, k)->val == node_x->val) {
		if (pprev) {
			memcpy(*pprev, mh_i32ptr_node(int_hash, k),
			       sizeof(struct mh_i32ptr_node_t));
		}

		mh_i32ptr_del(h, k, NULL, NULL);
	} else {
		pprev = NULL;
	}
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

	switch (type) {
	case ITER_GE:
		if (key != NULL) {
			check_key_parts(key_def, part_count,
					traits->allows_partial_key);
			u32 num = int32_key_to_value(key);
			const struct mh_i32ptr_node_t node = { .key = num };
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
		u32 num = int32_key_to_value(key);
		const struct mh_i32ptr_node_t node = { .key = num };
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

static u64
int64_key_to_value(void *key)
{
	u32 key_size = load_varint32(&key);
	if (key_size != 8)
		tnt_raise(ClientError, :ER_KEY_FIELD_TYPE, "u64");
	return *((u64 *) key);
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
		hash = int64_hash = mh_i64ptr_init();
	}
	return self;
}

- (size_t) node_size
{
	return sizeof(struct mh_i64ptr_node_t);
}

- (void) fold: (void *) node :(struct tuple *) tuple
{
	struct mh_i64ptr_node_t *node_x = node;
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	u64 num = int64_key_to_value(field);
	node_x->key = num;
	node_x->val = tuple;
}

- (struct tuple *) unfold: (const void *) node
{
	const struct mh_i64ptr_node_t *node_x = node;
	return node_x ? node_x->val : NULL;
}

- (size_t) size
{
	return mh_size(int64_hash);
}

- (struct tuple *) findUnsafe: (void *) key :(int) part_count
{
	(void) part_count;

	struct tuple *ret = NULL;
	u64 num = int64_key_to_value(key);
	const struct mh_i64ptr_node_t node = { .key = num };
	mh_int_t k = mh_i64ptr_get(int64_hash, &node, NULL, NULL);
	if (k != mh_end(int64_hash))
		ret = mh_i64ptr_node(int64_hash, k)->val;
#ifdef DEBUG
	say_debug("Hash64Index find(self:%p, key:%"PRIu64") = %p", self, num, ret);
#endif
	return ret;
}

- (void) replaceNode: (void *) h: (void *) node: (void **) pprev
{
	mh_i64ptr_replace(h, (struct mh_i64ptr_node_t *) node,
			     (struct mh_i64ptr_node_t **) pprev, NULL, NULL);
}

- (void) deleteNode: (void *) h: (void *) node: (void **) pprev
{
	const struct mh_i64ptr_node_t *node_x = (struct mh_i64ptr_node_t *) node;

	mh_int_t k = mh_i64ptr_get(h, node_x, NULL, NULL);
	if (k != mh_end(int64_hash) &&
			mh_i64ptr_node(int64_hash, k)->val == node_x->val) {
		if (pprev) {
			memcpy(*pprev, mh_i64ptr_node(int64_hash, k),
			       sizeof(struct mh_i64ptr_node_t));
		}

		mh_i64ptr_del(h, k, NULL, NULL);
	} else {
		pprev = NULL;
	}
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

	switch (type) {
	case ITER_GE:
		if (key != NULL) {
			check_key_parts(key_def, part_count,
					traits->allows_partial_key);
			u64 num = int64_key_to_value(key);
			const struct mh_i64ptr_node_t node = { .key = num };
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
		u64 num = int64_key_to_value(key);
		const struct mh_i64ptr_node_t node = { .key = num };
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
		hash = str_hash = mh_lstrptr_init();
	}
	return self;
}

- (size_t) node_size
{
    return sizeof(struct mh_lstrptr_node_t);
}

- (void) fold: (void *) node :(struct tuple *) tuple
{
	struct mh_lstrptr_node_t *node_x = node;
	void *field = tuple_field(tuple, key_def->parts[0].fieldno);
	if (field == NULL)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD,
			  key_def->parts[0].fieldno);

	node_x->key = field;
	node_x->val = tuple;
}

- (struct tuple *) unfold: (const void *) node
{
    const struct mh_lstrptr_node_t *node_x = node;
    return node_x ? node_x->val : NULL;
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

- (void) replaceNode: (void *) h: (void *) node: (void **) pprev
{
	struct mh_lstrptr_node_t *node_x = (struct mh_lstrptr_node_t *) node;
	struct mh_lstrptr_node_t **pprev_x = (struct mh_lstrptr_node_t **) pprev;
	mh_lstrptr_replace(h, node_x, pprev_x, NULL, NULL);
}

- (void) deleteNode: (void *) h: (void *) node: (void **) pprev
{
	const struct mh_lstrptr_node_t *node_x = (struct mh_lstrptr_node_t *) node;

	mh_int_t k = mh_lstrptr_get(h, node_x, NULL, NULL);
	if (k != mh_end(str_hash) &&
			mh_lstrptr_node(str_hash, k)->val == node_x->val) {
		if (pprev) {
			memcpy(*pprev, mh_lstrptr_node(str_hash, k),
			       sizeof(struct mh_lstrptr_node_t));
		}

		mh_lstrptr_del(h, k, NULL, NULL);
	} else {
		pprev = NULL;
	}
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

	switch (type) {
	case ITER_GE:
		if (key != NULL) {
			check_key_parts(key_def, part_count,
					traits->allows_partial_key);
			const struct mh_lstrptr_node_t node = { .key = key };
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
		const struct mh_lstrptr_node_t node = { .key = key };
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

