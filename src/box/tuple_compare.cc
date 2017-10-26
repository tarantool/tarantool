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
#include "trivia/util.h" /* NOINLINE */
#include <math.h>
#include "coll_def.h"

/* {{{ tuple_compare */

/*
 * Compare two tuple fields.
 * Separate version exists since compare is a very
 * often used operation, so any performance speed up
 * in it can have dramatic impact on the overall
 * performance.
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

static int
mp_compare_bool(const char *field_a, const char *field_b)
{
	int a_val = mp_decode_bool(&field_a);
	int b_val = mp_decode_bool(&field_b);
	return COMPARE_RESULT(a_val, b_val);
}

static int
mp_compare_integer_with_hint(const char *field_a, enum mp_type a_type,
			     const char *field_b, enum mp_type b_type)
{
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

#define EXP2_53 9007199254740992.0      /* 2.0 ^ 53 */
#define EXP2_64 1.8446744073709552e+19  /* 2.0 ^ 64 */

/*
 * Compare LHS with RHS, return a value <0, 0 or >0 depending on the
 * comparison result (strcmp-style).
 * Normally, K==1. If K==-1, the result is inverted (as if LHS and RHS
 * were swapped).
 * K is needed to enable tail call optimization in Release build.
 * NOINLINE attribute was added to avoid aggressive inlining which
 * resulted in over 2Kb code size for mp_compare_number.
 */
NOINLINE static int
mp_compare_double_uint64(double lhs, uint64_t rhs, int k)
{
	assert(k==1 || k==-1);
	/*
	 * IEEE double represents 2^N precisely.
	 * The value below is 2^53.  If a double exceeds this threshold,
	 * there's no fractional part. Moreover, the "next" float is
	 * 2^53+2, i.e. there's not enough precision to encode even some
	 * "odd" integers.
	 * Note: ">=" is important, see next block.
	 */
	if (lhs >= EXP2_53) {
		/*
		 * The value below is 2^64.
		 * Note: UINT64_MAX is 2^64-1, hence ">="
		 */
		if (lhs >= EXP2_64)
			return k;
		/* Within [2^53, 2^64) double->uint64_t is lossless. */
		assert((double)(uint64_t)lhs == lhs);
		return k*COMPARE_RESULT((uint64_t)lhs, rhs);
	}
	/*
	 * According to the IEEE 754 the double format is the
	 * following:
	 * +------+----------+----------+
	 * | sign | exponent | fraction |
	 * +------+----------+----------+
	 *  1 bit    11 bits    52 bits
	 * If the exponent is 0x7FF, the value is a special one.
	 * Special value can be NaN, +inf and -inf.
	 * If the fraction == 0, the value is inf. Sign depends on
	 * the sign bit.
	 * If the first bit of the fraction is 1, the value is the
	 * quiet NaN, else the signaling NaN.
	 */
	if (!isnan(lhs)) {
		/*
		 * lhs is a number or inf.
		 * If RHS < 2^53, uint64_t->double is lossless.
		 * Otherwize the value may get rounded.	 It's
		 * unspecified whether it gets rounded up or down,
		 * i.e. the conversion may yield 2^53 for a
		 * RHS > 2^53. Since we've aready ensured that
		 * LHS < 2^53, the result is still correct even if
		 * rounding happens.
		 */
		assert(lhs < EXP2_53);
		assert((uint64_t)(double)rhs == rhs || rhs > (uint64_t)EXP2_53);
		return k*COMPARE_RESULT(lhs, (double)rhs);
	}
	/*
	 * Lhs is NaN. We assume all NaNs to be less than any
	 * number.
	 */
	return -k;
}

static int
mp_compare_double_any_int(double lhs, const char *rhs, enum mp_type rhs_type,
			  int k)
{
	if (rhs_type == MP_INT) {
		int64_t v = mp_decode_int(&rhs);
		if (v < 0) {
			return mp_compare_double_uint64(-lhs, (uint64_t)-v,
							-k);
		}
		return mp_compare_double_uint64(lhs, (uint64_t)v, k);
	}
	assert(rhs_type == MP_UINT);
	return mp_compare_double_uint64(lhs, mp_decode_uint(&rhs), k);
}

