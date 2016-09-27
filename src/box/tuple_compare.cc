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
#include "tuple_compare.h"
#include "tuple.h"

/* {{{ tuple_compare */

/*
 * Compare two tuple fields.
 * Separate version exists since compare is a very
 * often used operation, so any performance speed up
 * in it can have dramatic impact on the overall
 * server performance.
 */
inline __attribute__((always_inline)) int
mp_compare_uint(const char **data_a, const char **data_b);

enum mp_class {
	MP_CLASS_NIL = 0,
	MP_CLASS_BOOL,
	MP_CLASS_NUMBER,
	MP_CLASS_STR,
	MP_CLASS_BIN,
	MP_CLASS_ARRAY,
	MP_CLASS_MAP
};

static enum mp_class mp_classes[] = {
	/* .MP_NIL     = */ MP_CLASS_NIL,
	/* .MP_UINT    = */ MP_CLASS_NUMBER,
	/* .MP_INT     = */ MP_CLASS_NUMBER,
	/* .MP_STR     = */ MP_CLASS_STR,
	/* .MP_BIN     = */ MP_CLASS_BIN,
	/* .MP_ARRAY   = */ MP_CLASS_ARRAY,
	/* .MP_MAP     = */ MP_CLASS_MAP,
	/* .MP_BOOL    = */ MP_CLASS_BOOL,
	/* .MP_FLOAT   = */ MP_CLASS_NUMBER,
	/* .MP_DOUBLE  = */ MP_CLASS_NUMBER,
	/* .MP_BIN     = */ MP_CLASS_BIN
};

#define COMPARE_RESULT(a, b) (a < b ? -1 : a > b)

static enum mp_class
mp_classof(enum mp_type type)
{
	return mp_classes[type];
}

static inline double
mp_decode_number(const char **data)
{
	double val;
	switch (mp_typeof(**data)) {
	case MP_UINT:
		val = mp_decode_uint(data);
		break;
	case MP_INT:
		val = mp_decode_int(data);
		break;
	case MP_FLOAT:
		val = mp_decode_float(data);
		break;
	case MP_DOUBLE:
		val = mp_decode_double(data);
		break;
	default:
		unreachable();
	}
	return val;
}

static int
mp_compare_bool(const char *field_a, const char *field_b)
{
	int a_val = mp_decode_bool(&field_a);
	int b_val = mp_decode_bool(&field_b);
	return COMPARE_RESULT(a_val, b_val);
}

static int
mp_compare_integer(const char *field_a, const char *field_b)
{
	enum mp_type a_type = mp_typeof(*field_a);
	enum mp_type b_type = mp_typeof(*field_b);
	assert(mp_classof(a_type) == MP_CLASS_NUMBER);
	assert(mp_classof(b_type) == MP_CLASS_NUMBER);
	if (a_type == MP_UINT) {
		uint64_t a_val = mp_decode_uint(&field_a);
		if (b_type == MP_UINT) {
			uint64_t b_val = mp_decode_uint(&field_b);
			return COMPARE_RESULT(a_val, b_val);
		} else {
			int64_t b_val = mp_decode_int(&field_b);
			if (b_val < 0)
				return 1;
			return COMPARE_RESULT(a_val, (uint64_t)b_val);
		}
	} else {
		int64_t a_val = mp_decode_int(&field_a);
		if (b_type == MP_UINT) {
			uint64_t b_val = mp_decode_uint(&field_b);
			if (a_val < 0)
				return -1;
			return COMPARE_RESULT((uint64_t)a_val, b_val);
		} else {
			int64_t b_val = mp_decode_int(&field_b);
			return COMPARE_RESULT(a_val, b_val);
		}
	}
}

