/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "tuple_hash.h"
#include "tuple.h"
#include <PMurHash.h>
#include "coll/coll.h"
#include <math.h>

/* Tuple and key hasher */
namespace {

enum {
	HASH_SEED = 13U
};

template <int TYPE>
static inline uint32_t
field_hash(uint32_t *ph, uint32_t *pcarry, const char **field)
{
	/*
	* (!) All fields, except TYPE_STRING hashed **including** MsgPack format
	* identifier (e.g. 0xcc). This was done **intentionally**
	* for performance reasons. Please follow MsgPack specification
	* and pack all your numbers to the most compact representation.
	* If you still want to add support for broken MsgPack,
	* please don't forget to patch tuple_compare_field().
	*/
	const char *f = *field;
	uint32_t size;
	mp_next(field);
	size = *field - f;  /* calculate the size of field */
	assert(size < INT32_MAX);
	PMurHash32_Process(ph, pcarry, f, size);
	return size;
}

template <>
inline uint32_t
field_hash<FIELD_TYPE_STRING>(uint32_t *ph, uint32_t *pcarry,
			      const char **pfield)
{
	/*
	* (!) MP_STR fields hashed **excluding** MsgPack format
	* indentifier. We have to do that to keep compatibility
	* with old third-party MsgPack (spec-old.md) implementations.
	* \sa https://github.com/tarantool/tarantool/issues/522
	*/
	uint32_t size;
	const char *f = mp_decode_str(pfield, &size);
	assert(size < INT32_MAX);
	PMurHash32_Process(ph, pcarry, f, size);
	return size;
}

template <int TYPE, int ...MORE_TYPES> struct KeyFieldHash {};

template <int TYPE, int TYPE2, int ...MORE_TYPES>
struct KeyFieldHash<TYPE, TYPE2, MORE_TYPES...> {
	static void hash(uint32_t *ph, uint32_t *pcarry,
			 const char **pfield, uint32_t *ptotal_size)
	{
		*ptotal_size += field_hash<TYPE>(ph, pcarry, pfield);
		KeyFieldHash<TYPE2, MORE_TYPES...>::
			hash(ph, pcarry, pfield, ptotal_size);
	}
};

template <int TYPE>
struct KeyFieldHash<TYPE> {
	static void hash(uint32_t *ph, uint32_t *pcarry,
			 const char **pfield, uint32_t *ptotal_size)
	{
		*ptotal_size += field_hash<TYPE>(ph, pcarry, pfield);
	}
};

template <int TYPE, int ...MORE_TYPES>
struct KeyHash {
	static uint32_t hash(const char *key, struct key_def *)
	{
		uint32_t h = HASH_SEED;
		uint32_t carry = 0;
		uint32_t total_size = 0;
		KeyFieldHash<TYPE, MORE_TYPES...>::hash(&h, &carry, &key,
							&total_size);
		return PMurHash32_Result(h, carry, total_size);
	}
};

template <>
struct KeyHash<FIELD_TYPE_UNSIGNED> {
	static uint32_t hash(const char *key, struct key_def *key_def)
	{
		uint64_t val = mp_decode_uint(&key);
		(void) key_def;
		if (likely(val <= UINT32_MAX))
			return val;
		return ((uint32_t)((val)>>33^(val)^(val)<<11));
	}
};

template <int TYPE, int ...MORE_TYPES> struct TupleFieldHash { };

template <int TYPE, int TYPE2, int ...MORE_TYPES>
struct TupleFieldHash<TYPE, TYPE2, MORE_TYPES...> {
	static void hash(const char **pfield, uint32_t *ph, uint32_t *pcarry,
			 uint32_t *ptotal_size)
	{
		*ptotal_size += field_hash<TYPE>(ph, pcarry, pfield);
		TupleFieldHash<TYPE2, MORE_TYPES...>::
			hash(pfield, ph, pcarry, ptotal_size);
	}
};

template <int TYPE>
struct TupleFieldHash<TYPE> {
	static void hash(const char **pfield, uint32_t *ph, uint32_t *pcarry,
			 uint32_t *ptotal_size)
	{
		*ptotal_size += field_hash<TYPE>(ph, pcarry, pfield);
	}
};

template <int TYPE, int ...MORE_TYPES>
struct TupleHash
{
	static uint32_t hash(struct tuple_multikey tuple_multikey,
			     struct key_def *key_def)
	{
		struct tuple *tuple = tuple_multikey.tuple;
		uint32_t multikey_idx = tuple_multikey.multikey_idx;
		uint32_t h = HASH_SEED;
		uint32_t carry = 0;
		uint32_t total_size = 0;
		const char *field = tuple_field_by_part(tuple,
						key_def->parts,
						(int)multikey_idx);
		TupleFieldHash<TYPE, MORE_TYPES...>::
			hash(&field, &h, &carry, &total_size);
		return PMurHash32_Result(h, carry, total_size);
	}
};

template <>
struct TupleHash<FIELD_TYPE_UNSIGNED> {
	static uint32_t hash(struct tuple_multikey tuple_multikey,
			     struct key_def *key_def)
	{
		struct tuple *tuple = tuple_multikey.tuple;
		uint32_t multikey_idx = tuple_multikey.multikey_idx;
		const char *field = tuple_field_by_part(tuple,
							key_def->parts,
							(int)multikey_idx);
		uint64_t val = mp_decode_uint(&field);
		if (likely(val <= UINT32_MAX))
			return val;
		return ((uint32_t)((val)>>33^(val)^(val)<<11));
	}
};

}; /* namespace { */