static int
mp_compare_double_any_number(double lhs, const char *rhs,
			     enum mp_type rhs_type, int k)
{
	double v;
	if (rhs_type == MP_FLOAT)
		v = mp_decode_float(&rhs);
	else if (rhs_type == MP_DOUBLE)
		v = mp_decode_double(&rhs);
	else
		return mp_compare_double_any_int(lhs, rhs, rhs_type, k);
	int lhs_is_nan = isnan(lhs);
	int rhs_is_nan = isnan(v);
	assert(lhs_is_nan == 1 || lhs_is_nan == 0);
	assert(rhs_is_nan == 1 || rhs_is_nan == 0);
	if (lhs_is_nan == 0 && rhs_is_nan == 0) {
		return k * COMPARE_RESULT(lhs, v);
	} else if (lhs_is_nan != rhs_is_nan) {
		/*
		 *   lhs  | lhs_isNaN |  rhs   | rhs_isNaN | ret
		 * -------+-----------+--------+-----------+-----
		 *   NaN  |     1     | number |     0     |  -1
		 * number |     0     |  NaN   |     1     |   1
		 */
		return k * (rhs_is_nan - lhs_is_nan);
	}
	/*
	 * Both NaNs. Compare signaling and quiet NaNs by
	 * 'quiet bit'.
	 */
	uint64_t lqbit = *((uint64_t *)&lhs) & (uint64_t)0x8000000000000;
	uint64_t rqbit = *((uint64_t *)&v) & (uint64_t)0x8000000000000;
	/*
	 * Lets consider the quiet NaN (fraction first bit == 1)
	 * to be bigger than signaling NaN (fraction first
	 * bit == 0).
	 */
	return k * COMPARE_RESULT(lqbit, rqbit);
}

static int
mp_compare_number_with_hint(const char *lhs, enum mp_type lhs_type,
			    const char *rhs, enum mp_type rhs_type)
{
	assert(mp_classof(lhs_type) == MP_CLASS_NUMBER);
	assert(mp_classof(rhs_type) == MP_CLASS_NUMBER);

	if (rhs_type == MP_FLOAT) {
		return mp_compare_double_any_number(
			mp_decode_float(&rhs), lhs, lhs_type, -1
		);
	}
	if (rhs_type == MP_DOUBLE) {
		return mp_compare_double_any_number(
			mp_decode_double(&rhs), lhs, lhs_type, -1
		);
	}
	assert(rhs_type == MP_INT || rhs_type == MP_UINT);
	if (lhs_type == MP_FLOAT) {
		return mp_compare_double_any_int(
			mp_decode_float(&lhs), rhs, rhs_type, 1
		);
	}
	if (lhs_type == MP_DOUBLE) {
		return mp_compare_double_any_int(
			mp_decode_double(&lhs), rhs, rhs_type, 1
		);
	}
	assert(lhs_type == MP_INT || lhs_type == MP_UINT);
	return mp_compare_integer_with_hint(lhs, lhs_type, rhs, rhs_type);
}

