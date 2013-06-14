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
#include "hash_index.h"
#include "say.h"
#include "tuple.h"
#include "pickle.h"
#include "exception.h"
#include "space.h"
#include "assoc.h"
#include "errinj.h"

static struct index_traits hash_index_traits = {
	/* .allows_partial_key = */ false,
};

/* {{{ HashIndex Iterators ****************************************/

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
			return (struct tuple *) mh_i32ptr_node(it->hash, it->h_pos++)->val;
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
			return (struct tuple *) mh_i64ptr_node(
						it->hash,it->h_pos++)->val;
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
			return (struct tuple *) mh_lstrptr_node(
						it->hash,it->h_pos++)->val;
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

/* }}} */

/* {{{ HashIndex -- base class for all hashes. ********************/

class Hash32Index: public HashIndex {
public:
	Hash32Index(struct key_def *key_def, struct space *space);
	~Hash32Index();

	virtual size_t
	size() const;

	virtual struct tuple *
	random(u32 rnd) const;

	virtual struct tuple *
	findByKey(const char *key, u32 part_count) const;

	virtual struct tuple *
	replace(struct tuple *old_tuple, struct tuple *new_tuple,
		enum dup_replace_mode mode);

	struct iterator *
	allocIterator() const;

	void
	initIterator(struct iterator *iterator, enum iterator_type type,
		     const char *key, u32 part_count) const;

	virtual void
	reserve(u32 n_tuples);
private:
	struct mh_i32ptr_t *int_hash;
};

class Hash64Index: public HashIndex {
public:
	Hash64Index(struct key_def *key_def, struct space *space);
	~Hash64Index();

	virtual size_t
	size() const;

	virtual struct tuple *
	random(u32 rnd) const;

	virtual struct tuple *
	findByKey(const char *key, u32 part_count) const;

	virtual struct tuple *
	replace(struct tuple *old_tuple, struct tuple *new_tuple,
		enum dup_replace_mode mode);

	struct iterator *
	allocIterator() const;

	void
	initIterator(struct iterator *iterator, enum iterator_type type,
		     const char *key, u32 part_count) const;

	virtual void
	reserve(u32 n_tuples);
private:
	struct mh_i64ptr_t *int64_hash;
};

class HashStrIndex: public HashIndex {
public:
	HashStrIndex(struct key_def *key_def, struct space *space);
	~HashStrIndex();

	virtual size_t
	size() const;

	virtual struct tuple *
	random(u32 rnd) const;

	virtual struct tuple *
	findByKey(const char *key, u32 part_count) const;

	virtual struct tuple *
	replace(struct tuple *old_tuple, struct tuple *new_tuple,
		enum dup_replace_mode mode);

	struct iterator *
	allocIterator() const;

	void
	initIterator(struct iterator *iterator, enum iterator_type type,
		     const char *key, u32 part_count) const;

	virtual void
	reserve(u32 n_tuples);
private:
	struct mh_lstrptr_t *str_hash;
};

HashIndex *
HashIndex::factory(struct key_def *key_def, struct space *space)
{
	/*
	 * Hash index always has a single-field key.
	 */
	switch (key_def->parts[0].type) {
	case NUM:
		return new Hash32Index(key_def, space);  /* 32-bit integer hash */
	case NUM64:
		return new Hash64Index(key_def, space);  /* 64-bit integer hash */
	case STRING:
		return new HashStrIndex(key_def, space); /* string hash */
	default:
		assert(false);
	}

	return NULL;
}

HashIndex::HashIndex(struct key_def *key_def, struct space *space)
	: Index(key_def, space)
{
	/* Nothing */
}

void
HashIndex::beginBuild()
{
}

void
HashIndex::buildNext(struct tuple *tuple)
{
	replace(NULL, tuple, DUP_INSERT);
}

void
HashIndex::endBuild()
{
}

