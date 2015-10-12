/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
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
#include "memtx_hash.h"
#include "say.h"
#include "tuple.h"
#include "memtx_engine.h"
#include "space.h"
#include "schema.h" /* space_cache_find() */
#include "errinj.h"

#include "third_party/PMurHash.h"

enum {
	HASH_SEED = 13U
};

static inline bool
equal(struct tuple *tuple_a, struct tuple *tuple_b,
	    const struct key_def *key_def)
{
	return tuple_compare(tuple_a, tuple_b, key_def) == 0;
}

static inline bool
equal_key(struct tuple *tuple, const char *key,
		const struct key_def *key_def)
{
	return tuple_compare_with_key(tuple, key, key_def->part_count,
					       key_def) == 0;
}

static inline uint32_t
mh_hash_field(uint32_t *ph1, uint32_t *pcarry, const char **field,
	      enum field_type type)
{
	const char *f = *field;
	uint32_t size;

	switch (type) {
	case STRING:
		/*
		 * (!) MP_STR fields hashed **excluding** MsgPack format
		 * indentifier. We have to do that to keep compatibility
		 * with old third-party MsgPack (spec-old.md) implementations.
		 * \sa https://github.com/tarantool/tarantool/issues/522
		 */
		f = mp_decode_str(field, &size);
		break;
	default:
		mp_next(field);
		size = *field - f;  /* calculate the size of field */
		/*
		 * (!) All other fields hashed **including** MsgPack format
		 * identifier (e.g. 0xcc). This was done **intentionally**
		 * for performance reasons. Please follow MsgPack specification
		 * and pack all your numbers to the most compact representation.
		 * If you still want to add support for broken MsgPack,
		 * please don't forget to patch tuple_compare_field().
		 */
		break;
	}
	assert(size < INT32_MAX);
	PMurHash32_Process(ph1, pcarry, f, size);
	return size;
}

static inline uint32_t
tuple_hash(struct tuple *tuple, const struct key_def *key_def)
{
	const struct key_part *part = key_def->parts;
	/*
	 * Speed up the simplest case when we have a
	 * single-part hash_table over an integer field.
	 */
	if (key_def->part_count == 1 && part->type == NUM) {
		const char *field = tuple_field(tuple, part->fieldno);
		uint64_t val = mp_decode_uint(&field);
		if (likely(val <= UINT32_MAX))
			return val;
		return ((uint32_t)((val)>>33^(val)^(val)<<11));
	}

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	for ( ; part < key_def->parts + key_def->part_count; part++) {
		const char *field = tuple_field(tuple, part->fieldno);
		total_size += mh_hash_field(&h, &carry, &field, part->type);
	}

	return PMurHash32_Result(h, carry, total_size);
}

static inline uint32_t
key_hash(const char *key, const struct key_def *key_def)
{
	const struct key_part *part = key_def->parts;

	if (key_def->part_count == 1 && part->type == NUM) {
		uint64_t val = mp_decode_uint(&key);
		if (likely(val <= UINT32_MAX))
			return val;
		return ((uint32_t)((val)>>33^(val)^(val)<<11));
	}

	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	/* Hash fields part by part (see mh_hash_field() comments) */
	for ( ; part < key_def->parts + key_def->part_count; part++)
		total_size += mh_hash_field(&h, &carry, &key, part->type);

	return PMurHash32_Result(h, carry, total_size);
}

#define LIGHT_NAME _index
#define LIGHT_DATA_TYPE struct tuple *
#define LIGHT_KEY_TYPE const char *
#define LIGHT_CMP_ARG_TYPE struct key_def *
#define LIGHT_EQUAL(a, b, c) equal(a, b, c)
#define LIGHT_EQUAL_KEY(a, b, c) equal_key(a, b, c)
#define HASH_INDEX_EXTENT_SIZE MEMTX_EXTENT_SIZE
typedef uint32_t hash_t;
#include "salad/light.h"

/* {{{ MemtxHash Iterators ****************************************/

struct hash_iterator {
	struct iterator base; /* Must be the first member. */
	struct light_index_core *hash_table;
	struct light_index_iterator hitr;
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
	struct tuple **res = light_index_itr_get_and_next(it->hash_table,
							  &it->hitr);
	return res ? *res : 0;
}

struct tuple *
hash_iterator_gt(struct iterator *ptr)
{
	assert(ptr->free == hash_iterator_free);
	ptr->next = hash_iterator_ge;
	struct hash_iterator *it = (struct hash_iterator *) ptr;
	struct tuple **res = light_index_itr_get_and_next(it->hash_table,
							  &it->hitr);
	if (!res)
		return 0;
	res = light_index_itr_get_and_next(it->hash_table,
							  &it->hitr);
	return res ? *res : 0;
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

/* {{{ MemtxHash -- implementation of all hashes. **********************/

MemtxHash::MemtxHash(struct key_def *key_def)
	: MemtxIndex(key_def)
{
	memtx_index_arena_init();
	hash_table = (struct light_index_core *) malloc(sizeof(*hash_table));
	if (hash_table == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE, sizeof(hash_table),
			  "MemtxHash", "hash_table");
	}
	light_index_create(hash_table, HASH_INDEX_EXTENT_SIZE,
			   memtx_index_extent_alloc, memtx_index_extent_free,
			   this->key_def);
}

