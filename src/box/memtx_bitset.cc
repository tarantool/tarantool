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

#include "memtx_bitset.h"

#include <string.h>
#include <small/mempool.h>

#include "trivia/util.h"

#include "bitset/index.h"
#include "fiber.h"
#include "index.h"
#include "schema.h"
#include "tuple.h"
#include "txn.h"
#include "memtx_tx.h"
#include "memtx_engine.h"

struct memtx_bitset_index {
	struct index base;
	struct tt_bitset_index index;
#ifndef OLD_GOOD_BITSET
	struct matras *id_to_tuple;
	struct mh_bitset_index_t *tuple_to_id;
	uint32_t spare_id;
#endif /*#ifndef OLD_GOOD_BITSET*/
};

#ifndef OLD_GOOD_BITSET
#include "small/matras.h"
struct mh_bitset_index_t;

struct bitset_hash_entry {
	struct tuple *tuple;
	uint32_t id;
};
#define mh_int_t uint32_t
#define mh_arg_t int

#if UINTPTR_MAX == 0xffffffff
#define mh_hash_key(a, arg) ((uintptr_t)(a))
#else
#define mh_hash_key(a, arg) ((uint32_t)(((uintptr_t)(a)) >> 33 ^ ((uintptr_t)(a)) ^ ((uintptr_t)(a)) << 11))
#endif
#define mh_hash(a, arg) mh_hash_key((a)->tuple, arg)
#define mh_cmp(a, b, arg) ((a)->tuple != (b)->tuple)
#define mh_cmp_key(a, b, arg) ((a) != (b)->tuple)

#define mh_node_t struct bitset_hash_entry
#define mh_key_t struct tuple *
#define mh_name _bitset_index
#define MH_SOURCE 1
#include <salad/mhash.h>

enum {
	SPARE_ID_END = 0xFFFFFFFF
};

static void
memtx_bitset_index_register_tuple(struct memtx_bitset_index *index,
				  struct tuple *tuple)
{
	uint32_t id;
	struct tuple **place;
	if (index->spare_id != SPARE_ID_END) {
		id = index->spare_id;
		void *mem = matras_get(index->id_to_tuple, id);
		index->spare_id = *(uint32_t *)mem;
		place = (struct tuple **)mem;
	} else {
		place = (struct tuple **)matras_alloc(index->id_to_tuple, &id);
	}
	*place = tuple;

	struct bitset_hash_entry entry;
	entry.id = id;
	entry.tuple = tuple;
	mh_bitset_index_put(index->tuple_to_id, &entry, 0, 0);
}

static void
memtx_bitset_index_unregister_tuple(struct memtx_bitset_index *index,
				    struct tuple *tuple)
{

	uint32_t k = mh_bitset_index_find(index->tuple_to_id, tuple, 0);
	struct bitset_hash_entry *e = mh_bitset_index_node(index->tuple_to_id, k);
	void *mem = matras_get(index->id_to_tuple, e->id);
	*(uint32_t *)mem = index->spare_id;
	index->spare_id = e->id;
	mh_bitset_index_del(index->tuple_to_id, k, 0);
}

static uint32_t
memtx_bitset_index_tuple_to_value(struct memtx_bitset_index *index,
				  struct tuple *tuple)
{
	uint32_t k = mh_bitset_index_find(index->tuple_to_id, tuple, 0);
	struct bitset_hash_entry *e = mh_bitset_index_node(index->tuple_to_id, k);
	return e->id;
}

static struct tuple *
memtx_bitset_index_value_to_tuple(struct memtx_bitset_index *index,
				  uint32_t value)
{
	void *mem = matras_get(index->id_to_tuple, value);
	return *(struct tuple **)mem;
}
#else /* #ifndef OLD_GOOD_BITSET */
static inline struct tuple *
value_to_tuple(size_t value);

static inline size_t
tuple_to_value(struct tuple *tuple)
{
	/*
	 * @todo small_ptr_compress() is broken
	 * https://github.com/tarantool/tarantool/issues/49
	 */
	/* size_t value = small_ptr_compress(tuple); */
	size_t value = (intptr_t) tuple >> 2;
	assert(value_to_tuple(value) == tuple);
	return value;
}

static inline struct tuple *
value_to_tuple(size_t value)
{
	/* return (struct tuple *) salloc_ptr_from_index(value); */
	return (struct tuple *) (value << 2);
}
#endif /* #ifndef OLD_GOOD_BITSET */

