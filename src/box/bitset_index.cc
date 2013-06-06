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

#include "bitset_index.h"

#include <string.h>

#include "salloc.h"
#include "tuple.h"
#include "space.h"
#include "exception.h"
#include "pickle.h"
#include <lib/bitset/index.h>

static struct index_traits bitset_index_traits = {
	/* .allows_partial_key = */ false,
};

static inline size_t
tuple_to_value(struct tuple *tuple)
{
	size_t value = salloc_ptr_to_index(tuple);
	assert(salloc_ptr_from_index(value) == tuple);
	return value;
}

static inline struct tuple *
value_to_tuple(size_t value)
{
	return (struct tuple *) salloc_ptr_from_index(value);
}

struct bitset_index_iterator {
	struct iterator base; /* Must be the first member. */
	struct bitset_iterator bitset_it;
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

	return value_to_tuple(value);
}

BitsetIndex::BitsetIndex(struct key_def *key_def, struct space *space)
	: Index(key_def, space)
{
	assert (!key_def->is_unique);

	if (bitset_index_create(&index, realloc) != 0)
		panic_syserror("bitset_index_create");
}

BitsetIndex::~BitsetIndex()
{
	bitset_index_destroy(&index);
}

void
BitsetIndex::beginBuild()
{
	tnt_raise(ClientError, ER_UNSUPPORTED, "BitsetIndex", "beginBuild()");
}

void
BitsetIndex::buildNext(struct tuple *tuple)
{
	(void) tuple;
	tnt_raise(ClientError, ER_UNSUPPORTED, "BitsetIndex", "buildNext()");
}

void
BitsetIndex::endBuild()
{
	tnt_raise(ClientError, ER_UNSUPPORTED, "BitsetIndex", "endBuild()");
}

void
BitsetIndex::build(Index *pk)
{
	assert (!key_def->is_unique);

	struct iterator *it = pk->position();
	struct tuple *tuple;
	pk->initIterator(it, ITER_ALL, NULL, 0);

	while ((tuple = it->next(it)))
		replace(NULL, tuple, DUP_INSERT);
}

size_t
BitsetIndex::size() const
{
	return bitset_index_size(&index);
}

struct tuple *
BitsetIndex::min() const
{
	tnt_raise(ClientError, ER_UNSUPPORTED, "BitsetIndex", "min()");
	return NULL;
}

struct tuple *
BitsetIndex::max() const
{
	tnt_raise(ClientError, ER_UNSUPPORTED, "BitsetIndex", "max()");
	return NULL;
}

struct tuple *
BitsetIndex::random(u32 rnd) const
{
	(void) rnd;
	tnt_raise(ClientError, ER_UNSUPPORTED, "BitsetIndex", "random()");
	return NULL;
}

struct iterator *
BitsetIndex::allocIterator() const
{
	struct bitset_index_iterator *it = (struct bitset_index_iterator *)
			malloc(sizeof(*it));
	if (!it)
		return NULL;

	memset(it, 0, sizeof(*it));
	it->base.next = bitset_index_iterator_next;
	it->base.free = bitset_index_iterator_free;

	bitset_iterator_create(&it->bitset_it, realloc);

	return (struct iterator *) it;
}

struct tuple *
BitsetIndex::findByKey(const void *key, u32 part_count) const
{
	(void) key;
	(void) part_count;
	tnt_raise(ClientError, ER_UNSUPPORTED, "BitsetIndex", "findByKey()");
	return NULL;
}

struct tuple *
BitsetIndex::findByTuple(struct tuple *tuple) const
{
	(void) tuple;
	tnt_raise(ClientError, ER_UNSUPPORTED, "BitsetIndex", "findByTuple()");
	return NULL;
}

struct tuple *
BitsetIndex::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		     enum dup_replace_mode mode)
{
	assert(!key_def->is_unique);
	assert(old_tuple != NULL || new_tuple != NULL);
	(void) mode;

	struct tuple *ret = NULL;

	if (old_tuple != NULL) {
		size_t value = tuple_to_value(old_tuple);
		if (bitset_index_contains_value(&index, value)) {
			ret = old_tuple;

			assert (old_tuple != new_tuple);
			bitset_index_remove_value(&index, value);
		}
	}

	if (new_tuple != NULL) {
		const void *field = tuple_field(new_tuple,
						key_def->parts[0].fieldno);
		assert (field != NULL);
		size_t bitset_key_size = (size_t) load_varint32(&field);
		const void *bitset_key = field;

		size_t value = tuple_to_value(new_tuple);
		if (bitset_index_insert(&index, bitset_key,
					bitset_key_size, value) < 0) {
			tnt_raise(ClientError, ER_MEMORY_ISSUE, 0,
				  "BitsetIndex", "insert");
		}
	}

	return ret;
}

void
BitsetIndex::initIterator(struct iterator *iterator, enum iterator_type type,
			  const void *key, u32 part_count) const
{
	assert(iterator->free == bitset_index_iterator_free);
	struct bitset_index_iterator *it = bitset_index_iterator(iterator);

	const void *bitset_key = NULL;
	size_t bitset_key_size = 0;

	if (type != ITER_ALL) {
		check_key_parts(key_def, part_count,
				bitset_index_traits.allows_partial_key);
		const void *key2 = key;
		bitset_key_size = (size_t) load_varint32(&key2);
		bitset_key = key2;
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
			tnt_raise(ClientError, ER_UNSUPPORTED,
				  "BitsetIndex", "requested iterator type");
		}

		if (rc != 0) {
			tnt_raise(ClientError, ER_MEMORY_ISSUE,
				  0, "BitsetIndex", "iterator expression");
		}

		if (bitset_index_init_iterator((bitset_index *) &index,
					       &it->bitset_it,
					       &expr) != 0) {
			tnt_raise(ClientError, ER_MEMORY_ISSUE,
				  0, "BitsetIndex", "iterator state");
		}

		bitset_expr_destroy(&expr);
	} catch (const Exception& e) {
		bitset_expr_destroy(&expr);
		throw;
	}
}

