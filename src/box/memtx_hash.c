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
#include "fiber.h"
#include "tuple.h"
#include "tuple_hash.h"
#include "memtx_engine.h"
#include "space.h"
#include "schema.h" /* space_cache_find() */
#include "errinj.h"

#include "third_party/PMurHash.h"
#include <small/mempool.h>

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
	/** Memory pool the iterator was allocated from. */
	struct mempool *pool;
};

static void
hash_iterator_free(struct iterator *iterator)
{
	assert(iterator->free == hash_iterator_free);
	struct hash_iterator *it = (struct hash_iterator *) iterator;
	mempool_free(it->pool, it);
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

static void
memtx_hash_index_destroy(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	light_index_destroy(index->hash_table);
	free(index->hash_table);
	free(index);
}

static void
memtx_hash_index_update_def(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	index->hash_table->arg = index->base.def->key_def;
}

static ssize_t
memtx_hash_index_size(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	return index->hash_table->count;
}

static ssize_t
memtx_hash_index_bsize(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	return matras_extent_count(&index->hash_table->mtable) *
					HASH_INDEX_EXTENT_SIZE;
}

static int
memtx_hash_index_random(struct index *base, uint32_t rnd, struct tuple **result)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct light_index_core *hash_table = index->hash_table;

	*result = NULL;
	if (hash_table->count == 0)
		return 0;
	rnd %= (hash_table->table_size);
	while (!light_index_pos_valid(hash_table, rnd)) {
		rnd++;
		rnd %= (hash_table->table_size);
	}
	*result = light_index_get(hash_table, rnd);
	return 0;
}

static ssize_t
memtx_hash_index_count(struct index *base, enum iterator_type type,
		       const char *key, uint32_t part_count)
{
	if (type == ITER_ALL)
		return memtx_hash_index_size(base); /* optimization */
	return generic_index_count(base, type, key, part_count);
}

static int
memtx_hash_index_get(struct index *base, const char *key,
		     uint32_t part_count, struct tuple **result)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;

	assert(base->def->opts.is_unique &&
	       part_count == base->def->key_def->part_count);
	(void) part_count;

	*result = NULL;
	uint32_t h = key_hash(key, base->def->key_def);
	uint32_t k = light_index_find_key(index->hash_table, h, key);
	if (k != light_index_end)
		*result = light_index_get(index->hash_table, k);
	return 0;
}

static int
memtx_hash_index_replace(struct index *base, struct tuple *old_tuple,
			 struct tuple *new_tuple, enum dup_replace_mode mode,
			 struct tuple **result)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct light_index_core *hash_table = index->hash_table;

	if (new_tuple) {
		uint32_t h = tuple_hash(new_tuple, base->def->key_def);
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
			diag_set(OutOfMemory, (ssize_t)hash_table->count,
				 "hash_table", "key");
			return -1;
		}
		uint32_t errcode = replace_check_dup(old_tuple,
						     dup_tuple, mode);
		if (errcode) {
			light_index_delete(hash_table, pos);
			if (dup_tuple) {
				uint32_t pos = light_index_insert(hash_table, h, dup_tuple);
				if (pos == light_index_end) {
					panic("Failed to allocate memory in "
					      "recover of int hash_table");
				}
			}
			struct space *sp = space_cache_find(base->def->space_id);
			if (sp != NULL)
				diag_set(ClientError, errcode, base->def->name,
					 space_name(sp));
			return -1;
		}

		if (dup_tuple) {
			*result = dup_tuple;
			return 0;
		}
	}

	if (old_tuple) {
		uint32_t h = tuple_hash(old_tuple, base->def->key_def);
		int res = light_index_delete_value(hash_table, h, old_tuple);
		assert(res == 0); (void) res;
	}
	*result = old_tuple;
	return 0;
}