static inline int
mp_compare_number(const char *lhs, const char *rhs)
{
	return mp_compare_number_with_hint(lhs, mp_typeof(*lhs),
					   rhs, mp_typeof(*rhs));
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
mp_compare_str_coll(const char *field_a, const char *field_b,
		    struct coll *coll)
{
	uint32_t size_a = mp_decode_strl(&field_a);
	uint32_t size_b = mp_decode_strl(&field_b);
	return coll->cmp(field_a, size_a, field_b, size_b, coll);
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
mp_compare_scalar_with_hint(const char *field_a, enum mp_type a_type,
			    const char *field_b, enum mp_type b_type)
{
	enum mp_class a_class = mp_classof(a_type);
	enum mp_class b_class = mp_classof(b_type);
	if (a_class != b_class)
		return COMPARE_RESULT(a_class, b_class);
	mp_compare_f cmp = mp_class_comparators[a_class];
	assert(cmp != NULL);
	return cmp(field_a, field_b);
}

static inline int
mp_compare_scalar(const char *field_a, const char *field_b)
{
	return mp_compare_scalar_with_hint(field_a, mp_typeof(*field_a),
					   field_b, mp_typeof(*field_b));
}

static inline int
mp_compare_scalar_coll(const char *field_a, const char *field_b,
		       struct coll *coll)
{
	enum mp_type type_a = mp_typeof(*field_a);
	enum mp_type type_b = mp_typeof(*field_b);
	if (type_a == MP_STR && type_b == MP_STR)
		return mp_compare_str_coll(field_a, field_b, coll);
	return mp_compare_scalar_with_hint(field_a, type_a, field_b, type_b);
}

/**
 * @brief Compare two fields parts using a type definition
 * @param field_a field
 * @param field_b field
 * @param field_type field type definition
 * @retval 0  if field_a == field_b
 * @retval <0 if field_a < field_b
 * @retval >0 if field_a > field_b
 */
static int
tuple_compare_field(const char *field_a, const char *field_b,
		    int8_t type, struct coll *coll)
{
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		return mp_compare_uint(field_a, field_b);
	case FIELD_TYPE_STRING:
		return coll != NULL ?
		       mp_compare_str_coll(field_a, field_b, coll) :
		       mp_compare_str(field_a, field_b);
	case FIELD_TYPE_INTEGER:
		return mp_compare_integer_with_hint(field_a,
						    mp_typeof(*field_a),
						    field_b,
						    mp_typeof(*field_b));
	case FIELD_TYPE_NUMBER:
		return mp_compare_number(field_a, field_b);
	case FIELD_TYPE_BOOLEAN:
		return mp_compare_bool(field_a, field_b);
	case FIELD_TYPE_SCALAR:
		return coll != NULL ?
		       mp_compare_scalar_coll(field_a, field_b, coll) :
		       mp_compare_scalar(field_a, field_b);
	default:
		unreachable();
		return 0;
	}
}

static int
tuple_compare_field_with_hint(const char *field_a, enum mp_type a_type,
			      const char *field_b, enum mp_type b_type,
			      int8_t type, struct coll *coll)
{
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
		return mp_compare_uint(field_a, field_b);
	case FIELD_TYPE_STRING:
		return coll != NULL ?
		       mp_compare_str_coll(field_a, field_b, coll) :
		       mp_compare_str(field_a, field_b);
	case FIELD_TYPE_INTEGER:
		return mp_compare_integer_with_hint(field_a, a_type,
						    field_b, b_type);
	case FIELD_TYPE_NUMBER:
		return mp_compare_number_with_hint(field_a, a_type,
						   field_b, b_type);
	case FIELD_TYPE_SCALAR:
		return coll != NULL ?
		       mp_compare_scalar_coll(field_a, field_b, coll) :
		       mp_compare_scalar_with_hint(field_a, a_type,
						   field_b, b_type);
	default:
		unreachable();
		return 0;
	}
}

