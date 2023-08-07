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
#include "memtx_tuple_compression.h"
#include "space.h"
#include "schema.h" /* space_by_id(), space_cache_find() */
#include "errinj.h"
#include "trivia/config.h"

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
	struct index *idx = iterator->index;					\
	bool is_first = true;							\
	do {									\
		int rc;								\
		if (is_first) {							\
			rc = name##_base(iterator, ret);			\
			iterator->next_internal = hash_iterator_ge;		\
		} else {							\
			rc = hash_iterator_ge_base(iterator, ret);		\
		}								\
		if (rc != 0 || *ret == NULL)					\
			return rc;						\
		is_first = false;						\
		*ret = memtx_tx_tuple_clarify(txn, space, *ret, idx, 0);	\
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/\
		memtx_tx_story_gc();						\
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/\
	} while (*ret == NULL);							\
	return 0;								\
}										\
struct forgot_to_add_semicolon

WRAP_ITERATOR_METHOD(hash_iterator_ge);
WRAP_ITERATOR_METHOD(hash_iterator_gt);

#undef WRAP_ITERATOR_METHOD

static int
hash_iterator_eq(struct iterator *it, struct tuple **ret)
{
	it->next_internal = exhausted_iterator_next;
	/* always returns zero. */
	hash_iterator_ge_base(it, ret);
	if (*ret == NULL)
		return 0;
	struct txn *txn = in_txn();
	struct space *sp = space_by_id(it->space_id);
	*ret = memtx_tx_tuple_clarify(txn, sp, *ret, it->index, 0);
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
	memtx_tx_story_gc();
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
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
	index->hash_table.common.arg = index->base.def->key_def;
}