void
HashIndex::build(Index *pk)
{
	u32 n_tuples = pk->size();

	if (n_tuples == 0)
		return;

	reserve(n_tuples);

	say_info("Adding %" PRIu32 " keys to HASH index %"
		 PRIu32 "...", n_tuples, index_n(this));

	struct iterator *it = pk->position();
	struct tuple *tuple;
	pk->initIterator(it, ITER_ALL, NULL, 0);

	while ((tuple = it->next(it)))
	      replace(NULL, tuple, DUP_INSERT);
}

struct tuple *
HashIndex::min() const
{
	tnt_raise(ClientError, ER_UNSUPPORTED, "Hash index", "min()");
	return NULL;
}

struct tuple *
HashIndex::max() const
{
	tnt_raise(ClientError, ER_UNSUPPORTED, "Hash index", "max()");
	return NULL;
}

struct tuple *
HashIndex::findByTuple(struct tuple *tuple) const
{
	assert(key_def->is_unique);
	if (tuple->field_count < key_def->max_fieldno)
		tnt_raise(IllegalParams, "tuple must have all indexed fields");

	/* Hash index currently is always single-part. */
	const char *field = tuple_field_old(tuple, key_def->parts[0].fieldno);
	return findByKey(field, 1);
}

/* }}} */

/* {{{ Hash32Index ************************************************/

static inline struct mh_i32ptr_node_t
int32_key_to_node(const char *key)
{
	u32 key_size = load_varint32(&key);
	if (key_size != 4)
		tnt_raise(ClientError, ER_KEY_FIELD_TYPE, "u32");
	struct mh_i32ptr_node_t node = { *(u32 *) key, NULL };
	return node;
}

static inline struct mh_i32ptr_node_t
int32_tuple_to_node(struct tuple *tuple, struct key_def *key_def)
{
	const char *field = tuple_field_old(tuple, key_def->parts[0].fieldno);
	struct mh_i32ptr_node_t node = int32_key_to_node(field);
	node.val = tuple;
	return node;
}


Hash32Index::Hash32Index(struct key_def *key_def, struct space *space)
	: HashIndex(key_def, space)
{
	int_hash = mh_i32ptr_new();
}

Hash32Index::~Hash32Index()
{
	mh_i32ptr_delete(int_hash);
}

void
Hash32Index::reserve(u32 n_tuples)
{
	mh_i32ptr_reserve(int_hash, n_tuples, NULL);
}

size_t
Hash32Index::size() const
{
	return mh_size(int_hash);
}


struct tuple *
Hash32Index::random(u32 rnd) const
{
	mh_int_t k = mh_i32ptr_random(int_hash, rnd);
	if (k != mh_end(int_hash))
		return (struct tuple *) mh_i32ptr_node(int_hash, k)->val;
	return NULL;
}

struct tuple *
Hash32Index::findByKey(const char *key, u32 part_count) const
{
	assert(key_def->is_unique);
	check_key_parts(key_def, part_count, false);

	(void) part_count;

	struct tuple *ret = NULL;
	struct mh_i32ptr_node_t node = int32_key_to_node(key);
	mh_int_t k = mh_i32ptr_get(int_hash, &node, NULL);
	if (k != mh_end(int_hash))
		ret = (struct tuple *) mh_i32ptr_node(int_hash, k)->val;
#ifdef DEBUG
	say_debug("Hash32Index find(self:%p, key:%i) = %p", self, node.key, ret);
#endif
	return ret;
}

struct tuple *
Hash32Index::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		     enum dup_replace_mode mode)
{
	struct mh_i32ptr_node_t new_node, old_node;
	uint32_t errcode;

	if (new_tuple) {
		struct mh_i32ptr_node_t *dup_node = &old_node;
		new_node = int32_tuple_to_node(new_tuple, key_def);
		mh_int_t pos = mh_i32ptr_put(int_hash, &new_node,
					     &dup_node, NULL);

		ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		{
			mh_i32ptr_del(int_hash, pos, NULL);
			pos = mh_end(int_hash);
		});

		if (pos == mh_end(int_hash)) {
			tnt_raise(LoggedError, ER_MEMORY_ISSUE, (ssize_t) pos,
				  "int hash", "key");
		}
		struct tuple *dup_tuple = (dup_node ?
					   (struct tuple *) dup_node->val
					   : NULL);
		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			mh_i32ptr_remove(int_hash, &new_node, NULL);
			if (dup_node) {
				pos = mh_i32ptr_put(int_hash, dup_node,
						    NULL, NULL);
				if (pos == mh_end(int_hash)) {
					panic("Failed to allocate memory in "
					      "recover of int hash");
				}
			}
			tnt_raise(ClientError, errcode, index_n(this));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		old_node = int32_tuple_to_node(old_tuple, key_def);
		mh_i32ptr_remove(int_hash, &old_node, NULL);
	}
	return old_tuple;
}