template<bool is_nullable>
static inline int
tuple_compare_slowpath(const struct tuple *tuple_a, const struct tuple *tuple_b,
		       const struct key_def *key_def)
{
	const struct key_part *part = key_def->parts;
	const char *tuple_a_raw = tuple_data(tuple_a);
	const char *tuple_b_raw = tuple_data(tuple_b);
	if (key_def->part_count == 1 && part->fieldno == 0) {
		mp_decode_array(&tuple_a_raw);
		mp_decode_array(&tuple_b_raw);
		if (! is_nullable) {
			return tuple_compare_field(tuple_a_raw, tuple_b_raw,
						   part->type, part->coll);
		}
		enum mp_type a_type = mp_typeof(*tuple_a_raw);
		enum mp_type b_type = mp_typeof(*tuple_b_raw);
		if (a_type == MP_NIL)
			return b_type == MP_NIL ? 0 : -1;
		else if (b_type == MP_NIL)
			return 1;
		return tuple_compare_field_with_hint(tuple_a_raw,  a_type,
						     tuple_b_raw, b_type,
						     part->type, part->coll);
	}

	bool was_null_met = false;
	const struct tuple_format *format_a = tuple_format(tuple_a);
	const struct tuple_format *format_b = tuple_format(tuple_b);
	const uint32_t *field_map_a = tuple_field_map(tuple_a);
	const uint32_t *field_map_b = tuple_field_map(tuple_b);
	const struct key_part *end;
	const char *field_a;
	const char *field_b;
	int rc;
	if (is_nullable)
		end = part + key_def->unique_part_count;
	else
		end = part + key_def->part_count;

	for (; part < end; part++) {
		field_a = tuple_field_raw(format_a, tuple_a_raw, field_map_a,
					  part->fieldno);
		field_b = tuple_field_raw(format_b, tuple_b_raw, field_map_b,
					  part->fieldno);
		assert(field_a != NULL && field_b != NULL);
		if (! is_nullable) {
			rc = tuple_compare_field(field_a, field_b, part->type,
						 part->coll);
			if (rc != 0)
				return rc;
			else
				continue;
		}
		enum mp_type a_type = mp_typeof(*field_a);
		enum mp_type b_type = mp_typeof(*field_b);
		if (a_type == MP_NIL) {
			if (b_type != MP_NIL)
				return -1;
			was_null_met = true;
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			rc = tuple_compare_field_with_hint(field_a, a_type,
							   field_b, b_type,
							   part->type,
							   part->coll);
			if (rc != 0)
				return rc;
		}
	}
	/*
	 * Do not use full parts set when no NULLs. It allows to
	 * simulate a NULL != NULL logic in secondary keys,
	 * because in them full parts set contains unique primary
	 * key.
	 */
	if (!is_nullable || !was_null_met)
		return 0;
	/*
	 * Inxex parts are equal and contain NULLs. So use
	 * extended parts only.
	 */
	end = key_def->parts + key_def->part_count;
	for (; part < end; ++part) {
		field_a = tuple_field_raw(format_a, tuple_a_raw, field_map_a,
					  part->fieldno);
		field_b = tuple_field_raw(format_b, tuple_b_raw, field_map_b,
					  part->fieldno);
		assert(field_a != NULL && field_b != NULL);
		rc = tuple_compare_field(field_a, field_b, part->type,
					 part->coll);
		if (rc != 0)
			return rc;
	}
	return 0;
}

template<bool is_nullable>
static inline int
tuple_compare_with_key_slowpath(const struct tuple *tuple, const char *key,
				uint32_t part_count,
				const struct key_def *key_def)
{
	assert(key != NULL || part_count == 0);
	assert(part_count <= key_def->part_count);
	const struct key_part *part = key_def->parts;
	const struct tuple_format *format = tuple_format(tuple);
	const char *tuple_raw = tuple_data(tuple);
	const uint32_t *field_map = tuple_field_map(tuple);
	if (likely(part_count == 1)) {
		const char *field;
		field = tuple_field_raw(format, tuple_raw, field_map,
					part->fieldno);
		if (! is_nullable) {
			return tuple_compare_field(field, key, part->type,
						   part->coll);
		}
		enum mp_type a_type = mp_typeof(*field);
		enum mp_type b_type = mp_typeof(*key);
		if (a_type == MP_NIL) {
			return b_type == MP_NIL ? 0 : -1;
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			return tuple_compare_field_with_hint(field, a_type, key,
							     b_type, part->type,
							     part->coll);
		}
	}

	const struct key_part *end = part + part_count;
	int rc;
	for (; part < end; ++part, mp_next(&key)) {
		const char *field;
		field = tuple_field_raw(format, tuple_raw, field_map,
					part->fieldno);
		if (! is_nullable) {
			int rc = tuple_compare_field(field, key, part->type,
						     part->coll);
			if (rc != 0)
				return rc;
			else
				continue;
		}
		enum mp_type a_type = mp_typeof(*field);
		enum mp_type b_type = mp_typeof(*key);
		if (a_type == MP_NIL) {
			if (b_type != MP_NIL)
				return -1;
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			rc = tuple_compare_field_with_hint(field, a_type, key,
							   b_type, part->type,
							   part->coll);
			if (rc != 0)
				return rc;
		}
	}
	return 0;
}

/**
 * is_sequential_nullable_tuples used to determine if this
 * function is used to compare two tuples, which keys are
 * sequential. In such a case it is necessary to compare them
 * using primary parts if a NULL was met.
 */
