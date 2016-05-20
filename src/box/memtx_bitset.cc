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

#include "tuple.h"

#ifndef OLD_GOOD_BITSET
#include "memtx_engine.h"
#include "small/matras.h"

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

void
MemtxBitset::registerTuple(struct tuple *tuple)
{
	uint32_t id;
	struct tuple **place;
	if (m_spare_id != SPARE_ID_END) {
		id = m_spare_id;
		void *mem = matras_get(m_id_to_tuple, id);
		m_spare_id = *(uint32_t *)mem;
		place = (struct tuple **)mem;
	} else {
		place = (struct tuple **)matras_alloc(m_id_to_tuple, &id);
	}
	*place = tuple;

	struct bitset_hash_entry entry;
	entry.id = id;
	entry.tuple = tuple;
	uint32_t pos = mh_bitset_index_put(m_tuple_to_id, &entry, 0, 0);
	if (pos == mh_end(m_tuple_to_id)) {
		*(uint32_t *)tuple = m_spare_id;
		m_spare_id = id;
		tnt_raise(OutOfMemory, (ssize_t) pos, "hash", "key");
	}
}

void
MemtxBitset::unregisterTuple(struct tuple *tuple)
{

	uint32_t k = mh_bitset_index_find(m_tuple_to_id, tuple, 0);
	struct bitset_hash_entry *e = mh_bitset_index_node(m_tuple_to_id, k);
	void *mem = matras_get(m_id_to_tuple, e->id);
	*(uint32_t *)mem = m_spare_id;
	m_spare_id = e->id;
	mh_bitset_index_del(m_tuple_to_id, k, 0);
}

uint32_t
MemtxBitset::tupleToValue(struct tuple *tuple) const
{
	uint32_t k = mh_bitset_index_find(m_tuple_to_id, tuple, 0);
	struct bitset_hash_entry *e = mh_bitset_index_node(m_tuple_to_id, k);
	return e->id;
}

struct tuple *
MemtxBitset::valueToTuple(uint32_t value) const
{
	void *mem = matras_get(m_id_to_tuple, value);
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
	struct bitset_iterator bitset_it;
#ifndef OLD_GOOD_BITSET
	const class MemtxBitset *bitset_index;
#endif /* #ifndef OLD_GOOD_BITSET */
};

static struct bitset_index_iterator *
bitset_index_iterator(struct iterator *it)
{
	return (struct bitset_index_iterator *) it;
}

void
bitset_index_iterator_free(struct iterator *iterator)
{
	assert(iterator->free == bitset_index_iterator_free);
	struct bitset_index_iterator *it = bitset_index_iterator(iterator);

	bitset_iterator_destroy(&it->bitset_it);
	free(it);
}

struct tuple *
bitset_index_iterator_next(struct iterator *iterator)
{
	assert(iterator->free == bitset_index_iterator_free);
	struct bitset_index_iterator *it = bitset_index_iterator(iterator);

	size_t value = bitset_iterator_next(&it->bitset_it);
	if (value == SIZE_MAX)
		return NULL;

#ifndef OLD_GOOD_BITSET
	return it->bitset_index->valueToTuple((uint32_t)value);
#else /* #ifndef OLD_GOOD_BITSET */
	return value_to_tuple(value);
#endif /* #ifndef OLD_GOOD_BITSET */
}

MemtxBitset::MemtxBitset(struct key_def *key_def)
	: MemtxIndex(key_def)
{
	assert(!this->key_def->opts.is_unique);

#ifndef OLD_GOOD_BITSET
	m_spare_id = SPARE_ID_END;
	m_id_to_tuple = (struct matras *)malloc(sizeof(*m_id_to_tuple));
	if (!m_id_to_tuple)
		panic_syserror("bitset_index_create");
	matras_create(m_id_to_tuple, MEMTX_EXTENT_SIZE, sizeof(struct tuple *),
		      memtx_index_extent_alloc, memtx_index_extent_free);

	m_tuple_to_id = mh_bitset_index_new();
	if (!m_tuple_to_id)
		panic_syserror("bitset_index_create");
#endif /* #ifndef OLD_GOOD_BITSET */
	if (bitset_index_create(&m_index, realloc) != 0)
		panic_syserror("bitset_index_create");

}

MemtxBitset::~MemtxBitset()
{
	bitset_index_destroy(&m_index);
#ifndef OLD_GOOD_BITSET
	mh_bitset_index_delete(m_tuple_to_id);
	matras_destroy(m_id_to_tuple);
	free(m_id_to_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
}

size_t
MemtxBitset::size() const
{
	return bitset_index_size(&m_index);
}

size_t
MemtxBitset::bsize() const
{
	size_t result = 0;
	result += bitset_index_bsize(&m_index);
#ifndef OLD_GOOD_BITSET
	result += matras_extent_count(m_id_to_tuple) * MEMTX_EXTENT_SIZE;
	result += mh_bitset_index_memsize(m_tuple_to_id);
#endif /* #ifndef OLD_GOOD_BITSET */
	return result;
}

struct iterator *
MemtxBitset::allocIterator() const
{
	struct bitset_index_iterator *it = (struct bitset_index_iterator *)
			malloc(sizeof(*it));
	if (!it)
		return NULL;

	memset(it, 0, sizeof(*it));
	it->base.next = bitset_index_iterator_next;
	it->base.free = bitset_index_iterator_free;

	bitset_iterator_create(&it->bitset_it, realloc);
#ifndef OLD_GOOD_BITSET
	it->bitset_index = this;
#endif

	return (struct iterator *) it;
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
	default:
		*key_len = 0;
		assert(false);
		return NULL;
	}
}

