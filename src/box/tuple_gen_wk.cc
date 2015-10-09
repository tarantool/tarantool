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
#include "tuple_gen.h"
#include "tuple.h"


template <int TYPE>
int field_compare_with_key(const char *&field, const char *&key);

template <>
inline int field_compare_with_key<NUM>(const char *&field, const char *&key) {
	return mp_compare_uint(field, key);
}
template <>
inline int field_compare_with_key<STRING>(const char *&field, const char *&key) {
	uint32_t size_a, size_b;
	const char *key_field;
	key_field = key;
	size_a = mp_decode_strl(&field);
	size_b = mp_decode_strl(&key_field);
	int r = memcmp(field, key_field, MIN(size_a, size_b));
	if (r == 0)
		r = size_a < size_b ? -1 : size_a > size_b;
	return r;
}

template <int TYPE>
int field_compare_buf(const char *&field_a, const char *&field_b);
template <>
inline int field_compare_buf<NUM>(const char *&field_a, const char *&field_b) {
	return mp_compare_uint(field_a, field_b);
}
template <>
inline int field_compare_buf<STRING>(const char *&field_a, const char *&field_b) {
	uint32_t size_a, size_b;
	const char *fa = field_a, *fb = field_b;
	size_a = mp_decode_strl(&fa);
	size_b = mp_decode_strl(&fb);
	int r = memcmp(fa, fb, MIN(size_a, size_b));
	if (r == 0)
		r = size_a < size_b ? -1 : size_a > size_b;
	return r;
}

/* Tuple with key comparer */
template<int FLD_ID, int IDX, int TYPE, int ...MORE_TYPES>
struct TupComp {};
/**
 * common
 */
template<int FLD_ID, int IDX, int TYPE, int IDX2, int ...MORE_TYPES>
struct TupComp<FLD_ID, IDX, TYPE, IDX2, MORE_TYPES...> {
	inline static int compareWithKey(const struct tuple *tuple,
					 const char *key,
					 uint32_t part_count,
					 const struct key_def *key_def,
					 const struct tuple_format *format,
					 const char *field)
	{
		int r;
		/* static if */
		if (IDX + 1 == IDX2) {
			r = field_compare_buf<TYPE>(field, key);
			if (r || part_count == FLD_ID + 1)
				return r;
			mp_next(&field);
		} else {
			r = field_compare_with_key<TYPE>(field, key);
			if (r || part_count == FLD_ID + 1)
				return r;
			field = tuple_field_old(format, tuple, IDX2);
		}
		mp_next(&key);
		return TupComp<FLD_ID + 1, IDX2, MORE_TYPES...>::
				compareWithKey(tuple, key, part_count,
					       key_def, format, field);
	}
};

template<int FLD_ID, int IDX, int TYPE>
struct TupComp<FLD_ID, IDX, TYPE> {
	inline static int compareWithKey(const struct tuple *,
					 const char *key,
					 uint32_t,
					 const struct key_def *,
					 const struct tuple_format *,
					 const char *field)
	{
		return field_compare_with_key<TYPE>(field, key);
	}
};

/**
 * header
 */
template<int FLD_ID, int IDX, int ...MORE_TYPES>
struct TupleComparer {
	static int compareWithKey(const struct tuple *tuple,
				  const char *key,
				  uint32_t part_count,
				  const struct key_def *key_def)
	{
		if (part_count == 0)
			return 0;
		struct tuple_format *format = tuple_format(tuple);
		/* Part count can be 0 in wildcard searches. */
		const char *field = tuple_field_old(format, tuple, IDX);
		return TupComp<FLD_ID, IDX, MORE_TYPES...>::
				compareWithKey(tuple, key, part_count,
					       key_def, format, field);
	}
};
template<int ...MORE_TYPES>
struct TupleComparer<0, 0, MORE_TYPES...> {
	static int compareWithKey(const struct tuple *tuple,
				  const char *key,
				  uint32_t part_count,
				  const struct key_def *key_def)
	{
		if (part_count == 0)
			return 0;
		struct tuple_format *format = tuple_format(tuple);
		/* Part count can be 0 in wildcard searches. */
		const char *field = tuple->data;
		mp_decode_array(&field);
		return TupComp<0, 0, MORE_TYPES...>::
				compareWithKey(tuple, key, part_count,
					       key_def, format, field);
	}
};

struct FuncParamsWK {
	tuple_cmp_wk_t f;
	uint32_t p[64];
};
#define COMPARER_WK(...) {TupleComparer<0, __VA_ARGS__>::compareWithKey, __VA_ARGS__},
const FuncParamsWK cmp_wk_arr[] = {
	COMPARER_WK(0, NUM   , 1, NUM   , 2, NUM   )
	COMPARER_WK(0, STRING, 1, NUM   , 2, NUM   )
	COMPARER_WK(0, NUM   , 1, STRING, 2, NUM   )
	COMPARER_WK(0, STRING, 1, STRING, 2, NUM   )
	COMPARER_WK(0, NUM   , 1, NUM   , 2, STRING)
	COMPARER_WK(0, STRING, 1, NUM   , 2, STRING)
	COMPARER_WK(0, NUM   , 1, STRING, 2, STRING)
	COMPARER_WK(0, STRING, 1, STRING, 2, STRING)

	COMPARER_WK(1, NUM   , 2, NUM   )
	COMPARER_WK(1, STRING, 2, NUM   )
	COMPARER_WK(1, NUM   , 2, STRING)
	COMPARER_WK(1, STRING, 2, STRING)
};

tuple_cmp_wk_t
tuple_gen_compare_with_key(const struct key_def *def) {
	for (uint32_t k = 0; k < sizeof(cmp_wk_arr) /
	     sizeof(cmp_wk_arr[0]); k++) {
		uint32_t i = 0;
		for (; i < def->part_count; i++)
			if (def->parts[i].fieldno != cmp_wk_arr[k].p[i * 2] ||
			    def->parts[i].type != cmp_wk_arr[k].p[i * 2 + 1])
				break;
		if (i == def->part_count)
			return cmp_wk_arr[k].f;
	}
	return tuple_compare_with_key;
}
