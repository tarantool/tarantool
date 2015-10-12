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
#include "tuple_compare_gen.h"
#include "tuple.h"


template <int TYPE>
static inline int
field_compare(const char **field_a, const char **field_b);

template <>
inline int
field_compare<NUM>(const char **field_a, const char **field_b) {
	return mp_compare_uint(*field_a, *field_b);
}

template <>
inline int
field_compare<STRING>(const char **field_a, const char **field_b) {
	uint32_t size_a, size_b;
	size_a = mp_decode_strl(field_a);
	size_b = mp_decode_strl(field_b);
	int r = memcmp(*field_a, *field_b, MIN(size_a, size_b));
	if (r == 0)
		r = size_a < size_b ? -1 : size_a > size_b;
	return r;
}

template <int TYPE>
static inline int
field_compare_and_next(const char **field_a, const char **field_b);

template <>
inline int
field_compare_and_next<NUM>(const char **field_a, const char **field_b) {
	int r = mp_compare_uint(*field_a, *field_b);
	mp_next(field_a);
	mp_next(field_b);
	return r;
}

template <>
inline int
field_compare_and_next<STRING>(const char **field_a, const char **field_b) {
	uint32_t size_a, size_b;
	size_a = mp_decode_strl(field_a);
	size_b = mp_decode_strl(field_b);
	int r = memcmp(*field_a, *field_b, MIN(size_a, size_b));
	if (r == 0)
		r = size_a < size_b ? -1 : size_a > size_b;
	*field_a += size_a;
	*field_b += size_b;
	return r;
}

/* Tuple comparer */
namespace /* local symbols */ {

template<int IDX, int TYPE, int ...MORE_TYPES>
struct ComparerPrimitive { };
/**
 * common
 */
template<int IDX, int TYPE, int IDX2, int ...MORE_TYPES>
struct ComparerPrimitive<IDX, TYPE, IDX2, MORE_TYPES...> {
	inline static int compare(const struct tuple *tuple_a,
				  const struct tuple *tuple_b,
				  const struct tuple_format *format_a,
				  const struct tuple_format *format_b,
				  const char *field_a,
				  const char *field_b)
	{
		int r;
		/* static if */
		if(IDX + 1 == IDX2) {
			if ((r = field_compare_and_next<TYPE>(&field_a, &field_b)) != 0)
				return r;
		} else {
			if ((r = field_compare<TYPE>(&field_a, &field_b)) != 0)
				return r;
			field_a = tuple_field_old(format_a, tuple_a, IDX2);
			field_b = tuple_field_old(format_b, tuple_b, IDX2);
		}
		return ComparerPrimitive<IDX2, MORE_TYPES...>::compare(tuple_a, tuple_b,
					format_a, format_b,field_a, field_b);
	}
};

template<int IDX, int TYPE>
struct ComparerPrimitive<IDX, TYPE> {
	inline static int compare(const struct tuple *,
				  const struct tuple *,
				  const struct tuple_format *,
				  const struct tuple_format *,
				  const char *field_a,
				  const char *field_b)
	{
		return field_compare<TYPE>(&field_a, &field_b);
	}
};

/**
 * header
 */
template<int IDX, int ...MORE_TYPES>
struct TupleComparer {
	static int compare(const struct tuple *tuple_a,
			   const struct tuple *tuple_b,
			   const struct key_def *)
	{
		struct tuple_format *format_a = tuple_format(tuple_a);
		struct tuple_format *format_b = tuple_format(tuple_b);
		const char *field_a = tuple_field_old(format_a, tuple_a, IDX);
		const char *field_b = tuple_field_old(format_b, tuple_b, IDX);
		return ComparerPrimitive<IDX, MORE_TYPES...>::compare(tuple_a, tuple_b,
					format_a, format_b,field_a, field_b);
	}
};
template<int ...MORE_TYPES>
struct TupleComparer<0, MORE_TYPES...> {
	static int compare(const struct tuple *tuple_a,
			   const struct tuple *tuple_b,
			   const struct key_def *)
	{
		struct tuple_format *format_a = tuple_format(tuple_a);
		struct tuple_format *format_b = tuple_format(tuple_b);
		const char *field_a = tuple_a->data;
		const char *field_b = tuple_b->data;
		mp_decode_array(&field_a);
		mp_decode_array(&field_b);
		return ComparerPrimitive<0, MORE_TYPES...>::compare(tuple_a, tuple_b,
					format_a, format_b,field_a, field_b);
	}
};
} /* namespace */

struct function_description {
	tuple_cmp_t f;
	uint32_t p[64];
};
#define COMPARER(...) {TupleComparer<__VA_ARGS__>::compare, __VA_ARGS__, UINT32_MAX},
static const function_description cmp_arr[] = {
	COMPARER(0, NUM   )
	COMPARER(0, STRING)
	COMPARER(0, NUM   , 1, NUM   )
	COMPARER(0, STRING, 1, NUM   )
	COMPARER(0, NUM   , 1, STRING)
	COMPARER(0, STRING, 1, STRING)
	COMPARER(0, NUM   , 1, NUM   , 2, NUM   )
	COMPARER(0, STRING, 1, NUM   , 2, NUM   )
	COMPARER(0, NUM   , 1, STRING, 2, NUM   )
	COMPARER(0, STRING, 1, STRING, 2, NUM   )
	COMPARER(0, NUM   , 1, NUM   , 2, STRING)
	COMPARER(0, STRING, 1, NUM   , 2, STRING)
	COMPARER(0, NUM   , 1, STRING, 2, STRING)
	COMPARER(0, STRING, 1, STRING, 2, STRING)
};

tuple_cmp_t
tuple_compare_gen(const struct key_def *def) {
	for (uint32_t k = 0; k < sizeof(cmp_arr) / sizeof(cmp_arr[0]); k++) {
		uint32_t i = 0;
		for (; i < def->part_count; i++)
			if (def->parts[i].fieldno != cmp_arr[k].p[i * 2] ||
			    def->parts[i].type != cmp_arr[k].p[i * 2 + 1])
				break;
		if (i == def->part_count && cmp_arr[k].p[i * 2] == UINT32_MAX)
			return cmp_arr[k].f;
	}
	return tuple_compare;
}