MemtxHash::~MemtxHash()
{
	light_index_destroy(hash_table);
	free(hash_table);
}

void
MemtxHash::reserve(uint32_t size_hint)
{
	(void)size_hint;
}

size_t
MemtxHash::size() const
{
	return hash_table->count;
}

size_t
MemtxHash::bsize() const
{
        return matras_extent_count(&hash_table->mtable) * HASH_INDEX_EXTENT_SIZE;
}

struct tuple *
MemtxHash::random(uint32_t rnd) const
{
	if (hash_table->count == 0)
		return NULL;
	rnd %= (hash_table->table_size);
	while (!light_index_pos_valid(hash_table, rnd)) {
		rnd++;
		rnd %= (hash_table->table_size);
	}
	return light_index_get(hash_table, rnd);
}

struct tuple *
MemtxHash::findByKey(const char *key, uint32_t part_count) const
{
	assert(key_def->opts.is_unique && part_count == key_def->part_count);
	(void) part_count;

	struct tuple *ret = NULL;
	uint32_t h = key_hash(key, key_def);
	uint32_t k = light_index_find_key(hash_table, h, key);
	if (k != light_index_end)
		ret = light_index_get(hash_table, k);
	return ret;
}

struct tuple *
MemtxHash::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		   enum dup_replace_mode mode)
{
	uint32_t errcode;

	if (new_tuple) {
		uint32_t h = tuple_hash(new_tuple, key_def);
		struct tuple *dup_tuple = NULL;
		hash_t pos = light_index_replace(hash_table, h, new_tuple, &dup_tuple);
		if (pos == light_index_end)
			pos = light_index_insert(hash_table, h, new_tuple);

		ERROR_INJECT(ERRINJ_INDEX_ALLOC,
		{
			light_index_delete(hash_table, pos);
			pos = light_index_end;
		});

		if (pos == light_index_end) {
			tnt_raise(LoggedError, ER_MEMORY_ISSUE, (ssize_t) hash_table->count,
				  "hash_table", "key");
		}
		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			light_index_delete(hash_table, pos);
			if (dup_tuple) {
				uint32_t pos = light_index_insert(hash_table, h, dup_tuple);
				if (pos == light_index_end) {
					panic("Failed to allocate memory in "
					      "recover of int hash_table");
				}
			}
			struct space *sp = space_cache_find(key_def->space_id);
			tnt_raise(ClientError, errcode, index_name(this),
				  space_name(sp));
		}

		if (dup_tuple)
			return dup_tuple;
	}

	if (old_tuple) {
		uint32_t h = tuple_hash(old_tuple, key_def);
		int res = light_index_delete_value(hash_table, h, old_tuple);
		assert(res == 0); (void) res;
	}
	return old_tuple;
}

struct iterator *
MemtxHash::allocIterator() const
{
	struct hash_iterator *it = (struct hash_iterator *)
			calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE,
			  sizeof(struct hash_iterator),
			  "MemtxHash", "iterator");
	}

	it->base.next = hash_iterator_ge;
	it->base.free = hash_iterator_free;
	it->hash_table = hash_table;
	light_index_itr_begin(it->hash_table, &it->hitr);
	return (struct iterator *) it;
}

void
MemtxHash::initIterator(struct iterator *ptr, enum iterator_type type,
			const char *key, uint32_t part_count) const
{
	assert(part_count == 0 || key != NULL);
	(void) part_count;
	assert(ptr->free == hash_iterator_free);

	struct hash_iterator *it = (struct hash_iterator *) ptr;

	switch (type) {
	case ITER_GT:
		if (part_count != 0) {
			light_index_itr_key(it->hash_table, &it->hitr,
					    key_hash(key, key_def), key);
			it->base.next = hash_iterator_gt;
		} else {
			light_index_itr_begin(it->hash_table, &it->hitr);
			it->base.next = hash_iterator_ge;
		}
		break;
	case ITER_ALL:
		light_index_itr_begin(it->hash_table, &it->hitr);
		it->base.next = hash_iterator_ge;
		break;
	case ITER_EQ:
		assert(part_count > 0);
		light_index_itr_key(it->hash_table, &it->hitr,
				    key_hash(key, key_def), key);
		it->base.next = hash_iterator_eq;
		break;
	default:
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  "Hash index", "requested iterator type");
	}
}

/**
 * Create a read view for iterator so further index modifications
 * will not affect the iterator iteration.
 */
void
MemtxHash::createReadViewForIterator(struct iterator *iterator)
{
	struct hash_iterator *it = (struct hash_iterator *) iterator;
	light_index_itr_freeze(it->hash_table, &it->hitr);
}

/**
 * Destroy a read view of an iterator. Must be called for iterators,
 * for which createReadViewForIterator was called.
 */
void
MemtxHash::destroyReadViewForIterator(struct iterator *iterator)
{
	struct hash_iterator *it = (struct hash_iterator *) iterator;
	light_index_itr_destroy(it->hash_table, &it->hitr);
}

/* }}} */