struct bitset_index_iterator {
	struct iterator base; /* Must be the first member. */
	struct tt_bitset_iterator bitset_it;
	/** Memory pool the iterator was allocated from. */
	struct mempool *pool;
};

static_assert(sizeof(struct bitset_index_iterator) <= MEMTX_ITERATOR_SIZE,
	      "sizeof(struct bitset_index_iterator) must be less than or equal "
	      "to MEMTX_ITERATOR_SIZE");

static struct bitset_index_iterator *
bitset_index_iterator(struct iterator *it)
{
	return (struct bitset_index_iterator *) it;
}

static void
bitset_index_iterator_free(struct iterator *iterator)
{
	assert(iterator->free == bitset_index_iterator_free);
	struct bitset_index_iterator *it = bitset_index_iterator(iterator);

	tt_bitset_iterator_destroy(&it->bitset_it);
	mempool_free(it->pool, it);
}

static int
bitset_index_iterator_next_raw(struct iterator *iterator, struct tuple **ret)
{
	assert(iterator->free == bitset_index_iterator_free);
	struct bitset_index_iterator *it = bitset_index_iterator(iterator);

	do {
		size_t value = tt_bitset_iterator_next(&it->bitset_it);
		if (value == SIZE_MAX) {
			*ret = NULL;
			return 0;
		}
#ifndef OLD_GOOD_BITSET
		struct memtx_bitset_index *index =
			(struct memtx_bitset_index *)iterator->index;
		struct tuple *tuple =
			memtx_bitset_index_value_to_tuple(index, value);
#else /* #ifndef OLD_GOOD_BITSET */
		struct tuple *tuple = value_to_tuple(value);
#endif /* #ifndef OLD_GOOD_BITSET */
		struct index *idx = iterator->index;
		struct txn *txn = in_txn();
		struct space *space = space_by_id(iterator->space_id);
		bool is_rw = txn != NULL;
		*ret = memtx_tx_tuple_clarify(txn, space, tuple, idx, 0, is_rw);
	} while (*ret == NULL);

	return 0;
}

