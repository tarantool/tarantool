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
#include "index.h"
#include "tuple.h"
#include "txn.h"
#include "memtx_tx.h"
#include "memtx_engine.h"
#include "space.h"
#include "schema.h" /* space_by_id(), space_cache_find() */
#include "errinj.h"

#include <small/mempool.h>

static inline bool
memtx_hash_equal(struct tuple *tuple_a, struct tuple *tuple_b,
		 struct key_def *key_def)
{
	return tuple_compare(tuple_a, HINT_NONE,
			     tuple_b, HINT_NONE, key_def) == 0;
}

static inline bool
memtx_hash_equal_key(struct tuple *tuple, const char *key,
		     struct key_def *key_def)
{
	return tuple_compare_with_key(tuple, HINT_NONE, key, key_def->part_count,
				      HINT_NONE, key_def) == 0;
}

#define LIGHT_NAME _index
#define LIGHT_DATA_TYPE struct tuple *
#define LIGHT_KEY_TYPE const char *
#define LIGHT_CMP_ARG_TYPE struct key_def *
#define LIGHT_EQUAL(a, b, c) memtx_hash_equal(a, b, c)
#define LIGHT_EQUAL_KEY(a, b, c) memtx_hash_equal_key(a, b, c)

#include "salad/light.h"

#undef LIGHT_NAME
#undef LIGHT_DATA_TYPE
#undef LIGHT_KEY_TYPE
#undef LIGHT_CMP_ARG_TYPE
#undef LIGHT_EQUAL
#undef LIGHT_EQUAL_KEY

struct memtx_hash_index {
	struct index base;
	struct light_index_core hash_table;
	struct memtx_gc_task gc_task;
	struct light_index_iterator gc_iterator;
};

/* {{{ MemtxHash Iterators ****************************************/

struct hash_iterator {
	struct iterator base; /* Must be the first member. */
	struct light_index_iterator iterator;
	/** Memory pool the iterator was allocated from. */
	struct mempool *pool;
};

static_assert(sizeof(struct hash_iterator) <= MEMTX_ITERATOR_SIZE,
	      "sizeof(struct hash_iterator) must be less than or equal "
	      "to MEMTX_ITERATOR_SIZE");

static void
hash_iterator_free(struct iterator *iterator)
{
	assert(iterator->free == hash_iterator_free);
	struct hash_iterator *it = (struct hash_iterator *) iterator;
	mempool_free(it->pool, it);
}

static int
hash_iterator_ge_base(struct iterator *ptr, struct tuple **ret)
{
	assert(ptr->free == hash_iterator_free);
	struct hash_iterator *it = (struct hash_iterator *) ptr;
	struct memtx_hash_index *index = (struct memtx_hash_index *)ptr->index;
	struct tuple **res = light_index_iterator_get_and_next(&index->hash_table,
							       &it->iterator);
	*ret = res != NULL ? *res : NULL;
	return 0;
}

static int
hash_iterator_gt_base(struct iterator *ptr, struct tuple **ret)
{
	assert(ptr->free == hash_iterator_free);
	ptr->next = hash_iterator_ge_base;
	struct hash_iterator *it = (struct hash_iterator *) ptr;
	struct memtx_hash_index *index = (struct memtx_hash_index *)ptr->index;
	struct tuple **res = light_index_iterator_get_and_next(&index->hash_table,
							       &it->iterator);
	if (res != NULL)
		res = light_index_iterator_get_and_next(&index->hash_table,
							&it->iterator);
	*ret = res != NULL ? *res : NULL;
	return 0;
}

#define WRAP_ITERATOR_METHOD(name)						\
static int									\
name(struct iterator *iterator, struct tuple **ret)				\
{										\
	struct txn *txn = in_txn();						\
	struct space *space = space_by_id(iterator->space_id);			\
	bool is_rw = txn != NULL;						\
	struct index *idx = iterator->index;					\
	bool is_first = true;							\
	do {									\
		int rc = is_first ? name##_base(iterator, ret)			\
				  : hash_iterator_ge_base(iterator, ret);	\
		if (rc != 0 || *ret == NULL)					\
			return rc;						\
		is_first = false;						\
		*ret = memtx_tx_tuple_clarify(txn, space, *ret, idx, 0, is_rw);	\
	} while (*ret == NULL);							\
	return 0;								\
}										\
struct forgot_to_add_semicolon

WRAP_ITERATOR_METHOD(hash_iterator_ge);
WRAP_ITERATOR_METHOD(hash_iterator_gt);