static ssize_t
memtx_hash_index_size(struct index *base)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct space *space = space_by_id(base->def->space_id);
	/* Substract invisible count. */
	return light_index_count(&index->hash_table) -
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
	struct txn *txn = in_txn();
	struct space *space = space_by_id(base->def->space_id);
	if (memtx_hash_index_size(base) == 0) {
		*result = NULL;
		memtx_tx_track_full_scan(txn, space, base);
		return 0;
	}

	do {
		uint32_t k = light_index_random(hash_table, rnd++);
		/*
		 * `light_index_end` is returned only in case the space is
		 * empty.
		 */
		assert(k != light_index_end);
		*result = light_index_get(hash_table, k);
		assert(*result != NULL);
		*result = memtx_tx_tuple_clarify(txn, space, *result, base, 0);
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		memtx_tx_story_gc();
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
	} while (*result == NULL);
	return memtx_prepare_result_tuple(result);
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
memtx_hash_index_get_internal(struct index *base, const char *key,
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
		*result = memtx_tx_tuple_clarify(txn, space, tuple, base, 0);
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		memtx_tx_story_gc();
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
	} else {
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		memtx_tx_track_point(txn, space, base, key);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
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
			diag_set(OutOfMemory,
				 (ssize_t)light_index_count(hash_table),
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
					diag_set(ClientError, errcode,
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

/** Implementation of create_iterator for memtx hash index. */
static struct iterator *
memtx_hash_index_create_iterator(struct index *base, enum iterator_type type,
				 const char *key, uint32_t part_count,
				 const char *pos)
{
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;

	assert(part_count == 0 || key != NULL);
	if (pos != NULL) {
		diag_set(UnsupportedIndexFeature, base->def, "pagination");
		return NULL;
	}

	struct hash_iterator *it = (struct hash_iterator *)
		mempool_alloc(&memtx->iterator_pool);
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
	case ITER_GT: {
		static bool warn_once = false;
		if (!warn_once) {
			warn_once = true;
			say_warn("HASH index 'GT' iterator type is deprecated "
				 "since Tarantool 2.11 and should not be "
				 "used. It will be removed in a future "
				 "Tarantool release.");
		}

		if (part_count != 0) {
			light_index_iterator_key(&index->hash_table, &it->iterator,
					key_hash(key, base->def->key_def), key);
			it->base.next_internal = hash_iterator_gt;
		} else {
			light_index_iterator_begin(&index->hash_table, &it->iterator);
			it->base.next_internal = hash_iterator_ge;
		}
		/* This iterator needs to be supported as a legacy. */
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		memtx_tx_track_full_scan(in_txn(),
					 space_by_id(it->base.space_id),
					 &index->base);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
		break;
	}
	case ITER_ALL:
		light_index_iterator_begin(&index->hash_table, &it->iterator);
		it->base.next_internal = hash_iterator_ge;
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		memtx_tx_track_full_scan(in_txn(),
					 space_by_id(it->base.space_id),
					 &index->base);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
		break;
	case ITER_EQ:
		assert(part_count > 0);
		light_index_iterator_key(&index->hash_table, &it->iterator,
				key_hash(key, base->def->key_def), key);
		it->base.next_internal = hash_iterator_eq;
		if (it->iterator.slotpos == light_index_end)
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
			memtx_tx_track_point(in_txn(),
					     space_by_id(it->base.space_id),
					     &index->base, key);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
		break;
	default:
		diag_set(UnsupportedIndexFeature, base->def,
			 "requested iterator type");
		mempool_free(&memtx->iterator_pool, it);
		return NULL;
	}
	it->base.next = memtx_iterator_next;
	it->base.position = generic_iterator_position;
	return (struct iterator *)it;
}

/** Read view implementation. */
struct hash_read_view {
	/** Base class. */
	struct index_read_view base;
	/** Read view index. Ref counter incremented. */
	struct memtx_hash_index *index;
	/** Light read view. */
	struct light_index_view view;
	/** Used for clarifying read view tuples. */
	struct memtx_tx_snapshot_cleaner cleaner;
};

/** Read view iterator implementation. */
struct hash_read_view_iterator {
	/** Base class. */
	struct index_read_view_iterator_base base;
	/** Light iterator. */
	struct light_index_iterator iterator;
};

static_assert(sizeof(struct hash_read_view_iterator) <=
	      INDEX_READ_VIEW_ITERATOR_SIZE,
	      "sizeof(struct hash_read_view_iterator) must be less than or "
	      "equal to INDEX_READ_VIEW_ITERATOR_SIZE");

static void
hash_read_view_free(struct index_read_view *base)
{
	struct hash_read_view *rv = (struct hash_read_view *)base;
	light_index_view_destroy(&rv->view);
	index_unref(&rv->index->base);
	memtx_tx_snapshot_cleaner_destroy(&rv->cleaner);
	TRASH(rv);
	free(rv);
}

#if defined(ENABLE_READ_VIEW)
# include "memtx_hash_read_view.cc"
#else /* !defined(ENABLE_READ_VIEW) */

static int
hash_read_view_get_raw(struct index_read_view *rv,
		       const char *key, uint32_t part_count,
		       struct read_view_tuple *result)
{
	(void)rv;
	(void)key;
	(void)part_count;
	(void)result;
	unreachable();
	return 0;
}

/** Implementation of next_raw index_read_view_iterator callback. */
static int
hash_read_view_iterator_next_raw(struct index_read_view_iterator *iterator,
				 struct read_view_tuple *result)
{
	struct hash_read_view_iterator *it =
		(struct hash_read_view_iterator *)iterator;
	struct hash_read_view *rv = (struct hash_read_view *)it->base.index;

	while (true) {
		struct tuple **res = light_index_view_iterator_get_and_next(
			&rv->view, &it->iterator);
		if (res == NULL) {
			*result = read_view_tuple_none();
			return 0;
		}
		if (memtx_prepare_read_view_tuple(*res, &rv->base,
						  &rv->cleaner, result) != 0)
			return -1;
		if (result->data != NULL)
			return 0;
	}
	return 0;
}

/** Positions the iterator to the given key. */
static int
hash_read_view_iterator_start(struct hash_read_view_iterator *it,
			      enum iterator_type type,
			      const char *key, uint32_t part_count)
{
	assert(type == ITER_ALL);
	assert(key == NULL);
	assert(part_count == 0);
	(void)type;
	(void)key;
	(void)part_count;
	struct hash_read_view *rv = (struct hash_read_view *)it->base.index;
	it->base.next_raw = hash_read_view_iterator_next_raw;
	light_index_view_iterator_begin(&rv->view, &it->iterator);
	return 0;
}

static void
hash_read_view_reset_key_def(struct hash_read_view *rv)
{
	rv->view.common.arg = NULL;
}

#endif /* !defined(ENABLE_READ_VIEW) */

/** Implementation of create_iterator index_read_view callback. */
static int
hash_read_view_create_iterator(struct index_read_view *base,
			       enum iterator_type type,
			       const char *key, uint32_t part_count,
			       struct index_read_view_iterator *iterator)
{
	struct hash_read_view *rv = (struct hash_read_view *)base;
	struct hash_read_view_iterator *it =
		(struct hash_read_view_iterator *)iterator;
	it->base.index = base;
	it->base.next_raw = exhausted_index_read_view_iterator_next_raw;
	light_index_view_iterator_begin(&rv->view, &it->iterator);
	return hash_read_view_iterator_start(it, type, key, part_count);
}

/** Implementation of create_read_view index callback. */
static struct index_read_view *
memtx_hash_index_create_read_view(struct index *base)
{
	static const struct index_read_view_vtab vtab = {
		.free = hash_read_view_free,
		.get_raw = hash_read_view_get_raw,
		.create_iterator = hash_read_view_create_iterator,
	};
	struct memtx_hash_index *index = (struct memtx_hash_index *)base;
	struct hash_read_view *rv =
		(struct hash_read_view *)xmalloc(sizeof(*rv));
	if (index_read_view_create(&rv->base, &vtab, base->def) != 0) {
		free(rv);
		return NULL;
	}
	struct space *space = space_by_id(base->def->space_id);
	assert(space != NULL);
	memtx_tx_snapshot_cleaner_create(&rv->cleaner, space);
	rv->index = index;
	index_ref(base);
	light_index_view_create(&rv->view, &index->hash_table);
	hash_read_view_reset_key_def(rv);
	return (struct index_read_view *)rv;
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
	/* .get_internal = */ memtx_hash_index_get_internal,
	/* .get = */ memtx_index_get,
	/* .replace = */ memtx_hash_index_replace,
	/* .create_iterator = */ memtx_hash_index_create_iterator,
	/* .create_read_view = */ memtx_hash_index_create_read_view,
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