#define HASHER(...) \
	{ KeyHash<__VA_ARGS__>::hash, TupleHash<__VA_ARGS__>::hash, \
		{ __VA_ARGS__, UINT32_MAX } },

struct hasher_signature {
	key_hash_t kf;
	tuple_hash_t tf;
	uint32_t p[64];
};

/**
 * field1 type,  field2 type, ...
 */
static const hasher_signature hash_arr[] = {
	HASHER(FIELD_TYPE_UNSIGNED)
	HASHER(FIELD_TYPE_STRING)
	HASHER(FIELD_TYPE_UNSIGNED, FIELD_TYPE_UNSIGNED)
	HASHER(FIELD_TYPE_STRING  , FIELD_TYPE_UNSIGNED)
	HASHER(FIELD_TYPE_UNSIGNED, FIELD_TYPE_STRING)
	HASHER(FIELD_TYPE_STRING  , FIELD_TYPE_STRING)
	HASHER(FIELD_TYPE_UNSIGNED, FIELD_TYPE_UNSIGNED, FIELD_TYPE_UNSIGNED)
	HASHER(FIELD_TYPE_STRING  , FIELD_TYPE_UNSIGNED, FIELD_TYPE_UNSIGNED)
	HASHER(FIELD_TYPE_UNSIGNED, FIELD_TYPE_STRING  , FIELD_TYPE_UNSIGNED)
	HASHER(FIELD_TYPE_STRING  , FIELD_TYPE_STRING  , FIELD_TYPE_UNSIGNED)
	HASHER(FIELD_TYPE_UNSIGNED, FIELD_TYPE_UNSIGNED, FIELD_TYPE_STRING)
	HASHER(FIELD_TYPE_STRING  , FIELD_TYPE_UNSIGNED, FIELD_TYPE_STRING)
	HASHER(FIELD_TYPE_UNSIGNED, FIELD_TYPE_STRING  , FIELD_TYPE_STRING)
	HASHER(FIELD_TYPE_STRING  , FIELD_TYPE_STRING  , FIELD_TYPE_STRING)
};

#undef HASHER

template <bool has_optional_parts, bool has_json_paths>
uint32_t
tuple_hash_slowpath(struct tuple_multikey tuple_multikey,
		    struct key_def *key_def);

uint32_t
key_hash_slowpath(const char *key, struct key_def *key_def);

void
key_def_set_hash_func(struct key_def *key_def) {
	if (key_def->is_nullable || key_def->has_json_paths)
		goto slowpath;
	/*
	 * Check that key_def defines sequential a key without holes
	 * starting from **arbitrary** field.
	 */
	for (uint32_t i = 1; i < key_def->part_count; i++) {
		if (key_def->parts[i - 1].fieldno + 1 !=
		    key_def->parts[i].fieldno)
			goto slowpath;
	}
	if (key_def_has_collation(key_def)) {
		/* Precalculated comparators don't use collation */
		goto slowpath;
	}
	/*
	 * Try to find pre-generated tuple_hash() and key_hash()
	 * implementations
	 */
	for (uint32_t k = 0; k < sizeof(hash_arr) / sizeof(hash_arr[0]); k++) {
		uint32_t i = 0;
		for (; i < key_def->part_count; i++) {
			if (key_def->parts[i].type != hash_arr[k].p[i]) {
				break;
			}
		}
		if (i == key_def->part_count && hash_arr[k].p[i] == UINT32_MAX){
			key_def->tuple_hash = hash_arr[k].tf;
			key_def->key_hash = hash_arr[k].kf;
			return;
		}
	}

slowpath:
	if (key_def->has_optional_parts) {
		if (key_def->has_json_paths)
			key_def->tuple_hash = tuple_hash_slowpath<true, true>;
		else
			key_def->tuple_hash = tuple_hash_slowpath<true, false>;
	} else {
		if (key_def->has_json_paths)
			key_def->tuple_hash = tuple_hash_slowpath<false, true>;
		else
			key_def->tuple_hash = tuple_hash_slowpath<false, false>;
	}
	key_def->key_hash = key_hash_slowpath;
}

uint32_t
tuple_hash_field(uint32_t *ph1, uint32_t *pcarry, const char **field,
		 struct coll *coll)
{
	char buf[9]; /* enough to store MP_INT/MP_UINT */
	const char *f = *field;
	uint32_t size;