static int
mp_compare_number(const char *field_a, const char *field_b)
{
	enum mp_type a_type = mp_typeof(*field_a);
	enum mp_type b_type = mp_typeof(*field_b);
	assert(mp_classof(a_type) == MP_CLASS_NUMBER);
	assert(mp_classof(b_type) == MP_CLASS_NUMBER);
	if (a_type == MP_FLOAT || a_type == MP_DOUBLE ||
	    b_type == MP_FLOAT || b_type == MP_DOUBLE) {
		double a_val = mp_decode_number(&field_a);
		double b_val = mp_decode_number(&field_b);
		return COMPARE_RESULT(a_val, b_val);
	}
	return mp_compare_integer(field_a, field_b);
}

static inline int
mp_compare_str(const char *field_a, const char *field_b)
{
	uint32_t size_a = mp_decode_strl(&field_a);
	uint32_t size_b = mp_decode_strl(&field_b);
	int r = memcmp(field_a, field_b, MIN(size_a, size_b));
	if (r != 0)
		return r;
	return COMPARE_RESULT(size_a, size_b);
}

static inline int
mp_compare_bin(const char *field_a, const char *field_b)
{
	uint32_t size_a = mp_decode_binl(&field_a);
	uint32_t size_b = mp_decode_binl(&field_b);
	int r = memcmp(field_a, field_b, MIN(size_a, size_b));
	if (r != 0)
		return r;
	return COMPARE_RESULT(size_a, size_b);
}

typedef int (*mp_compare_f)(const char *, const char *);
static mp_compare_f mp_class_comparators[] = {
	/* .MP_CLASS_NIL    = */ NULL,
	/* .MP_CLASS_BOOL   = */ mp_compare_bool,
	/* .MP_CLASS_NUMBER = */ mp_compare_number,
	/* .MP_CLASS_STR    = */ mp_compare_str,
	/* .MP_CLASS_BIN    = */ mp_compare_bin,
	/* .MP_CLASS_ARRAY  = */ NULL,
	/* .MP_CLASS_MAP    = */ NULL,
};

static int
mp_compare_scalar(const char *field_a, const char *field_b)
{
	enum mp_type a_type = mp_typeof(*field_a);
	enum mp_type b_type = mp_typeof(*field_b);
	enum mp_class a_class = mp_classof(a_type);
	enum mp_class b_class = mp_classof(b_type);
	if (a_class != b_class)
		return COMPARE_RESULT(a_class, b_class);
	mp_compare_f cmp = mp_class_comparators[a_class];
	assert(cmp != NULL);
	return cmp(field_a, field_b);
}

int
tuple_compare_field(const char *field_a, const char *field_b,
		    enum field_type type)
{
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		return mp_compare_uint(field_a, field_b);
	case FIELD_TYPE_STRING:
		return mp_compare_str(field_a, field_b);
	case FIELD_TYPE_INTEGER:
		return mp_compare_integer(field_a, field_b);
	case FIELD_TYPE_NUMBER:
		return mp_compare_number(field_a, field_b);
	case FIELD_TYPE_SCALAR:
		return mp_compare_scalar(field_a, field_b);
	default:
		unreachable();
		return 0;
	}
}

int
tuple_compare_default(const struct tuple *tuple_a, const struct tuple *tuple_b,
	      const struct key_def *key_def)
{
	if (key_def->part_count == 1 && key_def->parts[0].fieldno == 0) {
		const char *a = tuple_a->data;
		const char *b = tuple_b->data;
		mp_decode_array(&a);
		mp_decode_array(&b);
		return tuple_compare_field(a, b, key_def->parts[0].type);
	}

	const struct key_part *part = key_def->parts;
	const struct key_part *end = part + key_def->part_count;
	struct tuple_format *format_a = tuple_format(tuple_a);
	struct tuple_format *format_b = tuple_format(tuple_b);
	const char *field_a;
	const char *field_b;
	int r = 0;

	for (; part < end; part++) {
		field_a = tuple_field_old(format_a, tuple_a, part->fieldno);
		field_b = tuple_field_old(format_b, tuple_b, part->fieldno);
		assert(field_a != NULL && field_b != NULL);
		if ((r = tuple_compare_field(field_a, field_b, part->type)))
			break;
	}
	return r;
}

