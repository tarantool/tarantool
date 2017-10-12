/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "tuple_compare.h"
#include "tuple_hash.h"
#include "memtx_engine.h"
#include "memtx_index.h"
#include "space.h"
#include "schema.h" /* space_cache_find() */
#include "errinj.h"

#include "third_party/PMurHash.h"

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
	struct light_index_iterator iterator;
};

static void
hash_iterator_free(struct iterator *iterator)
{
	assert(iterator->free == hash_iterator_free);
	free(iterator);
}

static int
hash_iterator_ge(struct iterator *ptr, struct tuple **ret)
{
	assert(ptr->free == hash_iterator_free);
	struct hash_iterator *it = (struct hash_iterator *) ptr;
	struct tuple **res = light_index_iterator_get_and_next(it->hash_table,
							       &it->iterator);
	*ret = res != NULL ? *res : NULL;
	return 0;
}

static int
hash_iterator_gt(struct iterator *ptr, struct tuple **ret)
{
	assert(ptr->free == hash_iterator_free);
	ptr->next = hash_iterator_ge;
	struct hash_iterator *it = (struct hash_iterator *) ptr;
	struct tuple **res = light_index_iterator_get_and_next(it->hash_table,
							       &it->iterator);
	if (res != NULL)
		res = light_index_iterator_get_and_next(it->hash_table,
							&it->iterator);
	*ret = res != NULL ? *res : NULL;
	return 0;
}

static int
hash_iterator_eq_next(MAYBE_UNUSED struct iterator *it, struct tuple **ret)
{
	*ret = NULL;
	return 0;
}

static int
hash_iterator_eq(struct iterator *it, struct tuple **ret)
{
	it->next = hash_iterator_eq_next;
	return hash_iterator_ge(it, ret);
}

/* }}} */

/* {{{ MemtxHash -- implementation of all hashes. **********************/

MemtxHash::MemtxHash(struct index_def *index_def_arg)
	: index(index_def_arg)
{
	memtx_index_arena_init();
	hash_table = (struct light_index_core *) malloc(sizeof(*hash_table));
	if (hash_table == NULL) {
		tnt_raise(OutOfMemory, sizeof(*hash_table),
			  "MemtxHash", "hash_table");
	}
	light_index_create(hash_table, HASH_INDEX_EXTENT_SIZE,
			   memtx_index_extent_alloc, memtx_index_extent_free,
			   NULL, this->def->key_def);
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
MemtxHash::size()
{
	return hash_table->count;
}

size_t
MemtxHash::bsize()
{
        return matras_extent_count(&hash_table->mtable) * HASH_INDEX_EXTENT_SIZE;
}

struct tuple *
MemtxHash::min(const char *key, uint32_t part_count)
{
	return memtx_index_min(this, key, part_count);
}

struct tuple *
MemtxHash::max(const char *key, uint32_t part_count)
{
	return memtx_index_max(this, key, part_count);
}

size_t
MemtxHash::count(enum iterator_type type, const char *key,
		 uint32_t part_count)
{
	return memtx_index_count(this, type, key, part_count);
}

struct tuple *
MemtxHash::random(uint32_t rnd)
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
MemtxHash::findByKey(const char *key, uint32_t part_count)
{
	assert(def->opts.is_unique && part_count == def->key_def->part_count);
	(void) part_count;

	struct tuple *ret = NULL;
	uint32_t h = key_hash(key, def->key_def);
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
		uint32_t h = tuple_hash(new_tuple, def->key_def);
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
			tnt_raise(OutOfMemory, (ssize_t)hash_table->count,
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
			struct space *sp = space_cache_find(def->space_id);
			tnt_raise(ClientError, errcode, index_name(this),
				  space_name(sp));
		}

		if (dup_tuple)
			return dup_tuple;
	}

	if (old_tuple) {
		uint32_t h = tuple_hash(old_tuple, def->key_def);
		int res = light_index_delete_value(hash_table, h, old_tuple);
		assert(res == 0); (void) res;
	}
	return old_tuple;
}

struct iterator *
MemtxHash::allocIterator()
{
	struct hash_iterator *it = (struct hash_iterator *)
			calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct hash_iterator),
			  "MemtxHash", "iterator");
	}

	it->base.next = hash_iterator_ge;
	it->base.free = hash_iterator_free;
	it->hash_table = hash_table;
	light_index_iterator_begin(it->hash_table, &it->iterator);
	return (struct iterator *) it;
}

void
MemtxHash::initIterator(struct iterator *ptr, enum iterator_type type,
			const char *key, uint32_t part_count)
{
	assert(part_count == 0 || key != NULL);
	(void) part_count;
	assert(ptr->free == hash_iterator_free);

	struct hash_iterator *it = (struct hash_iterator *) ptr;

	switch (type) {
	case ITER_GT:
		if (part_count != 0) {
			light_index_iterator_key(it->hash_table, &it->iterator,
					key_hash(key, def->key_def), key);
			it->base.next = hash_iterator_gt;
		} else {
			light_index_iterator_begin(it->hash_table, &it->iterator);
			it->base.next = hash_iterator_ge;
		}
		break;
	case ITER_ALL:
		light_index_iterator_begin(it->hash_table, &it->iterator);
		it->base.next = hash_iterator_ge;
		break;
	case ITER_EQ:
		assert(part_count > 0);
		light_index_iterator_key(it->hash_table, &it->iterator,
				key_hash(key, def->key_def), key);
		it->base.next = hash_iterator_eq;
		break;
	default:
		return index::initIterator(ptr, type, key, part_count);
	}
}

struct hash_snapshot_iterator {
	struct snapshot_iterator base;
	struct light_index_core *hash_table;
	struct light_index_iterator iterator;
};

/**
 * Destroy read view and free snapshot iterator.
 * Virtual method of snapshot iterator @sa MemtxHash::createSnapshotIterator.
 */
static void
hash_snapshot_iterator_free(struct snapshot_iterator *iterator)
{
	assert(iterator->free == hash_snapshot_iterator_free);
	struct hash_snapshot_iterator *it =
		(struct hash_snapshot_iterator *) iterator;
	light_index_iterator_destroy(it->hash_table, &it->iterator);
	free(iterator);
}

/**
 * Get next tuple from snapshot iterator.
 * Virtual method of snapshot iterator @sa MemtxHash::createSnapshotIterator.
 */
static const char *
hash_snapshot_iterator_next(struct snapshot_iterator *iterator, uint32_t *size)
{
	assert(iterator->free == hash_snapshot_iterator_free);
	struct hash_snapshot_iterator *it =
		(struct hash_snapshot_iterator *) iterator;
	struct tuple **res = light_index_iterator_get_and_next(it->hash_table,
							       &it->iterator);
	if (res == NULL)
		return NULL;
	return tuple_data_range(*res, size);
}

/**
 * Create an ALL iterator with personal read view so further
 * index modifications will not affect the iteration results.
 * Must be destroyed by iterator->free after usage.
 */
struct snapshot_iterator *
MemtxHash::createSnapshotIterator()
{
	struct hash_snapshot_iterator *it = (struct hash_snapshot_iterator *)
		calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct hash_snapshot_iterator),
			  "MemtxHash", "iterator");
	}

	it->base.next = hash_snapshot_iterator_next;
	it->base.free = hash_snapshot_iterator_free;
	it->hash_table = hash_table;
	light_index_iterator_begin(it->hash_table, &it->iterator);
	light_index_iterator_freeze(it->hash_table, &it->iterator);
	return (struct snapshot_iterator *) it;
}

/* }}} */