#undef WRAP_ITERATOR_METHOD

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
	hash_iterator_ge_base(it, ret); /* always returns zero. */
	if (*ret == NULL)
		return 0;
	struct txn *txn = in_txn();
	struct space *sp = space_by_id(it->space_id);
	bool is_rw = txn != NULL;
	*ret = memtx_tx_tuple_clarify(txn, sp, *ret, it->index, 0, is_rw);
	return 0;
}

/* }}} */

/* {{{ MemtxHash -- implementation of all hashes. **********************/

static void
memtx_hash_index_free(struct memtx_hash_index *index)
{
	light_index_destroy(&index->hash_table);
	free(index);
}

static void
memtx_hash_index_gc_run(struct memtx_gc_task *task, bool *done)
{
	/*
	 * Yield every 1K tuples to keep latency < 0.1 ms.
	 * Yield more often in debug mode.
	 */
#ifdef NDEBUG
	enum { YIELD_LOOPS = 1000 };
#else
	enum { YIELD_LOOPS = 10 };
#endif

	struct memtx_hash_index *index = container_of(task,
			struct memtx_hash_index, gc_task);
	struct light_index_core *hash = &index->hash_table;
	struct light_index_iterator *itr = &index->gc_iterator;

	struct tuple **res;
	unsigned int loops = 0;
	while ((res = light_index_iterator_get_and_next(hash, itr)) != NULL) {
		tuple_unref(*res);
		if (++loops >= YIELD_LOOPS) {
			*done = false;
			return;
		}
	}
	*done = true;
}

static void
memtx_hash_index_gc_free(struct memtx_gc_task *task)
{
	struct memtx_hash_index *index = container_of(task,
			struct memtx_hash_index, gc_task);
	memtx_hash_index_free(index);
}

static const struct memtx_gc_task_vtab memtx_hash_index_gc_vtab = {
	.run = memtx_hash_index_gc_run,
	.free = memtx_hash_index_gc_free,
};

static void
memtx_hash_index_destroy(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;
	if (base->def->iid == 0) {
		/*
		 * Primary index. We need to free all tuples stored
		 * in the index, which may take a while. Schedule a
		 * background task in order not to block tx thread.
		 */
		index->gc_task.vtab = &memtx_hash_index_gc_vtab;
		light_index_iterator_begin(&index->hash_table,
					   &index->gc_iterator);
		memtx_engine_schedule_gc(memtx, &index->gc_task);
	} else {
		/*
		 * Secondary index. Destruction is fast, no need to
		 * hand over to background fiber.
		 */
		memtx_hash_index_free(index);
	}
}

static void
memtx_hash_index_update_def(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	index->hash_table.arg = index->base.def->key_def;
}

static ssize_t
memtx_hash_index_size(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct space *space = space_by_id(base->def->space_id);
	/* Substract invisible count. */
	return index->hash_table.count -
	       memtx_tx_index_invisible_count(in_txn(), space, base);
}

static ssize_t
memtx_hash_index_bsize(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	return matras_extent_count(&index->hash_table.mtable) *
					MEMTX_EXTENT_SIZE;
}

static int
memtx_hash_index_random(struct index *base, uint32_t rnd, struct tuple **result)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct light_index_core *hash_table = &index->hash_table;

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

	struct space *space = space_by_id(base->def->space_id);
	struct txn *txn = in_txn();
	*result = NULL;
	uint32_t h = key_hash(key, base->def->key_def);
	uint32_t k = light_index_find_key(&index->hash_table, h, key);
	if (k != light_index_end) {
		struct tuple *tuple = light_index_get(&index->hash_table, k);
		bool is_rw = txn != NULL;
		*result = memtx_tx_tuple_clarify(txn, space, tuple, base,
						 0, is_rw);
	} else {
		memtx_tx_track_point(txn, space, base, key);
	}
	return 0;
}