	switch (mp_typeof(**field)) {
	case MP_STR:
		/*
		 * (!) MP_STR fields hashed **excluding** MsgPack format
		 * indentifier. We have to do that to keep compatibility
		 * with old third-party MsgPack (spec-old.md) implementations.
		 * \sa https://github.com/tarantool/tarantool/issues/522
		 */
		f = mp_decode_str(field, &size);
		if (coll != NULL)
			return coll->hash(f, size, ph1, pcarry, coll);
		break;
	case MP_FLOAT:
	case MP_DOUBLE: {
		/*
		 * If a floating point number can be stored as an integer,
		 * convert it to MP_INT/MP_UINT before hashing so that we
		 * can select integer values by floating point keys and
		 * vice versa.
		 */
		double iptr;
		double val = mp_typeof(**field) == MP_FLOAT ?
			     mp_decode_float(field) :
			     mp_decode_double(field);
		if (!isfinite(val) || modf(val, &iptr) != 0 ||
		    val < -exp2(63) || val >= exp2(64)) {
			size = *field - f;
			break;
		}
		char *data;
		if (val >= 0)
			data = mp_encode_uint(buf, (uint64_t)val);
		else
			data = mp_encode_int(buf, (int64_t)val);
		size = data - buf;
		assert(size <= sizeof(buf));
		f = buf;
		break;
	}
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
tuple_hash_null(uint32_t *ph1, uint32_t *pcarry)
{
	assert(mp_sizeof_nil() == 1);
	const char null = 0xc0;
	PMurHash32_Process(ph1, pcarry, &null, 1);
	return mp_sizeof_nil();
}

uint32_t
tuple_hash_key_part(uint32_t *ph1, uint32_t *pcarry, struct tuple *tuple,
		    struct key_part *part, int multikey_idx)
{
	const char *field = tuple_field_by_part(tuple, part, multikey_idx);
	if (field == NULL)
		return tuple_hash_null(ph1, pcarry);
	return tuple_hash_field(ph1, pcarry, &field, part->coll);
}

template <bool has_json_paths>
const char *
tuple_get_field(struct tuple *tuple, struct key_part *part,
		uint32_t multikey_idx)
{
	struct tuple_format *format = tuple_format(tuple);
	const char *tuple_raw = tuple_data(tuple);
	const uint32_t *field_map = tuple_field_map(tuple);
	if (has_json_paths) {
		return tuple_field_raw_by_part(format, tuple_raw, field_map,
					       part, (int)multikey_idx);
	} else {
		return tuple_field_raw(format, tuple_raw, field_map,
				       part->fieldno);
	}
}

template <bool has_optional_parts>
void
tuple_hash_part(uint32_t *h, uint32_t *carry, uint32_t *total_size,
		const char *tuple_end, const char **field, struct coll *coll)
{
	if (has_optional_parts && (*field == NULL || *field >= tuple_end)) {
		*total_size += tuple_hash_null(h, carry);
	} else {
		*total_size += tuple_hash_field(h, carry, field, coll);
	}
}

template <bool has_optional_parts, bool has_json_paths>
uint32_t
tuple_hash_slowpath(struct tuple_multikey tuple_multikey,
		    struct key_def *key_def)
{
	assert(has_json_paths == key_def->has_json_paths);
	assert(has_optional_parts == key_def->has_optional_parts);
	struct tuple *tuple = tuple_multikey.tuple;
	uint32_t multikey_idx = tuple_multikey.multikey_idx;
	assert((multikey_idx == (uint32_t)MULTIKEY_NONE) ||
	       key_def->is_multikey);
	assert(!(key_def->is_multikey ^ has_json_paths));
	assert(!key_def->for_func_index);
	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;
	uint32_t prev_fieldno = key_def->parts[0].fieldno;
	const char *field = tuple_get_field<has_json_paths>(tuple,
							    key_def->parts,
							    multikey_idx);
	const char *tuple_end = (char *)tuple + tuple_size(tuple);
	tuple_hash_part<has_optional_parts>(&h, &carry, &total_size, tuple_end,
					    &field, key_def->parts[0].coll);
	for (uint32_t part_id = 1; part_id < key_def->part_count; part_id++) {
		/* If parts of key_def are not sequential we need to call
		 * tuple_field. Otherwise, tuple is hashed sequentially without
		 * need of tuple_field
		 */
		struct key_part *part = &key_def->parts[part_id];
		if (prev_fieldno + 1 != part->fieldno)
			field = tuple_get_field<has_json_paths>(tuple,
								part,
								multikey_idx);
		tuple_hash_part<has_optional_parts>(
			&h, &carry, &total_size, tuple_end, &field,
			key_def->parts[part_id].coll);
		prev_fieldno = key_def->parts[part_id].fieldno;
	}

	return PMurHash32_Result(h, carry, total_size);
}

uint32_t
key_hash_slowpath(const char *key, struct key_def *key_def)
{
	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	for (struct key_part *part = key_def->parts;
	     part < key_def->parts + key_def->part_count; part++) {
		total_size += tuple_hash_field(&h, &carry, &key, part->coll);
	}

	return PMurHash32_Result(h, carry, total_size);
}