template<bool is_nullable, bool is_sequential_nullable_tuples>
static inline int
key_compare_parts(const char *key_a, const char *key_b, uint32_t part_count,
		  const struct key_def *key_def)
{
	assert(!is_sequential_nullable_tuples || is_nullable);
	assert((key_a != NULL && key_b != NULL) || part_count == 0);
	const struct key_part *part = key_def->parts;
	if (likely(part_count == 1)) {
		if (! is_nullable) {
			return tuple_compare_field(key_a, key_b, part->type,
						   part->coll);
		}
		enum mp_type a_type = mp_typeof(*key_a);
		enum mp_type b_type = mp_typeof(*key_b);
		if (a_type == MP_NIL) {
			return b_type == MP_NIL ? 0 : -1;
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			return tuple_compare_field_with_hint(key_a, a_type,
							     key_b, b_type,
							     part->type,
							     part->coll);
		}
	}

	bool was_null_met = false;
	const struct key_part *end = part + part_count;
	int rc;
	for (; part < end; ++part, mp_next(&key_a), mp_next(&key_b)) {
		if (! is_nullable) {
			rc = tuple_compare_field(key_a, key_b, part->type,
						 part->coll);
			if (rc != 0)
				return rc;
			else
				continue;
		}
		enum mp_type a_type = mp_typeof(*key_a);
		enum mp_type b_type = mp_typeof(*key_b);
		if (a_type == MP_NIL) {
			if (b_type != MP_NIL)
				return -1;
			was_null_met = true;
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			rc = tuple_compare_field_with_hint(key_a, a_type, key_b,
							   b_type, part->type,
							   part->coll);
			if (rc != 0)
				return rc;
		}
	}

	if (!is_sequential_nullable_tuples || !was_null_met)
		return 0;
	end = key_def->parts + key_def->unique_part_count;
	for (; part < end; ++part, mp_next(&key_a), mp_next(&key_b)) {
		rc = tuple_compare_field(key_a, key_b, part->type, part->coll);
		if (rc != 0)
			return rc;
	}
	return 0;
}

template<bool is_nullable>
static inline int
tuple_compare_with_key_sequential(const struct tuple *tuple,
	const char *key, uint32_t part_count, const struct key_def *key_def)
{
	assert(key_def_is_sequential(key_def));
	const char *tuple_key = tuple_data(tuple);
	uint32_t tuple_field_count = mp_decode_array(&tuple_key);
	assert(tuple_field_count >= key_def->part_count);
	assert(part_count <= key_def->part_count);
	(void) tuple_field_count;
	return key_compare_parts<is_nullable, false>(tuple_key, key, part_count,
						     key_def);
}

int
key_compare(const char *key_a, const char *key_b,
	    const struct key_def *key_def)
{
	uint32_t part_count_a = mp_decode_array(&key_a);
	uint32_t part_count_b = mp_decode_array(&key_b);
	assert(part_count_a <= key_def->part_count);
	assert(part_count_b <= key_def->part_count);
	uint32_t part_count = MIN(part_count_a, part_count_b);
	assert(part_count <= key_def->part_count);
	if (! key_def->is_nullable) {
		return key_compare_parts<false, false>(key_a, key_b, part_count,
						       key_def);
	} else {
		return key_compare_parts<true, false>(key_a, key_b, part_count,
						      key_def);
	}
}

static int
tuple_compare_sequential(const struct tuple *tuple_a,
			 const struct tuple *tuple_b,
			 const struct key_def *key_def)
{
	assert(key_def_is_sequential(key_def));
	const char *key_a = tuple_data(tuple_a);
	uint32_t field_count_a = mp_decode_array(&key_a);
	assert(field_count_a >= key_def->part_count);
	(void) field_count_a;
	const char *key_b = tuple_data(tuple_b);
	uint32_t field_count_b = mp_decode_array(&key_b);
	assert(field_count_b >= key_def->part_count);
	(void) field_count_b;
	return key_compare_parts<false, false>(key_a, key_b,
					       key_def->part_count, key_def);
}