struct tuple *
MemtxBitset::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		     enum dup_replace_mode mode)
{
	assert(!key_def->opts.is_unique);
	assert(old_tuple != NULL || new_tuple != NULL);
	(void) mode;

	struct tuple *ret = NULL;

	if (old_tuple != NULL) {
#ifndef OLD_GOOD_BITSET
		uint32_t value = tupleToValue(old_tuple);
#else /* #ifndef OLD_GOOD_BITSET */
		size_t value = tuple_to_value(old_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
		if (bitset_index_contains_value(&m_index, (size_t)value)) {
			ret = old_tuple;

			assert(old_tuple != new_tuple);
			bitset_index_remove_value(&m_index, value);
#ifndef OLD_GOOD_BITSET
			unregisterTuple(old_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
		}
	}

	if (new_tuple != NULL) {
		const char *field;
		field = tuple_field(new_tuple, key_def->parts[0].fieldno);
		uint32_t key_len;
		const void *key = make_key(field, &key_len);
#ifndef OLD_GOOD_BITSET
		registerTuple(new_tuple);
		uint32_t value = tupleToValue(new_tuple);
#else /* #ifndef OLD_GOOD_BITSET */
		uint32_t value = tuple_to_value(new_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
		if (bitset_index_insert(&m_index, key, key_len, value) < 0) {
#ifndef OLD_GOOD_BITSET
			unregisterTuple(new_tuple);
#endif /* #ifndef OLD_GOOD_BITSET */
			tnt_raise(OutOfMemory, 0, "MemtxBitset", "insert");
		}
	}

	return ret;
}

void
MemtxBitset::initIterator(struct iterator *iterator, enum iterator_type type,
			  const char *key, uint32_t part_count) const
{
	assert(iterator->free == bitset_index_iterator_free);
	assert(part_count == 0 || key != NULL);
	(void) part_count;

	struct bitset_index_iterator *it = bitset_index_iterator(iterator);
#ifndef OLD_GOOD_BITSET
	assert(it->bitset_index == this);
#endif /* #ifndef OLD_GOOD_BITSET */

	const void *bitset_key = NULL;
	uint32_t bitset_key_size = 0;

	if (type != ITER_ALL) {
		assert(part_count == 1);
		bitset_key = make_key(key, &bitset_key_size);
	}

	struct bitset_expr expr;
	bitset_expr_create(&expr, realloc);
	try {
		int rc = 0;
		switch (type) {
		case ITER_ALL:
			rc = bitset_index_expr_all(&expr);
			break;
		case ITER_EQ:
			rc = bitset_index_expr_equals(&expr, bitset_key,
						      bitset_key_size);
			break;
		case ITER_BITS_ALL_SET:
			rc = bitset_index_expr_all_set(&expr, bitset_key,
						       bitset_key_size);
			break;
		case ITER_BITS_ALL_NOT_SET:
			rc = bitset_index_expr_all_not_set(&expr, bitset_key,
							   bitset_key_size);
			break;
		case ITER_BITS_ANY_SET:
			rc = bitset_index_expr_any_set(&expr, bitset_key,
						       bitset_key_size);
			break;
		default:
			return initIterator(iterator, type, key, part_count);
		}

		if (rc != 0) {
			tnt_raise(OutOfMemory, 0, "MemtxBitset",
				  "iterator expression");
		}

		if (bitset_index_init_iterator((bitset_index *) &m_index,
					       &it->bitset_it,
					       &expr) != 0) {
			tnt_raise(OutOfMemory, 0, "MemtxBitset",
				  "iterator state");
		}

		bitset_expr_destroy(&expr);
	} catch (Exception *e) {
		bitset_expr_destroy(&expr);
		throw;
	}
}

size_t
MemtxBitset::count(enum iterator_type type, const char *key,
		   uint32_t part_count) const
{
	if (type == ITER_ALL)
		return bitset_index_size(&m_index);

	assert(part_count == 1); /* checked by key_validate() */
	uint32_t bitset_key_size = 0;
	const void *bitset_key = make_key(key, &bitset_key_size);
	struct bit_iterator bit_it;
	size_t bit;
	if (type == ITER_BITS_ANY_SET) {
		/*
		 * Optimization: get the number of items for each requested bit
		 * and then found the maximum.
		 */
		bit_iterator_init(&bit_it, bitset_key, bitset_key_size, true);
		size_t result = 0;
		while ((bit = bit_iterator_next(&bit_it)) != SIZE_MAX) {
			size_t count = bitset_index_count(&m_index, bit);
			result = MAX(result, count);
		}
		return result;
	} else if (type == ITER_BITS_ALL_SET) {
		/**
		 * Optimization: for an empty key return the number of items
		 * in the index.
		 */
		bit_iterator_init(&bit_it, bitset_key, bitset_key_size, true);
		bit = bit_iterator_next(&bit_it);
		if (bit == SIZE_MAX)
			return bitset_index_size(&m_index);
		/**
		 * Optimiation: for a single bit key use
		 * bitset_index_count().
		 */
		if (bit_iterator_next(&bit_it) == SIZE_MAX)
			return bitset_index_count(&m_index, bit);
	} else if (type == ITER_BITS_ALL_NOT_SET) {
		/**
		 * Optimization: for an empty key return the number of items
		 * in the index.
		 */
		bit_iterator_init(&bit_it, bitset_key, bitset_key_size, true);
		bit = bit_iterator_next(&bit_it);
		if (bit == SIZE_MAX)
			return bitset_index_size(&m_index);
		/**
		 * Optimiation: for the single bit key use
		 * bitset_index_count().
		 */
		if (bit_iterator_next(&bit_it) == SIZE_MAX)
			return bitset_index_size(&m_index) - bitset_index_count(&m_index, bit);
	}

	/* Call generic method */
	return MemtxIndex::count(type, key, part_count);
}