struct iterator *
Hash32Index::allocIterator() const
{
	struct hash_i32_iterator *it = (struct hash_i32_iterator *)
			malloc(sizeof(struct hash_i32_iterator));
	if (it) {
		memset(it, 0, sizeof(*it));
		it->base.next = hash_iterator_i32_ge;
		it->base.free = hash_iterator_free;
	}
	return (struct iterator *) it;
}

void
Hash32Index::initIterator(struct iterator *ptr, enum iterator_type type,
			  const char *key, u32 part_count) const
{
	assert(ptr->free == hash_iterator_free);
	struct hash_i32_iterator *it = (struct hash_i32_iterator *) ptr;
	struct mh_i32ptr_node_t node;

	switch (type) {
	case ITER_GE:
		if (key != NULL) {
			check_key_parts(key_def, part_count,
					hash_index_traits.allows_partial_key);
			node = int32_key_to_node(key);
			it->h_pos = mh_i32ptr_get(int_hash, &node, NULL);
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
				hash_index_traits.allows_partial_key);
		node = int32_key_to_node(key);
		it->h_pos = mh_i32ptr_get(int_hash, &node, NULL);
		it->base.next = hash_iterator_i32_eq;
		break;
	default:
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  "Hash index", "requested iterator type");
	}
	it->hash = int_hash;
}

/* }}} */

/* {{{ Hash64Index ************************************************/

static inline struct mh_i64ptr_node_t
int64_key_to_node(const char *key)
{
	u32 key_size = load_varint32(&key);
	if (key_size != 8)
		tnt_raise(ClientError, ER_KEY_FIELD_TYPE, "u64");
	struct mh_i64ptr_node_t node = { *(u64 *) key, NULL };
	return node;
}

static inline struct mh_i64ptr_node_t
int64_tuple_to_node(struct tuple *tuple, struct key_def *key_def)
{
	const char *field = tuple_field_old(tuple, key_def->parts[0].fieldno);
	struct mh_i64ptr_node_t node = int64_key_to_node(field);
	node.val = tuple;
	return node;
}

Hash64Index::Hash64Index(struct key_def *key_def, struct space *space)
	: HashIndex(key_def, space)
{
	int64_hash = mh_i64ptr_new();
}

Hash64Index::~Hash64Index()
{
	mh_i64ptr_delete(int64_hash);
}

void
Hash64Index::reserve(u32 n_tuples)
{
	mh_i64ptr_reserve(int64_hash, n_tuples, NULL);
}

size_t
Hash64Index::size() const
{
	return mh_size(int64_hash);
}

struct tuple *
Hash64Index::random(u32 rnd) const
{
	mh_int_t k = mh_i64ptr_random(int64_hash, rnd);
	if (k != mh_end(int64_hash))
		return (struct tuple *) mh_i64ptr_node(int64_hash, k)->val;
	return NULL;
}

struct tuple *
Hash64Index::findByKey(const char *key, u32 part_count) const
{
	assert(key_def->is_unique);
	check_key_parts(key_def, part_count, false);

	(void) part_count;

	struct tuple *ret = NULL;
	struct mh_i64ptr_node_t node = int64_key_to_node(key);
	mh_int_t k = mh_i64ptr_get(int64_hash, &node, NULL);
	if (k != mh_end(int64_hash))
		ret = (struct tuple *) mh_i64ptr_node(int64_hash, k)->val;
#ifdef DEBUG
	say_debug("Hash64Index find(self:%p, key:%i) = %p", self, node.key, ret);
#endif
	return ret;
}