static int
tuple_compare_sequential_nullable(const struct tuple *tuple_a,
				  const struct tuple *tuple_b,
				  const struct key_def *key_def)
{
	assert(key_def->is_nullable);
	assert(key_def_is_sequential(key_def));
	const char *key_a = tuple_data(tuple_a);
	uint32_t field_count = mp_decode_array(&key_a);
	assert(field_count >= key_def->part_count);
	const char *key_b = tuple_data(tuple_b);
	field_count = mp_decode_array(&key_b);
	assert(field_count >= key_def->part_count);
	(void) field_count;
	return key_compare_parts<true, true>(key_a, key_b,
					     key_def->unique_part_count,
					     key_def);
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
			field_a = tuple_field_raw(format_a, tuple_data(tuple_a),
						  tuple_field_map(tuple_a),
						  IDX2);
			field_b = tuple_field_raw(format_b, tuple_data(tuple_b),
						  tuple_field_map(tuple_b),
						  IDX2);
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
		const char *field_a, *field_b;
		field_a = tuple_field_raw(format_a, tuple_data(tuple_a),
					  tuple_field_map(tuple_a), IDX);
		field_b = tuple_field_raw(format_b, tuple_data(tuple_b),
					  tuple_field_map(tuple_b), IDX);
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
		const char *field_a = tuple_data(tuple_a);
		const char *field_b = tuple_data(tuple_b);
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
	if (def->is_nullable) {
		if (key_def_is_sequential(def))
			return tuple_compare_sequential_nullable;
		return tuple_compare_slowpath<true>;
	}
	if (!key_def_has_collation(def)) {
		/* Precalculated comparators don't use collation */
		for (uint32_t k = 0;
		     k < sizeof(cmp_arr) / sizeof(cmp_arr[0]); k++) {
			uint32_t i = 0;
			for (; i < def->part_count; i++)
				if (def->parts[i].fieldno !=
				    cmp_arr[k].p[i * 2] ||
				    def->parts[i].type !=
				    cmp_arr[k].p[i * 2 + 1])
					break;
			if (i == def->part_count &&
			    cmp_arr[k].p[i * 2] == UINT32_MAX)
				return cmp_arr[k].f;
		}
	}
	if (key_def_is_sequential(def))
		return tuple_compare_sequential;
	return tuple_compare_slowpath<false>;
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
			field = tuple_field_raw(format, tuple_data(tuple),
						tuple_field_map(tuple), IDX2);
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
		const char *field = tuple_field_raw(format, tuple_data(tuple),
						    tuple_field_map(tuple),
						    IDX);
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
		const char *field = tuple_data(tuple);
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
	if (def->is_nullable) {
		if (key_def_is_sequential(def))
			return tuple_compare_with_key_sequential<true>;
		return tuple_compare_with_key_slowpath<true>;
	}
	if (!key_def_has_collation(def)) {
		/* Precalculated comparators don't use collation */
		for (uint32_t k = 0;
		     k < sizeof(cmp_wk_arr) / sizeof(cmp_wk_arr[0]);
		     k++) {

			uint32_t i = 0;
			for (; i < def->part_count; i++) {
				if (def->parts[i].fieldno !=
				    cmp_wk_arr[k].p[i * 2] ||
				    def->parts[i].type !=
				    cmp_wk_arr[k].p[i * 2 + 1]) {
					break;
				}
			}
			if (i == def->part_count)
				return cmp_wk_arr[k].f;
		}
	}
	if (key_def_is_sequential(def))
		return tuple_compare_with_key_sequential<false>;
	return tuple_compare_with_key_slowpath<false>;
}

/* }}} tuple_compare_with_key */

int
box_tuple_compare(const box_tuple_t *tuple_a, const box_tuple_t *tuple_b,
		  const box_key_def_t *key_def)
{
	return tuple_compare(tuple_a, tuple_b, key_def);
}

int
box_tuple_compare_with_key(const box_tuple_t *tuple_a, const char *key_b,
			   const box_key_def_t *key_def)
{
	uint32_t part_count = mp_decode_array(&key_b);
	return tuple_compare_with_key(tuple_a, key_b, part_count, key_def);

}