static void
memtx_bitset_index_destroy(struct index *base)
{
	struct memtx_bitset_index *index = (struct memtx_bitset_index *)base;
	tt_bitset_index_destroy(&index->index);
#ifndef OLD_GOOD_BITSET
	mh_bitset_index_delete(index->tuple_to_id);
	matras_destroy(index->id_to_tuple);
	free(index->id_to_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
	free(index);
}

static ssize_t
memtx_bitset_index_size(struct index *base)
{
	struct memtx_bitset_index *index = (struct memtx_bitset_index *)base;
	struct space *space = space_by_id(base->def->space_id);
	/* Substract invisible count. */
	return tt_bitset_index_size(&index->index) -
	       memtx_tx_index_invisible_count(in_txn(), space, base);
}

static ssize_t
memtx_bitset_index_bsize(struct index *base)
{
	struct memtx_bitset_index *index = (struct memtx_bitset_index *)base;
	size_t result = 0;
	result += tt_bitset_index_bsize(&index->index);
#ifndef OLD_GOOD_BITSET
	result += matras_extent_count(index->id_to_tuple) * MEMTX_EXTENT_SIZE;
	result += mh_bitset_index_memsize(index->tuple_to_id);
#endif /* #ifndef OLD_GOOD_BITSET */
	return result;
}

static inline const char *
make_key(const char *field, uint32_t *key_len)
{
	static uint64_t u64key;
	switch (mp_typeof(*field)) {
	case MP_UINT:
		u64key = mp_decode_uint(&field);
		*key_len = sizeof(uint64_t);
		return (const char *) &u64key;
		break;
	case MP_STR:
		return mp_decode_str(&field, key_len);
		break;
	case MP_BIN:
		return mp_decode_bin(&field, key_len);
		break;
	default:
		*key_len = 0;
		unreachable();
		return NULL;
	}
}

static int
memtx_bitset_index_replace(struct index *base, struct tuple *old_tuple,
			   struct tuple *new_tuple, enum dup_replace_mode mode,
			   struct tuple **result, struct tuple **successor)
{
	struct memtx_bitset_index *index = (struct memtx_bitset_index *)base;

	/* BITSET index doesn't support ordering. */
	*successor = NULL;

	assert(!base->def->opts.is_unique);
	assert(!base->def->key_def->is_multikey);
	assert(old_tuple != NULL || new_tuple != NULL);
	(void) mode;

	*result = NULL;

	if (old_tuple != NULL) {
#ifndef OLD_GOOD_BITSET
		uint32_t value = memtx_bitset_index_tuple_to_value(index, old_tuple);
#else /* #ifndef OLD_GOOD_BITSET */
		size_t value = tuple_to_value(old_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
		if (tt_bitset_index_contains_value(&index->index,
						   (size_t) value)) {
			*result = old_tuple;

			assert(old_tuple != new_tuple);
			tt_bitset_index_remove_value(&index->index, value);
#ifndef OLD_GOOD_BITSET
			memtx_bitset_index_unregister_tuple(index, old_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
		}
	}

	if (new_tuple != NULL) {
		const char *field = tuple_field_by_part(new_tuple,
				base->def->key_def->parts, MULTIKEY_NONE);
		uint32_t key_len;
		const void *key = make_key(field, &key_len);
#ifndef OLD_GOOD_BITSET
		memtx_bitset_index_register_tuple(index, new_tuple);
		uint32_t value = memtx_bitset_index_tuple_to_value(index, new_tuple);
#else /* #ifndef OLD_GOOD_BITSET */
		uint32_t value = tuple_to_value(new_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
		if (tt_bitset_index_insert(&index->index, key, key_len,
					   value) < 0) {
#ifndef OLD_GOOD_BITSET
			memtx_bitset_index_unregister_tuple(index, new_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
			diag_set(OutOfMemory, 0, "memtx_bitset_index", "insert");
			return -1;
		}
	}
	return 0;
}

static struct iterator *
memtx_bitset_index_create_iterator(struct index *base, enum iterator_type type,
				   const char *key, uint32_t part_count)
{
	struct memtx_bitset_index *index = (struct memtx_bitset_index *)base;
	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;

	assert(part_count == 0 || key != NULL);
	(void) part_count;

	struct bitset_index_iterator *it = (struct bitset_index_iterator *)
		mempool_alloc(&memtx->iterator_pool);
	if (!it) {
		diag_set(OutOfMemory, sizeof(*it),
			 "memtx_bitset_index", "iterator");
		return NULL;
	}

	iterator_create(&it->base, base);
	it->pool = &memtx->iterator_pool;
	it->base.next_raw = bitset_index_iterator_next_raw;
	it->base.next = memtx_iterator_next;
	it->base.free = bitset_index_iterator_free;

	tt_bitset_iterator_create(&it->bitset_it, realloc);
	const void *bitset_key = NULL;
	uint32_t bitset_key_size = 0;

	if (type != ITER_ALL) {
		assert(part_count == 1);
		bitset_key = make_key(key, &bitset_key_size);
	}

	struct tt_bitset_expr expr;
	tt_bitset_expr_create(&expr, realloc);

	int rc = 0;
	switch (type) {
	case ITER_ALL:
		rc = tt_bitset_index_expr_all(&expr);
		break;
	case ITER_EQ:
		rc = tt_bitset_index_expr_equals(&expr, bitset_key,
						 bitset_key_size);
		break;
	case ITER_BITS_ALL_SET:
		rc = tt_bitset_index_expr_all_set(&expr, bitset_key,
						  bitset_key_size);
		break;
	case ITER_BITS_ALL_NOT_SET:
		rc = tt_bitset_index_expr_all_not_set(&expr, bitset_key,
						      bitset_key_size);
		break;
	case ITER_BITS_ANY_SET:
		rc = tt_bitset_index_expr_any_set(&expr, bitset_key,
						  bitset_key_size);
		break;
	default:
		diag_set(UnsupportedIndexFeature, base->def,
			 "requested iterator type");
		goto fail;
	}

	if (rc != 0) {
		diag_set(OutOfMemory, 0, "memtx_bitset_index",
			 "iterator expression");
		goto fail;
	}

	if (tt_bitset_index_init_iterator(&index->index, &it->bitset_it,
					  &expr) != 0) {
		diag_set(OutOfMemory, 0, "memtx_bitset_index",
			 "iterator state");
		goto fail;
	}

	tt_bitset_expr_destroy(&expr);
	return (struct iterator *)it;
fail:
	tt_bitset_expr_destroy(&expr);
	mempool_free(&memtx->iterator_pool, it);
	return NULL;
}

static ssize_t
memtx_bitset_index_count(struct index *base, enum iterator_type type,
			 const char *key, uint32_t part_count)
{
	struct memtx_bitset_index *index = (struct memtx_bitset_index *)base;

	if (type == ITER_ALL)
		return memtx_bitset_index_size(base);

	assert(part_count == 1); /* checked by key_validate() */
	uint32_t bitset_key_size = 0;
	const void *bitset_key = make_key(key, &bitset_key_size);
	struct bit_iterator bit_it;
	size_t bit;
	if (type == ITER_BITS_ANY_SET) {
		/**
		 * Optimization: for an empty key return 0.
		 */
		bit_iterator_init(&bit_it, bitset_key, bitset_key_size, true);
		bit = bit_iterator_next(&bit_it);
		if (bit == SIZE_MAX)
			return 0;
		/**
		 * Optimiation: for a single bit key use
		 * bitset_index_count().
		 */
		if (bit_iterator_next(&bit_it) == SIZE_MAX)
			return tt_bitset_index_count(&index->index, bit);
	} else if (type == ITER_BITS_ALL_SET) {
		/**
		 * Optimization: for an empty key return 0.
		 */
		bit_iterator_init(&bit_it, bitset_key, bitset_key_size, true);
		bit = bit_iterator_next(&bit_it);
		if (bit == SIZE_MAX)
			return 0;
		/**
		 * Optimiation: for a single bit key use
		 * bitset_index_count().
		 */
		if (bit_iterator_next(&bit_it) == SIZE_MAX)
			return tt_bitset_index_count(&index->index, bit);
	} else if (type == ITER_BITS_ALL_NOT_SET) {
		/**
		 * Optimization: for an empty key return the number of items
		 * in the index.
		 */
		bit_iterator_init(&bit_it, bitset_key, bitset_key_size, true);
		bit = bit_iterator_next(&bit_it);
		if (bit == SIZE_MAX)
			return tt_bitset_index_size(&index->index);
		/**
		 * Optimiation: for the single bit key use
		 * bitset_index_count().
		 */
		if (bit_iterator_next(&bit_it) == SIZE_MAX)
			return tt_bitset_index_size(&index->index) -
				tt_bitset_index_count(&index->index, bit);
	}

	/* Call generic method */
	return generic_index_count(base, type, key, part_count);
}

static const struct index_vtab memtx_bitset_index_vtab = {
	/* .destroy = */ memtx_bitset_index_destroy,
	/* .commit_create = */ generic_index_commit_create,
	/* .abort_create = */ generic_index_abort_create,
	/* .commit_modify = */ generic_index_commit_modify,
	/* .commit_drop = */ generic_index_commit_drop,
	/* .update_def = */ generic_index_update_def,
	/* .depends_on_pk = */ generic_index_depends_on_pk,
	/* .def_change_requires_rebuild = */
		memtx_index_def_change_requires_rebuild,
	/* .size = */ memtx_bitset_index_size,
	/* .bsize = */ memtx_bitset_index_bsize,
	/* .min = */ generic_index_min,
	/* .max = */ generic_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ memtx_bitset_index_count,
	/* .get_raw = */ generic_index_get_raw,
	/* .get = */ generic_index_get,
	/* .replace = */ memtx_bitset_index_replace,
	/* .create_iterator = */ memtx_bitset_index_create_iterator,
	/* .create_snapshot_iterator = */
		generic_index_create_snapshot_iterator,
	/* .stat = */ generic_index_stat,
	/* .compact = */ generic_index_compact,
	/* .reset_stat = */ generic_index_reset_stat,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

struct index *
memtx_bitset_index_new(struct memtx_engine *memtx, struct index_def *def)
{
	assert(def->iid > 0);
	assert(!def->opts.is_unique);

	struct memtx_bitset_index *index =
		(struct memtx_bitset_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct memtx_bitset_index");
		return NULL;
	}
	if (index_create(&index->base, (struct engine *)memtx,
			 &memtx_bitset_index_vtab, def) != 0) {
		free(index);
		return NULL;
	}

#ifndef OLD_GOOD_BITSET
	index->spare_id = SPARE_ID_END;
	index->id_to_tuple = (struct matras *)malloc(sizeof(*index->id_to_tuple));
	if (index->id_to_tuple == NULL)
		panic("failed to allocate memtx bitset index");
	matras_create(index->id_to_tuple, MEMTX_EXTENT_SIZE, sizeof(struct tuple *),
		      memtx_index_extent_alloc, memtx_index_extent_free, memtx);

	index->tuple_to_id = mh_bitset_index_new();
#endif /* #ifndef OLD_GOOD_BITSET */

	tt_bitset_index_create(&index->index, realloc);
	return &index->base;
}