struct tuple *
Hash64Index::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		     enum dup_replace_mode mode)
{
	struct mh_i64ptr_node_t new_node, old_node;
	uint32_t errcode;

	if (new_tuple) {
		struct mh_i64ptr_node_t *dup_node = &old_node;
		new_node = int64_tuple_to_node(new_tuple, key_def);
		mh_int_t pos = mh_i64ptr_put(int64_hash, &new_node,
					     &dup_node, NULL);

		ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		{
			mh_i64ptr_del(int64_hash, pos, NULL);
			pos = mh_end(int64_hash);
		});

		if (pos == mh_end(int64_hash)) {
			tnt_raise(LoggedError, ER_MEMORY_ISSUE, (ssize_t) pos,
				  "int hash", "key");
		}
		struct tuple *dup_tuple = (dup_node ?
					   (struct tuple *) dup_node->val :
					   NULL);
		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			mh_i64ptr_remove(int64_hash, &new_node, NULL);
			if (dup_node) {
				pos = mh_i64ptr_put(int64_hash, dup_node, NULL, NULL);
				if (pos == mh_end(int64_hash)) {
					panic("Failed to allocate memory in "
					      "recover of int hash");
				}
			}
			tnt_raise(ClientError, errcode, index_n(this));
		}

		if (dup_tuple)
			return dup_tuple;
	}

	if (old_tuple) {
		old_node = int64_tuple_to_node(old_tuple, key_def);
		mh_i64ptr_remove(int64_hash, &old_node, NULL);
	}

	return old_tuple;
}


struct iterator *
Hash64Index::allocIterator() const
{
	struct hash_i64_iterator *it = (struct hash_i64_iterator *)
			malloc(sizeof(struct hash_i64_iterator));
	if (it) {
		memset(it, 0, sizeof(*it));
		it->base.next = hash_iterator_i64_ge;
		it->base.free = hash_iterator_free;
	}

	return (struct iterator *) it;
}

void
Hash64Index::initIterator(struct iterator *ptr, enum iterator_type type,
			  const char *key, u32 part_count) const
{
	assert(ptr->free == hash_iterator_free);
	struct hash_i64_iterator *it = (struct hash_i64_iterator *) ptr;
	struct mh_i64ptr_node_t node;

	switch (type) {
	case ITER_GE:
		if (key != NULL) {
			check_key_parts(key_def, part_count,
					hash_index_traits.allows_partial_key);
			node = int64_key_to_node(key);
			it->h_pos = mh_i64ptr_get(int64_hash, &node, NULL);
			it->base.next = hash_iterator_i64_ge;
			break;
		}
		/* Fall through. */
	case ITER_ALL:
		it->h_pos = mh_begin(int64_hash);
		it->base.next = hash_iterator_i64_ge;
		break;
	case ITER_EQ:
		check_key_parts(key_def, part_count,
			hash_index_traits.allows_partial_key);
		node = int64_key_to_node(key);
		it->h_pos = mh_i64ptr_get(int64_hash, &node, NULL);
		it->base.next = hash_iterator_i64_eq;
		break;
	default:
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  "Hash index", "requested iterator type");
	}
	it->hash = int64_hash;
}

/* }}} */

/* {{{ HashStrIndex ***********************************************/

static inline struct mh_lstrptr_node_t
lstrptr_tuple_to_node(struct tuple *tuple, struct key_def *key_def)
{
	const char *field = tuple_field_old(tuple, key_def->parts[0].fieldno);
	if (field == NULL)
		tnt_raise(ClientError, ER_NO_SUCH_FIELD,
			  key_def->parts[0].fieldno);

	struct mh_lstrptr_node_t node = { field, tuple };
	return node;
}

HashStrIndex::HashStrIndex(struct key_def *key_def, struct space *space)
	: HashIndex(key_def, space)
{
	str_hash = mh_lstrptr_new();
}

HashStrIndex::~HashStrIndex()
{
	mh_lstrptr_delete(str_hash);
}

