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
#include "errinj.h"

#include "third_party/PMurHash.h"

enum {
	HASH_SEED = 13U
};

static inline bool
mh_index_eq(struct tuple *const *tuple_a, struct tuple *const *tuple_b,
	    const struct key_def *key_def)
{
	return tuple_compare(*tuple_a, *tuple_b, key_def) == 0;
}

static inline bool
mh_index_eq_key(const char *key, struct tuple *const *tuple,
		const struct key_def *key_def)
{
	return tuple_compare_with_key(*tuple, key, key_def->part_count,
				      key_def) == 0;
}


static inline uint32_t
mh_index_hash(struct tuple *const *tuple, const struct key_def *key_def)
{
	const struct key_part *part = key_def->parts;
	/*
	 * Speed up the simplest case when we have a
	 * single-part hash over an integer field.
	 */
	if (key_def->part_count == 1 && part->type == NUM) {
		const char *field = tuple_field(*tuple, part->fieldno);
		uint64_t val = mp_decode_uint(&field);
		if (likely(val <= UINT32_MAX))
			return val;
		return ((uint32_t)((val)>>33^(val)^(val)<<11));
	}

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	for ( ; part < key_def->parts + key_def->part_count; part++) {
		const char *field = tuple_field(*tuple, part->fieldno);
		const char *f = field;
		mp_next(&f);
		uint32_t size = f - field;
		assert(size < INT32_MAX);
		PMurHash32_Process(&h, &carry, field, size);
		total_size += size;
	}

	return PMurHash32_Result(h, carry, total_size);
}

static inline uint32_t
mh_index_hash_key(const char *key, const struct key_def *key_def)
{
	const struct key_part *part = key_def->parts;

	if (key_def->part_count == 1 && part->type == NUM) {
		uint64_t val = mp_decode_uint(&key);
		if (likely(val <= UINT32_MAX))
			return val;
		return ((uint32_t)((val)>>33^(val)^(val)<<11));
	}

	/* Calculate key size */
	const char *k = key;
	for (uint32_t part = 0; part < key_def->part_count; part++) {
		mp_next(&k);
	}

	return PMurHash32(HASH_SEED, key, k - key);
}

#define mh_int_t uint32_t
#define mh_arg_t const struct key_def *

#define mh_hash(a, arg) mh_index_hash(a, arg)
#define mh_hash_key(a, arg) mh_index_hash_key(a, arg)
#define mh_eq(a, b, arg) mh_index_eq(a, b, arg)
#define mh_eq_key(a, b, arg) mh_index_eq_key(a, b, arg)

#define mh_key_t const char *
typedef struct tuple * mh_node_t;
#define mh_name _index
#define MH_SOURCE 1
#include "salad/mhash.h"

/* {{{ HashIndex Iterators ****************************************/

struct hash_iterator {
	struct iterator base; /* Must be the first member. */
	struct mh_index_t *hash;
	uint32_t h_pos;
};

void
hash_iterator_free(struct iterator *iterator)
{
	assert(iterator->free == hash_iterator_free);
	free(iterator);
}

struct tuple *
hash_iterator_ge(struct iterator *ptr)
{
	assert(ptr->free == hash_iterator_free);
	struct hash_iterator *it = (struct hash_iterator *) ptr;

	while (it->h_pos < mh_end(it->hash)) {
		if (mh_exist(it->hash, it->h_pos))
			return *mh_index_node(it->hash, it->h_pos++);
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
hash_iterator_eq(struct iterator *it)
{
	it->next = hash_iterator_eq_next;
	return hash_iterator_ge(it);
}

/* }}} */

/* {{{ HashIndex -- implementation of all hashes. **********************/

HashIndex::HashIndex(struct key_def *key_def)
	: Index(key_def)
{
	hash = mh_index_new();
	if (hash == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE, sizeof(hash),
			  "HashIndex", "hash");
	}
}

HashIndex::~HashIndex()
{
	mh_index_delete(hash);
}

void
HashIndex::reserve(uint32_t size_hint)
{
	mh_index_reserve(hash, size_hint, key_def);
}

size_t
HashIndex::size() const
{
	return mh_size(hash);
}

size_t
HashIndex::memsize() const
{
        return mh_index_memsize(hash);
}

struct tuple *
HashIndex::random(uint32_t rnd) const
{
	uint32_t k = mh_index_random(hash, rnd);
	if (k != mh_end(hash))
		return *mh_index_node(hash, k);
	return NULL;
}

struct tuple *
HashIndex::findByKey(const char *key, uint32_t part_count) const
{
	assert(key_def->is_unique && part_count == key_def->part_count);
	(void) part_count;

	struct tuple *ret = NULL;
	uint32_t k = mh_index_find(hash, key, key_def);
	if (k != mh_end(hash))
		ret = *mh_index_node(hash, k);
	return ret;
}

struct tuple *
HashIndex::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		   enum dup_replace_mode mode)
{
	uint32_t errcode;

	if (new_tuple) {
		struct tuple *dup_tuple = NULL;
		struct tuple **dup_node = &dup_tuple;
		uint32_t pos = mh_index_put(hash, &new_tuple,
					    &dup_node, key_def);

		ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		{
			mh_index_del(hash, pos, key_def);
			pos = mh_end(hash);
		});

		if (pos == mh_end(hash)) {
			tnt_raise(LoggedError, ER_MEMORY_ISSUE, (ssize_t) pos,
				  "hash", "key");
		}
		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			mh_index_remove(hash, &new_tuple, key_def);
			if (dup_tuple) {
				pos = mh_index_put(hash, &dup_tuple, NULL,
						   key_def);
				if (pos == mh_end(hash)) {
					panic("Failed to allocate memory in "
					      "recover of int hash");
				}
			}
			tnt_raise(ClientError, errcode, index_id(this));
		}

		if (dup_tuple)
			return dup_tuple;
	}

	if (old_tuple) {
		mh_index_remove(hash, &old_tuple, key_def);
	}
	return old_tuple;
}

struct iterator *
HashIndex::allocIterator() const
{
	struct hash_iterator *it = (struct hash_iterator *)
			calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE,
			  sizeof(struct hash_iterator),
			  "HashIndex", "iterator");
	}

	it->base.next = hash_iterator_ge;
	it->base.free = hash_iterator_free;
	return (struct iterator *) it;
}

void
HashIndex::initIterator(struct iterator *ptr, enum iterator_type type,
			const char *key, uint32_t part_count) const
{
	assert(key != NULL || part_count == 0);
	(void) part_count;
	assert(ptr->free == hash_iterator_free);

	struct hash_iterator *it = (struct hash_iterator *) ptr;

	switch (type) {
	case ITER_ALL:
		it->h_pos = mh_begin(hash);
		it->base.next = hash_iterator_ge;
		break;
	case ITER_EQ:
		assert(part_count > 0);
		it->h_pos = mh_index_find(hash, key, key_def);
		it->base.next = hash_iterator_eq;
		break;
	default:
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  "Hash index", "requested iterator type");
	}
	it->hash = hash;
}
/* }}} */
