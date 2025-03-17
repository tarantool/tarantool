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

enum {
	HASH_SEED = 13U
};

static uint32_t
hash_mp_uint(uint32_t *ph, uint32_t *pcarry, uint64_t num)
{
	char data[16];
	uint32_t size;
	if (num <= UINT8_MAX) {
		mp_store_u8(data, (uint8_t)num);
		size = 1;
	} else if (num <= UINT16_MAX) {
		mp_store_u16(data, (uint16_t)num);
		size = 2;
	} else if (num <= UINT32_MAX) {
		mp_store_u32(data, (uint32_t)num);
		size = 4;
	} else {
		mp_store_u64(data, num);
		size = 8;
	}
	PMurHash32_Process(ph, pcarry, data, size);
	return size;
}

static uint32_t
hash_mp_nint(uint32_t *ph, uint32_t *pcarry, int64_t num)
{
	assert(num < 0);
	char data[16];
	uint32_t size;
	if (num >= INT8_MIN) {
		mp_store_u8(data, (uint8_t)num);
		size = 1;
	} else if (num >= INT16_MIN) {
		mp_store_u16(data, (uint16_t)num);
		size = 2;
	} else if (num >= INT32_MIN) {
		mp_store_u32(data, (uint32_t)num);
		size = 4;
	} else {
		mp_store_u64(data, num);
		size = 8;
	}
	PMurHash32_Process(ph, pcarry, data, size);
	return size;
}

static uint32_t
hash_mp_int(uint32_t *ph, uint32_t *pcarry, int64_t num)
{
	if (num >= 0)
		return hash_mp_uint(ph, pcarry, num);
	return hash_mp_nint(ph, pcarry, num);
}

static uint32_t
hash_mp_double(uint32_t *ph, uint32_t *pcarry, double num)
{
	char buf[16];
	char *end = mp_store_double(buf, num);
	size_t size = end - buf;
	PMurHash32_Process(ph, pcarry, buf, size);
	return size;
}

template <bool has_optional_parts, bool has_json_paths>
uint32_t
tuple_hash_impl(struct tuple *tuple, struct key_def *key_def);

void
key_def_set_hash_func(struct key_def *key_def) {
	if (key_def->has_optional_parts) {
		if (key_def->has_json_paths)
			key_def->tuple_hash = tuple_hash_impl<true, true>;
		else
			key_def->tuple_hash = tuple_hash_impl<true, false>;
	} else {
		if (key_def->has_json_paths)
			key_def->tuple_hash = tuple_hash_impl<false, true>;
		else
			key_def->tuple_hash = tuple_hash_impl<false, false>;
	}
}

uint32_t
tuple_hash_field(uint32_t *ph1, uint32_t *pcarry, const char **field,
		 struct coll *coll)
{
	const char *f = *field;
	uint32_t size;
	switch (mp_typeof(**field)) {
	case MP_UINT:
		return hash_mp_uint(ph1, pcarry, mp_decode_uint(field));
	case MP_INT:
		return hash_mp_int(ph1, pcarry, mp_decode_int(field));
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
		    val < -exp2(63) || val >= exp2(64))
			return hash_mp_double(ph1, pcarry, val);
		if (val >= 0)
			return hash_mp_uint(ph1, pcarry, (uint64_t)val);
		return hash_mp_nint(ph1, pcarry, (int64_t)val);
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

uint32_t
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

template <bool has_optional_parts, bool has_json_paths>
uint32_t
tuple_hash_impl(struct tuple *tuple, struct key_def *key_def)
{
	assert(has_json_paths == key_def->has_json_paths);
	assert(has_optional_parts == key_def->has_optional_parts);
	assert(!key_def->is_multikey);
	assert(!key_def->for_func_index);
	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;
	uint32_t prev_fieldno = key_def->parts[0].fieldno;
	struct tuple_format *format = tuple_format(tuple);
	const char *tuple_raw = tuple_data(tuple);
	const uint32_t *field_map = tuple_field_map(tuple);
	const char *field;
	if (has_json_paths) {
		field = tuple_field_raw_by_part(format, tuple_raw, field_map,
						key_def->parts, MULTIKEY_NONE);
	} else {
		field = tuple_field_raw(format, tuple_raw, field_map,
					prev_fieldno);
	}
	const char *end = (char *)tuple + tuple_size(tuple);
	if (has_optional_parts && field == NULL) {
		total_size += tuple_hash_null(&h, &carry);
	} else {
		total_size += tuple_hash_field(&h, &carry, &field,
					       key_def->parts[0].coll);
	}
	for (uint32_t part_id = 1; part_id < key_def->part_count; part_id++) {
		struct key_part *part = &key_def->parts[part_id];

		/* If parts of key_def are not sequential we need to call
		 * tuple_field. Otherwise, tuple is hashed sequentially without
		 * need of tuple_field.
		 * JSON fields are not stored sequentially in memory, they must
		 * be extracted explicitly.
		 */
		if (has_json_paths) {
			field = tuple_field_raw_by_part(
				format, tuple_raw, field_map, part,
				MULTIKEY_NONE);
		} else if (prev_fieldno + 1 != part->fieldno) {
			field = tuple_field_raw(
				format, tuple_raw, field_map, part->fieldno);
		}

		if (has_optional_parts && (field == NULL || field >= end)) {
			total_size += tuple_hash_null(&h, &carry);
		} else {
			total_size +=
				tuple_hash_field(&h, &carry, &field,
						 key_def->parts[part_id].coll);
		}
		prev_fieldno = key_def->parts[part_id].fieldno;
	}

	return PMurHash32_Result(h, carry, total_size);
}

uint32_t
key_hash(const char *key, struct key_def *key_def)
{
	uint32_t h = HASH_SEED;
	uint32_t carry = 0;
	uint32_t total_size = 0;

	for (struct key_part *part = key_def->parts;
	     part < key_def->parts + key_def->part_count; part++)
		total_size += tuple_hash_field(&h, &carry, &key, part->coll);

	return PMurHash32_Result(h, carry, total_size);
}