static struct iterator *
memtx_hash_index_create_iterator(struct index *base, enum iterator_type type,
				 const char *key, uint32_t part_count)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;

	assert(part_count == 0 || key != NULL);

	struct hash_iterator *it = mempool_alloc(&memtx->hash_iterator_pool);
	if (it == NULL) {
		diag_set(OutOfMemory, sizeof(struct hash_iterator),
			 "memtx_hash_index", "iterator");
		return NULL;
	}
	iterator_create(&it->base, base);
	it->pool = &memtx->hash_iterator_pool;
	it->base.free = hash_iterator_free;
	it->hash_table = index->hash_table;
	light_index_iterator_begin(it->hash_table, &it->iterator);

	switch (type) {
	case ITER_GT:
		if (part_count != 0) {
			light_index_iterator_key(it->hash_table, &it->iterator,
					key_hash(key, base->def->key_def), key);
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
				key_hash(key, base->def->key_def), key);
		it->base.next = hash_iterator_eq;
		break;
	default:
		diag_set(UnsupportedIndexFeature, base->def,
			 "requested iterator type");
		mempool_free(&memtx->hash_iterator_pool, it);
		return NULL;
	}
	return (struct iterator *)it;
}

struct hash_snapshot_iterator {
	struct snapshot_iterator base;
	struct light_index_core *hash_table;
	struct light_index_iterator iterator;
};

/**
 * Destroy read view and free snapshot iterator.
 * Virtual method of snapshot iterator.
 * @sa index_vtab::create_snapshot_iterator.
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
 * Virtual method of snapshot iterator.
 * @sa index_vtab::create_snapshot_iterator.
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
static struct snapshot_iterator *
memtx_hash_index_create_snapshot_iterator(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct hash_snapshot_iterator *it = (struct hash_snapshot_iterator *)
		calloc(1, sizeof(*it));
	if (it == NULL) {
		diag_set(OutOfMemory, sizeof(struct hash_snapshot_iterator),
			 "memtx_hash_index", "iterator");
		return NULL;
	}

	it->base.next = hash_snapshot_iterator_next;
	it->base.free = hash_snapshot_iterator_free;
	it->hash_table = index->hash_table;
	light_index_iterator_begin(it->hash_table, &it->iterator);
	light_index_iterator_freeze(it->hash_table, &it->iterator);
	return (struct snapshot_iterator *) it;
}

static const struct index_vtab memtx_hash_index_vtab = {
	/* .destroy = */ memtx_hash_index_destroy,
	/* .commit_create = */ generic_index_commit_create,
	/* .abort_create = */ memtx_index_abort_create,
	/* .commit_modify = */ generic_index_commit_modify,
	/* .commit_drop = */ memtx_index_commit_drop,
	/* .update_def = */ memtx_hash_index_update_def,
	/* .depends_on_pk = */ generic_index_depends_on_pk,
	/* .def_change_requires_rebuild = */
		memtx_index_def_change_requires_rebuild,
	/* .size = */ memtx_hash_index_size,
	/* .bsize = */ memtx_hash_index_bsize,
	/* .min = */ generic_index_min,
	/* .max = */ generic_index_max,
	/* .random = */ memtx_hash_index_random,
	/* .count = */ memtx_hash_index_count,
	/* .get = */ memtx_hash_index_get,
	/* .replace = */ memtx_hash_index_replace,
	/* .create_iterator = */ memtx_hash_index_create_iterator,
	/* .create_snapshot_iterator = */
		memtx_hash_index_create_snapshot_iterator,
	/* .info = */ generic_index_info,
	/* .reset_stat = */ generic_index_reset_stat,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

struct memtx_hash_index *
memtx_hash_index_new(struct memtx_engine *memtx, struct index_def *def)
{
	memtx_index_arena_init();

	if (!mempool_is_initialized(&memtx->hash_iterator_pool)) {
		mempool_create(&memtx->hash_iterator_pool, cord_slab_cache(),
			       sizeof(struct hash_iterator));
	}

	struct memtx_hash_index *index =
		(struct memtx_hash_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct memtx_hash_index");
		return NULL;
	}
	struct light_index_core *hash_table =
		(struct light_index_core *)malloc(sizeof(*hash_table));
	if (hash_table == NULL) {
		free(index);
		diag_set(OutOfMemory, sizeof(*hash_table),
			 "malloc", "struct light_index_core");
		return NULL;
	}
	if (index_create(&index->base, (struct engine *)memtx,
			 &memtx_hash_index_vtab, def) != 0) {
		free(hash_table);
		free(index);
		return NULL;
	}

	light_index_create(hash_table, HASH_INDEX_EXTENT_SIZE,
			   memtx_index_extent_alloc, memtx_index_extent_free,
			   NULL, index->base.def->key_def);
	index->hash_table = hash_table;
	return index;
}

/* }}} */