int
tuple_compare_with_key_default(const struct tuple *tuple, const char *key,
			       uint32_t part_count, const struct key_def *key_def)
{
	assert(key != NULL || part_count == 0);
	assert(part_count <= key_def->part_count);
	struct tuple_format *format = tuple_format(tuple);
	if (likely(part_count == 1)) {
		const struct key_part *part = key_def->parts;
		const char *field = tuple_field_old(format, tuple,
						    part->fieldno);
		return tuple_compare_field(field, key, part->type);
	}

	const struct key_part *part = key_def->parts;
	const struct key_part *end = part + MIN(part_count, key_def->part_count);
	int r = 0; /* Part count can be 0 in wildcard searches. */
	for (; part < end; part++) {
		const char *field = tuple_field_old(format, tuple,
						    part->fieldno);
		r = tuple_compare_field(field, key, part->type);
		if (r != 0)
			break;
		mp_next(&key);
	}
	return r;
}

template <int TYPE>
static inline int
field_compare(const char **field_a, const char **field_b);

template <>
inline int
field_compare<FIELD_TYPE_UNSIGNED>(const char **field_a, const char **field_b)
{
	return mp_compare_uint(*field_a, *field_b);
}

template <>
inline int
field_compare<FIELD_TYPE_STRING>(const char **field_a, const char **field_b)
{
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
field_compare_and_next<FIELD_TYPE_UNSIGNED>(const char **field_a,
					    const char **field_b)
{
	int r = mp_compare_uint(*field_a, *field_b);
	mp_next(field_a);
	mp_next(field_b);
	return r;
}

template <>
inline int
field_compare_and_next<FIELD_TYPE_STRING>(const char **field_a,
					  const char **field_b)
{
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

/* Tuple comparator */
namespace /* local symbols */ {

template <int IDX, int TYPE, int ...MORE_TYPES> struct FieldCompare { };

/**
 * Common case.
 */
template <int IDX, int TYPE, int IDX2, int TYPE2, int ...MORE_TYPES>
struct FieldCompare<IDX, TYPE, IDX2, TYPE2, MORE_TYPES...>
{
	inline static int compare(const struct tuple *tuple_a,
				  const struct tuple *tuple_b,
				  const struct tuple_format *format_a,
				  const struct tuple_format *format_b,
				  const char *field_a,
				  const char *field_b)
	{
		int r;
		/* static if */
		if (IDX + 1 == IDX2) {
			if ((r = field_compare_and_next<TYPE>(&field_a,
							      &field_b)) != 0)
				return r;
		} else {
			if ((r = field_compare<TYPE>(&field_a, &field_b)) != 0)
				return r;
			field_a = tuple_field_old(format_a, tuple_a, IDX2);
			field_b = tuple_field_old(format_b, tuple_b, IDX2);
		}
		return FieldCompare<IDX2, TYPE2, MORE_TYPES...>::
			compare(tuple_a, tuple_b, format_a,
				format_b, field_a, field_b);
	}
};

template <int IDX, int TYPE>
struct FieldCompare<IDX, TYPE>
{
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
template <int IDX, int TYPE, int ...MORE_TYPES>
struct TupleCompare
{
	static int compare(const struct tuple *tuple_a,
			   const struct tuple *tuple_b,
			   const struct key_def *)
	{
		struct tuple_format *format_a = tuple_format(tuple_a);
		struct tuple_format *format_b = tuple_format(tuple_b);
		const char *field_a = tuple_field_old(format_a, tuple_a, IDX);
		const char *field_b = tuple_field_old(format_b, tuple_b, IDX);
		return FieldCompare<IDX, TYPE, MORE_TYPES...>::
			compare(tuple_a, tuple_b, format_a,
				format_b, field_a, field_b);
	}
};

template <int TYPE, int ...MORE_TYPES>
struct TupleCompare<0, TYPE, MORE_TYPES...> {
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
		return FieldCompare<0, TYPE, MORE_TYPES...>::compare(tuple_a, tuple_b,
					format_a, format_b, field_a, field_b);
	}
};
} /* end of anonymous namespace */

struct comparator_signature {
	tuple_compare_t f;
	uint32_t p[64];
};
#define COMPARATOR(...) \
	{ TupleCompare<__VA_ARGS__>::compare, { __VA_ARGS__, UINT32_MAX } },

/**
 * field1 no, field1 type, field2 no, field2 type, ...
 */
static const comparator_signature cmp_arr[] = {
	COMPARATOR(0, FIELD_TYPE_UNSIGNED)
	COMPARATOR(0, FIELD_TYPE_STRING)
	COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_UNSIGNED)
	COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_UNSIGNED)
	COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_STRING)
	COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_STRING)
	COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_UNSIGNED)
	COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_UNSIGNED)
	COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_UNSIGNED)
	COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_UNSIGNED)
	COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_STRING)
	COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_STRING)
	COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_STRING)
	COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_STRING)
};