static int
memtx_hash_index_replace(struct index *base, struct tuple *old_tuple,
			 struct tuple *new_tuple, enum dup_replace_mode mode,
			 struct tuple **result, struct tuple **successor)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct light_index_core *hash_table = &index->hash_table;

	/* HASH index doesn't support ordering. */
	*successor = NULL;

	if (new_tuple) {
		uint32_t h = tuple_hash(new_tuple, base->def->key_def);
		struct tuple *dup_tuple = NULL;
		uint32_t pos = light_index_replace(hash_table, h, new_tuple,
						   &dup_tuple);
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
			if (sp != NULL) {
				if (errcode == ER_TUPLE_FOUND){
					diag_set(ClientError, errcode,  base->def->name,
						 space_name(sp), tuple_str(dup_tuple),
						 tuple_str(new_tuple));
				} else {
					diag_set(ClientError, errcode, base->def->name,
						 space_name(sp));
				}
			}
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

	struct hash_iterator *it = mempool_alloc(&memtx->iterator_pool);
	if (it == NULL) {
		diag_set(OutOfMemory, sizeof(struct hash_iterator),
			 "memtx_hash_index", "iterator");
		return NULL;
	}
	iterator_create(&it->base, base);
	it->pool = &memtx->iterator_pool;
	it->base.free = hash_iterator_free;
	light_index_iterator_begin(&index->hash_table, &it->iterator);

	switch (type) {
	case ITER_GT:
		if (part_count != 0) {
			light_index_iterator_key(&index->hash_table, &it->iterator,
					key_hash(key, base->def->key_def), key);
			it->base.next = hash_iterator_gt;
		} else {
			light_index_iterator_begin(&index->hash_table, &it->iterator);
			it->base.next = hash_iterator_ge;
		}
		break;
	case ITER_ALL:
		light_index_iterator_begin(&index->hash_table, &it->iterator);
		it->base.next = hash_iterator_ge;
		break;
	case ITER_EQ:
		assert(part_count > 0);
		light_index_iterator_key(&index->hash_table, &it->iterator,
				key_hash(key, base->def->key_def), key);
		it->base.next = hash_iterator_eq;
		if (it->iterator.slotpos == light_index_end)
			memtx_tx_track_point(in_txn(),
					     space_by_id(it->base.space_id),
					     &index->base, key);
		break;
	default:
		diag_set(UnsupportedIndexFeature, base->def,
			 "requested iterator type");
		mempool_free(&memtx->iterator_pool, it);
		return NULL;
	}
	return (struct iterator *)it;
}

struct hash_snapshot_iterator {
	struct snapshot_iterator base;
	struct memtx_hash_index *index;
	struct light_index_iterator iterator;
	struct memtx_tx_snapshot_cleaner cleaner;
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
	memtx_leave_delayed_free_mode((struct memtx_engine *)
				      it->index->base.engine);
	light_index_iterator_destroy(&it->index->hash_table, &it->iterator);
	index_unref(&it->index->base);
	memtx_tx_snapshot_cleaner_destroy(&it->cleaner);
	free(iterator);
}

/**
 * Get next tuple from snapshot iterator.
 * Virtual method of snapshot iterator.
 * @sa index_vtab::create_snapshot_iterator.
 */
static int
hash_snapshot_iterator_next(struct snapshot_iterator *iterator,
			    const char **data, uint32_t *size)
{
	assert(iterator->free == hash_snapshot_iterator_free);
	struct hash_snapshot_iterator *it =
		(struct hash_snapshot_iterator *) iterator;
	struct light_index_core *hash_table = &it->index->hash_table;

	while (true) {
		struct tuple **res =
			light_index_iterator_get_and_next(hash_table,
			                                  &it->iterator);
		if (res == NULL) {
			*data = NULL;
			return 0;
		}

		struct tuple *tuple = *res;
		tuple = memtx_tx_snapshot_clarify(&it->cleaner, tuple);

		if (tuple != NULL) {
			*data = tuple_data_range(*res, size);
			return 0;
		}
	}
	return 0;
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
	it->index = index;
	index_ref(base);
	light_index_iterator_begin(&index->hash_table, &it->iterator);
	light_index_iterator_freeze(&index->hash_table, &it->iterator);
	memtx_enter_delayed_free_mode((struct memtx_engine *)base->engine);
	return (struct snapshot_iterator *) it;
}

static const struct index_vtab memtx_hash_index_vtab = {
	/* .destroy = */ memtx_hash_index_destroy,
	/* .commit_create = */ generic_index_commit_create,
	/* .abort_create = */ generic_index_abort_create,
	/* .commit_modify = */ generic_index_commit_modify,
	/* .commit_drop = */ generic_index_commit_drop,
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
	/* .stat = */ generic_index_stat,
	/* .compact = */ generic_index_compact,
	/* .reset_stat = */ generic_index_reset_stat,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

struct index *
memtx_hash_index_new(struct memtx_engine *memtx, struct index_def *def)
{
	struct memtx_hash_index *index =
		(struct memtx_hash_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct memtx_hash_index");
		return NULL;
	}
	if (index_create(&index->base, (struct engine *)memtx,
			 &memtx_hash_index_vtab, def) != 0) {
		free(index);
		return NULL;
	}

	light_index_create(&index->hash_table, MEMTX_EXTENT_SIZE,
			   memtx_index_extent_alloc, memtx_index_extent_free,
			   memtx, index->base.def->key_def);
	return &index->base;
}

/* }}} */