void
HashStrIndex::reserve(u32 n_tuples)
{
	mh_lstrptr_reserve(str_hash, n_tuples, NULL);
}


size_t
HashStrIndex::size() const
{
	return mh_size(str_hash);
}

struct tuple *
HashStrIndex::random(u32 rnd) const
{
	mh_int_t k = mh_lstrptr_random(str_hash, rnd);
	if (k != mh_end(str_hash))
		return (struct tuple *) mh_lstrptr_node(str_hash, k)->val;
	return NULL;
}

struct tuple *
HashStrIndex::findByKey(const char *key, u32 part_count) const
{
	assert(key_def->is_unique);
	check_key_parts(key_def, part_count, false);

	struct tuple *ret = NULL;
	const struct mh_lstrptr_node_t node = { key, NULL };
	mh_int_t k = mh_lstrptr_get(str_hash, &node, NULL);
	if (k != mh_end(str_hash))
		ret = (struct tuple *) mh_lstrptr_node(str_hash, k)->val;
#ifdef DEBUG
	u32 key_size = load_varint32(&key);
	say_debug("HashStrIndex find(self:%p, key:(%i)'%.*s') = %p",
		  self, key_size, key_size, key, ret);
#endif
	return ret;
}

struct tuple *
HashStrIndex::replace(struct tuple *old_tuple, struct tuple *new_tuple,
	enum dup_replace_mode mode)
{
	struct mh_lstrptr_node_t new_node, old_node;
	uint32_t errcode;

	if (new_tuple) {
		struct mh_lstrptr_node_t *dup_node = &old_node;
		new_node = lstrptr_tuple_to_node(new_tuple, key_def);
		mh_int_t pos = mh_lstrptr_put(str_hash, &new_node,
					      &dup_node, NULL);

		ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		{
			mh_lstrptr_del(str_hash, pos, NULL);
			pos = mh_end(str_hash);
		});

		if (pos == mh_end(str_hash)) {
			tnt_raise(LoggedError, ER_MEMORY_ISSUE, (ssize_t) pos,
				  "str hash", "key");
		}
		struct tuple *dup_tuple = dup_node
				? (struct tuple *) dup_node->val
				: NULL;
		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			mh_lstrptr_remove(str_hash, &new_node, NULL);
			if (dup_node) {
				pos = mh_lstrptr_put(str_hash, dup_node,
						     NULL, NULL);
				if (pos == mh_end(str_hash)) {
					panic("Failed to allocate memory in "
					      "recover of str hash");
				}
			}
			tnt_raise(ClientError, errcode, index_n(this));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		old_node = lstrptr_tuple_to_node(old_tuple, key_def);
		mh_lstrptr_remove(str_hash, &old_node, NULL);
	}
	return old_tuple;
}

struct iterator *
HashStrIndex::allocIterator() const
{
	struct hash_lstr_iterator *it = (struct hash_lstr_iterator *)
			malloc(sizeof(struct hash_lstr_iterator));
	if (it) {
		memset(it, 0, sizeof(*it));
		it->base.next = hash_iterator_lstr_ge;
		it->base.free = hash_iterator_free;
	}
	return (struct iterator *) it;
}

void
HashStrIndex::initIterator(struct iterator *ptr, enum iterator_type type,
			   const char *key, u32 part_count) const
{
	(void) part_count;

	assert(ptr->free == hash_iterator_free);
	struct hash_lstr_iterator *it = (struct hash_lstr_iterator *) ptr;
	struct mh_lstrptr_node_t node;

	switch (type) {
	case ITER_GE:
		if (key != NULL) {
			check_key_parts(key_def, part_count,
					hash_index_traits.allows_partial_key);
			node.key = key;
			it->h_pos = mh_lstrptr_get(str_hash, &node, NULL);
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
				hash_index_traits.allows_partial_key);
		node.key = key;
		it->h_pos = mh_lstrptr_get(str_hash, &node, NULL);
		it->base.next = hash_iterator_lstr_eq;
		break;
	default:
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  "Hash index", "requested iterator type");
	}
	it->hash = str_hash;
}

/* }}} */