#undef COMPARATOR

tuple_compare_t
tuple_compare_create(const struct key_def *def) {
	for (uint32_t k = 0; k < sizeof(cmp_arr) / sizeof(cmp_arr[0]); k++) {
		uint32_t i = 0;
		for (; i < def->part_count; i++)
			if (def->parts[i].fieldno != cmp_arr[k].p[i * 2] ||
			    def->parts[i].type != cmp_arr[k].p[i * 2 + 1])
				break;
		if (i == def->part_count && cmp_arr[k].p[i * 2] == UINT32_MAX)
			return cmp_arr[k].f;
	}
	return tuple_compare_default;
}

/* }}} tuple_compare */

/* {{{ tuple_compare_with_key */

template <int TYPE>
static inline int field_compare_with_key(const char **field, const char **key);

template <>
inline int
field_compare_with_key<FIELD_TYPE_UNSIGNED>(const char **field, const char **key)
{
	return mp_compare_uint(*field, *key);
}

template <>
inline int
field_compare_with_key<FIELD_TYPE_STRING>(const char **field, const char **key)
{
	uint32_t size_a, size_b;
	size_a = mp_decode_strl(field);
	size_b = mp_decode_strl(key);
	int r = memcmp(*field, *key, MIN(size_a, size_b));
	if (r == 0)
		r = size_a < size_b ? -1 : size_a > size_b;
	return r;
}

template <int TYPE>
static inline int
field_compare_with_key_and_next(const char **field_a, const char **field_b);

template <>
inline int
field_compare_with_key_and_next<FIELD_TYPE_UNSIGNED>(const char **field_a,
						     const char **field_b)
{
	int r = mp_compare_uint(*field_a, *field_b);
	mp_next(field_a);
	mp_next(field_b);
	return r;
}

template <>
inline int
field_compare_with_key_and_next<FIELD_TYPE_STRING>(const char **field_a,
					const char **field_b)
{
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

/* Tuple with key comparator */
namespace /* local symbols */ {

template <int FLD_ID, int IDX, int TYPE, int ...MORE_TYPES>
struct FieldCompareWithKey {};
/**
 * common
 */
template <int FLD_ID, int IDX, int TYPE, int IDX2, int TYPE2, int ...MORE_TYPES>
struct FieldCompareWithKey<FLD_ID, IDX, TYPE, IDX2, TYPE2, MORE_TYPES...>
{
	inline static int
	compare(const struct tuple *tuple, const char *key,
		uint32_t part_count, const struct key_def *key_def,
		const struct tuple_format *format, const char *field)
	{
		int r;
		/* static if */
		if (IDX + 1 == IDX2) {
			r = field_compare_with_key_and_next<TYPE>(&field, &key);
			if (r || part_count == FLD_ID + 1)
				return r;
		} else {
			r = field_compare_with_key<TYPE>(&field, &key);
			if (r || part_count == FLD_ID + 1)
				return r;
			field = tuple_field_old(format, tuple, IDX2);
			mp_next(&key);
		}
		return FieldCompareWithKey<FLD_ID + 1, IDX2, TYPE2, MORE_TYPES...>::
				compare(tuple, key, part_count,
					       key_def, format, field);
	}
};

template <int FLD_ID, int IDX, int TYPE>
struct FieldCompareWithKey<FLD_ID, IDX, TYPE> {
	inline static int compare(const struct tuple *,
					 const char *key,
					 uint32_t,
					 const struct key_def *,
					 const struct tuple_format *,
					 const char *field)
	{
		return field_compare_with_key<TYPE>(&field, &key);
	}
};

/**
 * header
 */
template <int FLD_ID, int IDX, int TYPE, int ...MORE_TYPES>
struct TupleCompareWithKey
{
	static int
	compare(const struct tuple *tuple, const char *key,
		uint32_t part_count, const struct key_def *key_def)
	{
		/* Part count can be 0 in wildcard searches. */
		if (part_count == 0)
			return 0;
		struct tuple_format *format = tuple_format(tuple);
		const char *field = tuple_field_old(format, tuple, IDX);
		return FieldCompareWithKey<FLD_ID, IDX, TYPE, MORE_TYPES...>::
				compare(tuple, key, part_count,
					key_def, format, field);
	}
};

template <int TYPE, int ...MORE_TYPES>
struct TupleCompareWithKey<0, 0, TYPE, MORE_TYPES...>
{
	static int compare(const struct tuple *tuple,
				  const char *key,
				  uint32_t part_count,
				  const struct key_def *key_def)
	{
		/* Part count can be 0 in wildcard searches. */
		if (part_count == 0)
			return 0;
		struct tuple_format *format = tuple_format(tuple);
		const char *field = tuple->data;
		mp_decode_array(&field);
		return FieldCompareWithKey<0, 0, TYPE, MORE_TYPES...>::
			compare(tuple, key, part_count,
				key_def, format, field);
	}
};

} /* end of anonymous namespace */

struct comparator_with_key_signature
{
	tuple_compare_with_key_t f;
	uint32_t p[64];
};

#define KEY_COMPARATOR(...) \
	{ TupleCompareWithKey<0, __VA_ARGS__>::compare, { __VA_ARGS__ } },

static const comparator_with_key_signature cmp_wk_arr[] = {
	KEY_COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_UNSIGNED)
	KEY_COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_UNSIGNED)
	KEY_COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_UNSIGNED)
	KEY_COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_UNSIGNED)
	KEY_COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_STRING)
	KEY_COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_STRING)
	KEY_COMPARATOR(0, FIELD_TYPE_UNSIGNED, 1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_STRING)
	KEY_COMPARATOR(0, FIELD_TYPE_STRING  , 1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_STRING)

	KEY_COMPARATOR(1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_UNSIGNED)
	KEY_COMPARATOR(1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_UNSIGNED)
	KEY_COMPARATOR(1, FIELD_TYPE_UNSIGNED, 2, FIELD_TYPE_STRING)
	KEY_COMPARATOR(1, FIELD_TYPE_STRING  , 2, FIELD_TYPE_STRING)
};

#undef KEY_COMPARATOR

tuple_compare_with_key_t
tuple_compare_with_key_create(const struct key_def *def)
{
	for (uint32_t k = 0;
	     k < sizeof(cmp_wk_arr) / sizeof(cmp_wk_arr[0]);
	     k++) {

		uint32_t i = 0;
		for (; i < def->part_count; i++) {

			if (def->parts[i].fieldno != cmp_wk_arr[k].p[i * 2] ||
			    def->parts[i].type != cmp_wk_arr[k].p[i * 2 + 1]) {

				break;
			}
		}
		if (i == def->part_count)
			return cmp_wk_arr[k].f;
	}
	return tuple_compare_with_key_default;
}

/* }}} tuple_compare_with_key */
