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
#include "core/decimal.h"
#include "tuple_compare.h"
#include "tuple.h"
#include "coll/coll.h"
#include "trivia/util.h" /* NOINLINE */
#include <math.h>
#include "mp_decimal.h"
#include "mp_extension_types.h"
#include "mp_uuid.h"
#include "mp_datetime.h"

/* {{{ tuple_compare */

/*
 * Compare two tuple fields.
 * Separate version exists since compare is a very
 * often used operation, so any performance speed up
 * in it can have dramatic impact on the overall
 * performance.
 */
ALWAYS_INLINE int
mp_compare_uint(const char **data_a, const char **data_b);

enum mp_class {
	MP_CLASS_NIL = 0,
	MP_CLASS_BOOL,
	MP_CLASS_NUMBER,
	MP_CLASS_STR,
	MP_CLASS_BIN,
	MP_CLASS_UUID,
	MP_CLASS_DATETIME,
	MP_CLASS_INTERVAL,
	MP_CLASS_ARRAY,
	MP_CLASS_MAP,
	mp_class_max,
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
	/* .MP_EXT     = */ mp_class_max,
};

static enum mp_class mp_ext_classes[] = {
	/* .MP_UNKNOWN_EXTENSION = */ mp_class_max, /* unsupported */
	/* .MP_DECIMAL		 = */ MP_CLASS_NUMBER,
	/* .MP_UUID		 = */ MP_CLASS_UUID,
	/* .MP_ERROR		 = */ mp_class_max,
	/* .MP_DATETIME		 = */ MP_CLASS_DATETIME,
	/* .MP_COMPRESSION	 = */ mp_class_max,
	/* .MP_INTERVAL		 = */ MP_CLASS_INTERVAL,
};

static enum mp_class
mp_classof(enum mp_type type)
{
	return mp_classes[type];
}

static enum mp_class
mp_extension_class(const char *data)
{
	assert(mp_typeof(*data) == MP_EXT);
	int8_t type;
	mp_decode_extl(&data, &type);
	assert(type >= 0 && type < mp_extension_type_MAX);
	return mp_ext_classes[type];
}

/*
 * @brief Read the msgpack value at \p field (whatever it is: int, uint, float
 *        or double) as double.
 * @param field the field to read the value from
 * @retval the read-and-cast result
 */
static double
mp_read_as_double(const char *field)
{
	double result = NAN;
	assert(mp_classof(mp_typeof(*field)) == MP_CLASS_NUMBER);
	/* This can only fail on non-numeric msgpack field, so it shouldn't. */
	if (mp_read_double_lossy(&field, &result) == -1)
		unreachable();
	return result;
}

static int
mp_compare_bool(const char *field_a, const char *field_b)
{
	int a_val = mp_decode_bool(&field_a);
	int b_val = mp_decode_bool(&field_b);
	return COMPARE_RESULT(a_val, b_val);
}

static int
mp_compare_float32(const char *field_a, const char *field_b)
{
	float a_val = mp_decode_float(&field_a);
	float b_val = mp_decode_float(&field_b);
	return COMPARE_RESULT(a_val, b_val);
}

static int
mp_compare_float64(const char *field_a, const char *field_b)
{
	double a_val = mp_decode_double(&field_a);
	double b_val = mp_decode_double(&field_b);
	return COMPARE_RESULT(a_val, b_val);
}

static int
mp_compare_as_double(const char *field_a, const char *field_b)
{
	double a_val = mp_read_as_double(field_a);
	double b_val = mp_read_as_double(field_b);
	return COMPARE_RESULT(a_val, b_val);
}

static int
mp_compare_integer_with_type(const char *field_a, enum mp_type a_type,
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

static int
mp_compare_double_any_int(double lhs, const char *rhs, enum mp_type rhs_type,
			  int k)
{
	if (rhs_type == MP_INT)
		return double_compare_int64(lhs, mp_decode_int(&rhs), k);
	assert(rhs_type == MP_UINT);
	return double_compare_uint64(lhs, mp_decode_uint(&rhs), k);
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
	uint64_t lqbit;
	memcpy(&lqbit, &lhs, sizeof(lhs));
	lqbit &= UINT64_C(0x8000000000000);
	uint64_t rqbit;
	memcpy(&rqbit, &v, sizeof(v));
	rqbit &= UINT64_C(0x8000000000000);
	/*
	 * Lets consider the quiet NaN (fraction first bit == 1)
	 * to be bigger than signaling NaN (fraction first
	 * bit == 0).
	 */
	return k * COMPARE_RESULT(lqbit, rqbit);
}

static int
mp_compare_decimal(const char *lhs, const char *rhs)
{
	decimal_t lhs_dec, rhs_dec;
	decimal_t *ret;
	ret = mp_decode_decimal(&lhs, &lhs_dec);
	assert(ret != NULL);
	ret = mp_decode_decimal(&rhs, &rhs_dec);
	assert(ret != NULL);
	(void)ret;
	return decimal_compare(&lhs_dec, &rhs_dec);

}

/**
 * Compare a decimal to something not representable as decimal. Like NaN, Inf or
 * just a value outside the (-1e38, 1e38) range. In all these cases the decimal
 * value doesn't matter.
 */
static inline int
decimal_compare_nan_or_huge(double rhs)
{
	/* We assume NaN is less than everything else. */
	if (isnan(rhs))
		return 1;
	assert(fabs(rhs) >= 1e38);
	return (rhs < 0) - (rhs > 0);
}

static int
mp_compare_decimal_any_number(decimal_t *lhs, const char *rhs,
			      enum mp_type rhs_type, int k)
{
	decimal_t rhs_dec;
	decimal_t *rc;
	switch (rhs_type) {
	case MP_FLOAT:
	{
		double d = mp_decode_float(&rhs);
		rc = decimal_from_double(&rhs_dec, d);
		if (rc == NULL)
			return decimal_compare_nan_or_huge(d) * k;
		break;
	}
	case MP_DOUBLE:
	{
		double d = mp_decode_double(&rhs);
		rc = decimal_from_double(&rhs_dec, d);
		if (rc == NULL)
			return decimal_compare_nan_or_huge(d) * k;
		break;
	}
	case MP_INT:
	{
		int64_t num = mp_decode_int(&rhs);
		rc = decimal_from_int64(&rhs_dec, num);
		break;
	}
	case MP_UINT:
	{
		uint64_t num = mp_decode_uint(&rhs);
		rc = decimal_from_uint64(&rhs_dec, num);
		break;
	}
	case MP_EXT:
	{
		int8_t ext_type;
		uint32_t len = mp_decode_extl(&rhs, &ext_type);
		switch (ext_type) {
		case MP_DECIMAL:
			rc = decimal_unpack(&rhs, len, &rhs_dec);
			break;
		default:
			unreachable();
		}
		break;
	}
	default:
		unreachable();
	}
	assert(rc != NULL);
	return k * decimal_compare(lhs, &rhs_dec);
}

static int
mp_compare_number_with_type(const char *lhs, enum mp_type lhs_type,
			    const char *rhs, enum mp_type rhs_type)
{
	assert(mp_classof(lhs_type) == MP_CLASS_NUMBER ||
	       mp_extension_class(lhs) == MP_CLASS_NUMBER);
	assert(mp_classof(rhs_type) == MP_CLASS_NUMBER ||
	       mp_extension_class(rhs) == MP_CLASS_NUMBER);

	/*
	 * Test decimals first, so that we don't have to
	 * account for them in other comparators.
	 */
	decimal_t dec;
	if (rhs_type == MP_EXT) {
		int8_t ext_type;
		uint32_t len = mp_decode_extl(&rhs, &ext_type);
		switch (ext_type) {
		case MP_DECIMAL:
			return mp_compare_decimal_any_number(
				decimal_unpack(&rhs, len, &dec), lhs, lhs_type, -1
			);
		default:
			unreachable();
		}
	}
	if (lhs_type == MP_EXT) {
		int8_t ext_type;
		uint32_t len = mp_decode_extl(&lhs, &ext_type);
		switch (ext_type) {
		case MP_DECIMAL:
			return mp_compare_decimal_any_number(
				decimal_unpack(&lhs, len, &dec), rhs, rhs_type, 1
			);
		default:
			unreachable();
		}
	}
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
	return mp_compare_integer_with_type(lhs, lhs_type, rhs, rhs_type);
}

static inline int
mp_compare_number(const char *lhs, const char *rhs)
{
	return mp_compare_number_with_type(lhs, mp_typeof(*lhs),
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
mp_compare_str_coll(const char *field_a, const char *field_b, struct coll *coll)
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

static inline int
mp_compare_uuid(const char *field_a, const char *field_b)
{
	/*
	 * Packed uuid fields are in the right order for
	 * comparison and are big-endian, so memcmp is
	 * the same as tt_uuid_compare() and lets us
	 * spare 2 mp_uuid_unpack() calls.
	 * "field_a + 2" to skip the uuid header.
	 */
	return memcmp(field_a + 2, field_b + 2, UUID_PACKED_LEN);
}

static int
mp_compare_datetime(const char *lhs, const char *rhs)
{
	struct datetime lhs_dt, rhs_dt;
	struct datetime *ret;
	ret = mp_decode_datetime(&lhs, &lhs_dt);
	assert(ret != NULL);
	ret = mp_decode_datetime(&rhs, &rhs_dt);
	assert(ret != NULL);
	(void)ret;
	return datetime_compare(&lhs_dt, &rhs_dt);
}

typedef int (*mp_compare_f)(const char *, const char *);
static mp_compare_f mp_class_comparators[] = {
	/* .MP_CLASS_NIL    = */ NULL,
	/* .MP_CLASS_BOOL   = */ mp_compare_bool,
	/* .MP_CLASS_NUMBER = */ mp_compare_number,
	/* .MP_CLASS_STR    = */ mp_compare_str,
	/* .MP_CLASS_BIN    = */ mp_compare_bin,
	/* .MP_CLASS_UUID   = */ mp_compare_uuid,
	/* .MP_CLASS_DATETIME=*/ mp_compare_datetime,
	/* .MP_CLASS_INTERVAL=*/ NULL,
	/* .MP_CLASS_ARRAY  = */ NULL,
	/* .MP_CLASS_MAP    = */ NULL,
};

static int
mp_compare_scalar_with_type(const char *field_a, enum mp_type a_type,
			    const char *field_b, enum mp_type b_type)
{
	enum mp_class a_class = mp_classof(a_type) < mp_class_max ?
						      mp_classof(a_type) :
						      mp_extension_class(field_a);
	enum mp_class b_class = mp_classof(b_type) < mp_class_max ?
						      mp_classof(b_type) :
						      mp_extension_class(field_b);
	if (a_class != b_class)
		return COMPARE_RESULT(a_class, b_class);
	mp_compare_f cmp = mp_class_comparators[a_class];
	assert(cmp != NULL);
	return cmp(field_a, field_b);
}

static inline int
mp_compare_scalar(const char *field_a, const char *field_b)
{
	return mp_compare_scalar_with_type(field_a, mp_typeof(*field_a),
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
	return mp_compare_scalar_with_type(field_a, type_a, field_b, type_b);
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
int
tuple_compare_field(const char *field_a, const char *field_b,
		    int8_t type, struct coll *coll)
{
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
	case FIELD_TYPE_UINT8:
	case FIELD_TYPE_UINT16:
	case FIELD_TYPE_UINT32:
	case FIELD_TYPE_UINT64:
		return mp_compare_uint(field_a, field_b);
	case FIELD_TYPE_STRING:
		return coll != NULL ?
		       mp_compare_str_coll(field_a, field_b, coll) :
		       mp_compare_str(field_a, field_b);
	case FIELD_TYPE_INTEGER:
	case FIELD_TYPE_INT8:
	case FIELD_TYPE_INT16:
	case FIELD_TYPE_INT32:
	case FIELD_TYPE_INT64:
		return mp_compare_integer_with_type(field_a,
						    mp_typeof(*field_a),
						    field_b,
						    mp_typeof(*field_b));
	case FIELD_TYPE_NUMBER:
		return mp_compare_number(field_a, field_b);
	case FIELD_TYPE_FLOAT32:
		return mp_compare_float32(field_a, field_b);
	case FIELD_TYPE_FLOAT64:
		return mp_compare_float64(field_a, field_b);
	case FIELD_TYPE_DOUBLE:
		return mp_compare_as_double(field_a, field_b);
	case FIELD_TYPE_BOOLEAN:
		return mp_compare_bool(field_a, field_b);
	case FIELD_TYPE_VARBINARY:
		return mp_compare_bin(field_a, field_b);
	case FIELD_TYPE_SCALAR:
		return coll != NULL ?
		       mp_compare_scalar_coll(field_a, field_b, coll) :
		       mp_compare_scalar(field_a, field_b);
	case FIELD_TYPE_DECIMAL:
		return mp_compare_decimal(field_a, field_b);
	case FIELD_TYPE_UUID:
		return mp_compare_uuid(field_a, field_b);
	case FIELD_TYPE_DATETIME:
		return mp_compare_datetime(field_a, field_b);
	default:
		unreachable();
		return 0;
	}
}

static int
tuple_compare_field_with_type(const char *field_a, enum mp_type a_type,
			      const char *field_b, enum mp_type b_type,
			      int8_t type, struct coll *coll)
{
	switch (type) {
	case FIELD_TYPE_UNSIGNED:
	case FIELD_TYPE_UINT8:
	case FIELD_TYPE_UINT16:
	case FIELD_TYPE_UINT32:
	case FIELD_TYPE_UINT64:
		return mp_compare_uint(field_a, field_b);
	case FIELD_TYPE_STRING:
		return coll != NULL ?
		       mp_compare_str_coll(field_a, field_b, coll) :
		       mp_compare_str(field_a, field_b);
	case FIELD_TYPE_INTEGER:
	case FIELD_TYPE_INT8:
	case FIELD_TYPE_INT16:
	case FIELD_TYPE_INT32:
	case FIELD_TYPE_INT64:
		return mp_compare_integer_with_type(field_a, a_type,
						    field_b, b_type);
	case FIELD_TYPE_NUMBER:
		return mp_compare_number_with_type(field_a, a_type,
						   field_b, b_type);
	case FIELD_TYPE_FLOAT32:
		return mp_compare_float32(field_a, field_b);
	case FIELD_TYPE_FLOAT64:
		return mp_compare_float64(field_a, field_b);
	case FIELD_TYPE_DOUBLE:
		return mp_compare_as_double(field_a, field_b);
	case FIELD_TYPE_BOOLEAN:
		return mp_compare_bool(field_a, field_b);
	case FIELD_TYPE_VARBINARY:
		return mp_compare_bin(field_a, field_b);
	case FIELD_TYPE_SCALAR:
		return coll != NULL ?
		       mp_compare_scalar_coll(field_a, field_b, coll) :
		       mp_compare_scalar_with_type(field_a, a_type,
						   field_b, b_type);
	case FIELD_TYPE_DECIMAL:
		return mp_compare_number_with_type(field_a, a_type,
						   field_b, b_type);
	case FIELD_TYPE_UUID:
		return mp_compare_uuid(field_a, field_b);
	case FIELD_TYPE_DATETIME:
		return mp_compare_datetime(field_a, field_b);
	default:
		unreachable();
		return 0;
	}
}

/*
 * Reverse the compare result if the key part sort order is descending.
 */
template<bool has_desc_parts>
static inline int
key_part_compare_result(struct key_part *part, int result)
{
	bool is_asc = !has_desc_parts || part->sort_order != SORT_ORDER_DESC;
	return result * (is_asc ? 1 : -1);
}

/*
 * Reverse the hint if the key part sort order is descending.
 */
template<bool has_desc_parts>
static inline hint_t
key_part_hint(struct key_part *part, hint_t hint)
{
	bool is_asc = !has_desc_parts || part->sort_order != SORT_ORDER_DESC;
	/* HINT_MAX - HINT_NONE underflows to HINT_NONE. */
	return is_asc ? hint : HINT_MAX - hint;
}

/*
 * Implements the field comparison logic. If the key we use is not nullable
 * then a simple call to tuple_compare_field is used.
 *
 * Otherwise one of \p field_a and \p field_b can be NIL: either it's encoded
 * as MP_NIL or if the field is absent (corresponding field pointer equals to
 * NULL). In this case we perform comparison so that NIL is lesser than any
 * value but two NILs are equal.
 *
 * The template parameters a_is_optional and b_is_optional specify if the
 * corresponding field arguments can be absent (equal to NULL).
 *
 * If \p was_null_met is not NULL, sets the boolean pointed by it to true if
 * any of \p field_a and \p field_b is absent or NIL. Othervice the pointed
 * value is not modified.
 *
 * This code had been deduplicated, so made the function always_inline in
 * order to make sure it's still inlined after refactoring.
 *
 * @param part the key part we compare
 * @param field_a the field to compare
 * @param field_b the field to compare against
 * @param was_null_met pointer to the value to set to true if a NIL is met,
 *                     can be set to NULL if the information isn't required
 * @retval 0  if field_a == field_b
 * @retval <0 if field_a < field_b
 * @retval >0 if field_a > field_b
 */
template<bool is_nullable, bool a_is_optional, bool b_is_optional,
	 bool has_desc_parts>
static ALWAYS_INLINE int
key_part_compare_fields(struct key_part *part, const char *field_a,
			const char *field_b, bool *was_null_met = NULL)
{
	int rc;
	if (!is_nullable) {
		rc = tuple_compare_field(field_a, field_b,
					 part->type, part->coll);
		return key_part_compare_result<has_desc_parts>(part, rc);
	}
	enum mp_type a_type = (a_is_optional && field_a == NULL) ?
			      MP_NIL : mp_typeof(*field_a);
	enum mp_type b_type = (b_is_optional && field_b == NULL) ?
			      MP_NIL : mp_typeof(*field_b);
	bool a_is_value = a_type != MP_NIL;
	bool b_is_value = b_type != MP_NIL;
	if (!a_is_value || !b_is_value) {
		if (was_null_met != NULL)
			*was_null_met = true;
		rc = a_is_value - b_is_value;
	} else {
		rc = tuple_compare_field_with_type(field_a, a_type,
						   field_b, b_type,
						   part->type, part->coll);
	}
	return key_part_compare_result<has_desc_parts>(part, rc);
}

template<bool is_nullable, bool has_optional_parts, bool has_json_paths,
	 bool is_multikey, bool has_desc_parts>
static inline int
tuple_compare_slowpath(struct tuple *tuple_a, hint_t tuple_a_hint,
		       struct tuple *tuple_b, hint_t tuple_b_hint,
		       struct key_def *key_def)
{
	assert(has_json_paths == key_def->has_json_paths);
	assert(!has_optional_parts || is_nullable);
	assert(is_nullable == key_def->is_nullable);
	assert(has_optional_parts == key_def->has_optional_parts);
	assert(key_def->is_multikey == is_multikey);
	assert(!is_multikey || (tuple_a_hint != HINT_NONE &&
		tuple_b_hint != HINT_NONE));
	int rc = 0;
	if (!is_multikey && (rc = hint_cmp(tuple_a_hint, tuple_b_hint)) != 0)
		return rc;
	bool was_null_met = false;
	struct key_part *part = key_def->parts;
	const char *tuple_a_raw = tuple_data(tuple_a);
	const char *tuple_b_raw = tuple_data(tuple_b);
	struct tuple_format *format_a = tuple_format(tuple_a);
	struct tuple_format *format_b = tuple_format(tuple_b);
	const char *field_map_a = tuple_field_map(tuple_a);
	const char *field_map_b = tuple_field_map(tuple_b);
	struct key_part *end;
	const char *field_a, *field_b;
	if (is_nullable)
		end = part + key_def->unique_part_count;
	else
		end = part + key_def->part_count;

	for (; part < end; part++) {
		if (is_multikey) {
			field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
							  field_map_a, part,
							  (int)tuple_a_hint);
			field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
							  field_map_b, part,
							  (int)tuple_b_hint);
		} else if (has_json_paths) {
			field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
							  field_map_a, part,
							  MULTIKEY_NONE);
			field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
							  field_map_b, part,
							  MULTIKEY_NONE);
		} else {
			field_a = tuple_field_raw(format_a, tuple_a_raw,
						  field_map_a, part->fieldno);
			field_b = tuple_field_raw(format_b, tuple_b_raw,
						  field_map_b, part->fieldno);
		}
		assert(has_optional_parts ||
		       (field_a != NULL && field_b != NULL));
		rc = key_part_compare_fields<is_nullable, has_optional_parts,
					     has_optional_parts,
					     has_desc_parts>(
			part, field_a, field_b, &was_null_met);
		if (rc != 0)
			return rc;
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
	 * Index parts are equal and contain NULLs. So use
	 * extended parts only.
	 */
	end = key_def->parts + key_def->part_count;
	for (; part < end; ++part) {
		if (is_multikey) {
			field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
							  field_map_a, part,
							  (int)tuple_a_hint);
			field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
							  field_map_b, part,
							  (int)tuple_b_hint);
		} else if (has_json_paths) {
			field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
							  field_map_a, part,
							  MULTIKEY_NONE);
			field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
							  field_map_b, part,
							  MULTIKEY_NONE);
		} else {
			field_a = tuple_field_raw(format_a, tuple_a_raw,
						  field_map_a, part->fieldno);
			field_b = tuple_field_raw(format_b, tuple_b_raw,
						  field_map_b, part->fieldno);
		}
		/*
		 * Extended parts are primary, and they can not
		 * be absent or be NULLs.
		 */
		assert(field_a != NULL && field_b != NULL);
		rc = key_part_compare_fields<false, false, false,
					     has_desc_parts>(
			part, field_a, field_b);
		if (rc != 0)
			return rc;
	}
	return 0;
}

template<bool is_nullable, bool has_optional_parts, bool has_json_paths,
	 bool is_multikey, bool has_desc_parts>
static inline int
tuple_compare_with_key_slowpath(struct tuple *tuple, hint_t tuple_hint,
				const char *key, uint32_t part_count,
				hint_t key_hint, struct key_def *key_def)
{
	assert(has_json_paths == key_def->has_json_paths);
	assert(!has_optional_parts || is_nullable);
	assert(is_nullable == key_def->is_nullable);
	assert(has_optional_parts == key_def->has_optional_parts);
	assert(key != NULL || part_count == 0);
	assert(part_count <= key_def->part_count);
	assert(key_def->is_multikey == is_multikey);
	assert(!is_multikey || (tuple_hint != HINT_NONE &&
		key_hint == HINT_NONE));
	int rc = 0;
	if (!is_multikey && (rc = hint_cmp(tuple_hint, key_hint)) != 0)
		return rc;
	struct key_part *part = key_def->parts;
	struct tuple_format *format = tuple_format(tuple);
	const char *tuple_raw = tuple_data(tuple);
	const char *field_map = tuple_field_map(tuple);
	if (likely(part_count == 1)) {
		const char *field;
		if (is_multikey) {
			field = tuple_field_raw_by_part(format, tuple_raw,
							field_map, part,
							(int)tuple_hint);
		} else if (has_json_paths) {
			field = tuple_field_raw_by_part(format, tuple_raw,
							field_map, part,
							MULTIKEY_NONE);
		} else {
			field = tuple_field_raw(format, tuple_raw, field_map,
						part->fieldno);
		}
		return key_part_compare_fields<is_nullable, has_optional_parts,
					       false, has_desc_parts>(
			part, field, key);
	}

	struct key_part *end = part + part_count;
	for (; part < end; ++part, mp_next(&key)) {
		const char *field;
		if (is_multikey) {
			field = tuple_field_raw_by_part(format, tuple_raw,
							field_map, part,
							(int)tuple_hint);
		} else if (has_json_paths) {
			field = tuple_field_raw_by_part(format, tuple_raw,
							field_map, part,
							MULTIKEY_NONE);
		} else {
			field = tuple_field_raw(format, tuple_raw, field_map,
						part->fieldno);
		}
		rc = key_part_compare_fields<is_nullable, has_optional_parts,
					     false, has_desc_parts>(
			part, field, key);
		if (rc != 0)
			return rc;
	}
	return 0;
}

/**
 * Compare key parts and skip compared equally. After function call, keys
 * will point to the first field that differ or to the end of key or
 * part_count + 1 field in order.
 * Key arguments must not be NULL, allowed to be NULL if dereferenced.
 * Set was_null_met to true if at least one part is NULL, to false otherwise.
 */
template<bool is_nullable, bool has_desc_parts>
static inline int
key_compare_and_skip_parts(const char **key_a, const char **key_b,
			   uint32_t part_count, struct key_def *key_def,
			   bool *was_null_met)
{
	assert(is_nullable == key_def->is_nullable);
	assert(key_a != NULL && key_b != NULL);
	assert((*key_a != NULL && *key_b != NULL) || part_count == 0);
	struct key_part *part = key_def->parts;
	int rc;
	*was_null_met = false;

	if (likely(part_count == 1)) {
		rc = key_part_compare_fields<is_nullable, false, false,
					     has_desc_parts>(
			part, *key_a, *key_b, was_null_met);
		/* If key parts are equals, we must skip them. */
		if (rc == 0) {
			mp_next(key_a);
			mp_next(key_b);
		}
		return rc;
	}

	struct key_part *end = part + part_count;
	for (; part < end; ++part, mp_next(key_a), mp_next(key_b)) {
		rc = key_part_compare_fields<is_nullable, false, false,
					     has_desc_parts>(
			part, *key_a, *key_b, was_null_met);
		if (rc != 0)
			return rc;
	}
	return 0;
}

template<bool is_nullable, bool has_desc_parts>
static inline int
key_compare_parts(const char *key_a, const char *key_b, uint32_t part_count,
		  struct key_def *key_def, bool *was_null_met)
{
	return key_compare_and_skip_parts<is_nullable, has_desc_parts>(
		&key_a, &key_b, part_count, key_def, was_null_met);
}

template<bool is_nullable, bool has_optional_parts, bool has_desc_parts>
static inline int
tuple_compare_with_key_sequential(struct tuple *tuple, hint_t tuple_hint,
				  const char *key, uint32_t part_count,
				  hint_t key_hint, struct key_def *key_def)
{
	assert(!has_optional_parts || is_nullable);
	assert(key_def_is_sequential(key_def));
	assert(is_nullable == key_def->is_nullable);
	assert(has_optional_parts == key_def->has_optional_parts);
	int rc = hint_cmp(tuple_hint, key_hint);
	if (rc != 0)
		return rc;
	const char *tuple_key = tuple_data(tuple);
	uint32_t field_count = mp_decode_array(&tuple_key);
	uint32_t cmp_part_count;
	if (has_optional_parts && field_count < part_count) {
		cmp_part_count = field_count;
	} else {
		assert(field_count >= part_count);
		cmp_part_count = part_count;
	}
	bool unused;
	rc = key_compare_and_skip_parts<is_nullable, has_desc_parts>(
		&tuple_key, &key, cmp_part_count, key_def, &unused);
	if (!has_optional_parts || rc != 0)
		return rc;
	/*
	 * If some tuple indexed fields are absent, then check
	 * corresponding key fields to be equal to NULL.
	 */
	if (field_count < part_count) {
		for (uint32_t i = field_count; i < part_count;
		     ++i, mp_next(&key)) {
			rc = key_part_compare_fields<true, true, false,
						     has_desc_parts>(
				&key_def->parts[i], NULL, key);
			if (rc != 0)
				return rc;
		}
	}
	return 0;
}

int
key_compare(const char *key_a, uint32_t part_count_a, hint_t key_a_hint,
	    const char *key_b, uint32_t part_count_b, hint_t key_b_hint,
	    struct key_def *key_def)
{
	int rc = hint_cmp(key_a_hint, key_b_hint);
	if (rc != 0)
		return rc;
	assert(part_count_a <= key_def->part_count);
	assert(part_count_b <= key_def->part_count);
	uint32_t part_count = MIN(part_count_a, part_count_b);
	assert(part_count <= key_def->part_count);
	bool unused;
	if (! key_def->is_nullable) {
		if (key_def_has_desc_parts(key_def)) {
			return key_compare_parts<false, true>(
				key_a, key_b, part_count, key_def, &unused);
		} else {
			return key_compare_parts<false, false>(
				key_a, key_b, part_count, key_def, &unused);
		}
	} else {
		if (key_def_has_desc_parts(key_def)) {
			return key_compare_parts<true, true>(
				key_a, key_b, part_count, key_def, &unused);
		} else {
			return key_compare_parts<true, false>(
				key_a, key_b, part_count, key_def, &unused);
		}
	}
}

template<bool is_nullable, bool has_optional_parts, bool has_desc_parts>
static int
tuple_compare_sequential(struct tuple *tuple_a, hint_t tuple_a_hint,
			 struct tuple *tuple_b, hint_t tuple_b_hint,
			 struct key_def *key_def)
{
	assert(!has_optional_parts || is_nullable);
	assert(has_optional_parts == key_def->has_optional_parts);
	assert(key_def_is_sequential(key_def));
	assert(is_nullable == key_def->is_nullable);
	int rc = hint_cmp(tuple_a_hint, tuple_b_hint);
	if (rc != 0)
		return rc;
	const char *key_a = tuple_data(tuple_a);
	uint32_t fc_a = mp_decode_array(&key_a);
	const char *key_b = tuple_data(tuple_b);
	uint32_t fc_b = mp_decode_array(&key_b);
	if (!has_optional_parts && !is_nullable) {
		assert(fc_a >= key_def->part_count);
		assert(fc_b >= key_def->part_count);
		bool unused;
		return key_compare_parts<false, has_desc_parts>(
			key_a, key_b, key_def->part_count, key_def, &unused);
	}
	bool was_null_met = false;
	struct key_part *part = key_def->parts;
	struct key_part *end = part + key_def->unique_part_count;
	uint32_t i = 0;
	for (const char *field_a, *field_b; part < end; ++part, ++i) {
		field_a = (has_optional_parts && i >= fc_a) ? NULL : key_a;
		field_b = (has_optional_parts && i >= fc_b) ? NULL : key_b;
		rc = key_part_compare_fields<true, has_optional_parts,
					     has_optional_parts,
					     has_desc_parts>(
			part, field_a, field_b, &was_null_met);
		if (rc != 0)
			return rc;
		if (!has_optional_parts || i < fc_a)
			mp_next(&key_a);
		if (!has_optional_parts || i < fc_b)
			mp_next(&key_b);
	}
	if (! was_null_met)
		return 0;
	end = key_def->parts + key_def->part_count;
	for (; part < end; ++part, ++i, mp_next(&key_a), mp_next(&key_b)) {
		/*
		 * If tuples are equal by unique_part_count, then
		 * the rest of parts are a primary key, which can
		 * not be absent or be null.
		 */
		assert(i < fc_a && i < fc_b);
		rc = key_part_compare_fields<false, false, false,
					     has_desc_parts>(
			part, key_a, key_b);
		if (rc != 0)
			return rc;
	}
	return 0;
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
	inline static int compare(struct tuple *tuple_a,
				  struct tuple *tuple_b,
				  struct tuple_format *format_a,
				  struct tuple_format *format_b,
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
	inline static int compare(struct tuple *,
				  struct tuple *,
				  struct tuple_format *,
				  struct tuple_format *,
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
	static int compare(struct tuple *tuple_a, hint_t tuple_a_hint,
			   struct tuple *tuple_b, hint_t tuple_b_hint,
			   struct key_def *)
	{
		int rc = hint_cmp(tuple_a_hint, tuple_b_hint);
		if (rc != 0)
			return rc;
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
	static int compare(struct tuple *tuple_a, hint_t tuple_a_hint,
			   struct tuple *tuple_b, hint_t tuple_b_hint,
			   struct key_def *)
	{
		int rc = hint_cmp(tuple_a_hint, tuple_b_hint);
		if (rc != 0)
			return rc;
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
	compare(struct tuple *tuple, const char *key, uint32_t part_count,
		struct key_def *key_def, struct tuple_format *format,
		const char *field)
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
	inline static int compare(struct tuple *,
				  const char *key,
				  uint32_t,
				  struct key_def *,
				  struct tuple_format *,
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
	compare(struct tuple *tuple, hint_t tuple_hint,
		const char *key, uint32_t part_count,
		hint_t key_hint, struct key_def *key_def)
	{
		/* Part count can be 0 in wildcard searches. */
		if (part_count == 0)
			return 0;
		int rc = hint_cmp(tuple_hint, key_hint);
		if (rc != 0)
			return rc;
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
	static int compare(struct tuple *tuple, hint_t tuple_hint,
			   const char *key, uint32_t part_count,
			   hint_t key_hint, struct key_def *key_def)
	{
		/* Part count can be 0 in wildcard searches. */
		if (part_count == 0)
			return 0;
		int rc = hint_cmp(tuple_hint, key_hint);
		if (rc != 0)
			return rc;
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

/**
 * A functional index tuple compare.
 * tuple_a_hint and tuple_b_hint are expected to be valid pointers to functional
 * key memory. These keys are represented as a MsgPack array with the number of
 * elements equal to the functional index definition part_count (let's denote
 * it as func_index_part_count). The keys have been already validated by
 * key_list_iterator_next().
 *
 * In case of unique non-nullable index, the non-extended key_def is used as a
 * cmp_def (see memtx_tree_index_new_tpl()). Otherwise, the extended cmp_def has
 * part_count > func_index_part_count, since it was produced by key_def_merge()
 * of the functional key part and the primary key. So its tail parts are taken
 * from primary index key definition.
 */
template<bool is_nullable, bool has_desc_parts>
static inline int
func_index_compare(struct tuple *tuple_a, hint_t tuple_a_hint,
		   struct tuple *tuple_b, hint_t tuple_b_hint,
		   struct key_def *cmp_def)
{
	assert(cmp_def->for_func_index);
	assert(is_nullable == cmp_def->is_nullable);

	const char *key_a = tuple_data((struct tuple *)tuple_a_hint);
	const char *key_b = tuple_data((struct tuple *)tuple_b_hint);
	assert(mp_typeof(*key_a) == MP_ARRAY);
	uint32_t part_count_a = mp_decode_array(&key_a);
	assert(mp_typeof(*key_b) == MP_ARRAY);
	uint32_t part_count_b = mp_decode_array(&key_b);
	assert(part_count_a == part_count_b);
	uint32_t key_part_count = part_count_a;
	(void)part_count_b;

	bool was_null_met;
	int rc = key_compare_parts<is_nullable, has_desc_parts>(
		key_a, key_b, key_part_count, cmp_def, &was_null_met);
	if (rc != 0)
		return rc;
	/*
	 * Tuples with nullified fields may violate the uniqueness constraint
	 * so if we encountered a nullified field while comparing the secondary
	 * key parts we must proceed with comparing the primary key parts even
	 * if the secondary index is unique.
	 */
	if (key_part_count == cmp_def->unique_part_count &&
	    (!is_nullable || !was_null_met))
		return 0;
	/*
	 * Primary index definition key compare.
	 * It cannot contain nullable parts so the code is
	 * simplified correspondingly.
	 */
	const char *tuple_a_raw = tuple_data(tuple_a);
	const char *tuple_b_raw = tuple_data(tuple_b);
	struct tuple_format *format_a = tuple_format(tuple_a);
	struct tuple_format *format_b = tuple_format(tuple_b);
	const char *field_map_a = tuple_field_map(tuple_a);
	const char *field_map_b = tuple_field_map(tuple_b);
	const char *field_a, *field_b;
	for (uint32_t i = key_part_count; i < cmp_def->part_count; i++) {
		struct key_part *part = &cmp_def->parts[i];
		field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
						  field_map_a, part,
						  MULTIKEY_NONE);
		field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
						  field_map_b, part,
						  MULTIKEY_NONE);
		assert(field_a != NULL && field_b != NULL);
		rc = key_part_compare_fields<false, false, false,
					     has_desc_parts>(
			part, field_a, field_b);
		if (rc != 0)
			return rc;
	}
	return 0;
}

/**
 * A functional index key compare.
 * tuple_hint is expected to be a valid pointer to
 * functional key memory and is compared with the given key by
 * using the functional index key definition.
 */
template<bool is_nullable, bool has_desc_parts>
static inline int
func_index_compare_with_key(struct tuple *tuple, hint_t tuple_hint,
			    const char *key, uint32_t part_count,
			    hint_t key_hint, struct key_def *key_def)
{
	(void)key_hint;
	assert(key_def->for_func_index);
	assert(is_nullable == key_def->is_nullable);
	const char *tuple_key = tuple_data((struct tuple *)tuple_hint);
	assert(mp_typeof(*tuple_key) == MP_ARRAY);

	uint32_t tuple_key_count = mp_decode_array(&tuple_key);
	uint32_t cmp_part_count = MIN(part_count, tuple_key_count);
	cmp_part_count = MIN(cmp_part_count, key_def->part_count);
	bool unused;
	int rc = key_compare_and_skip_parts<is_nullable, has_desc_parts>(
		&tuple_key, &key, cmp_part_count, key_def, &unused);
	if (rc != 0)
		return rc;
	/* Equals if nothing to compare. */
	if (part_count == cmp_part_count ||
	    key_def->part_count == cmp_part_count)
		return 0;
	/*
	 * Now we know that tuple_key count is less than key part_count
	 * and key_def part_count, so let's keep comparing, but with
	 * original tuple fields. We will compare parts of primary key,
	 * it cannot contain nullable parts so the code is simplified
	 * correspondingly. Also, all the key parts, corresponding to
	 * func key, were already skipped.
	 */
	const char *tuple_raw = tuple_data(tuple);
	struct tuple_format *format = tuple_format(tuple);
	const char *field_map = tuple_field_map(tuple);
	const char *field;
	part_count = MIN(part_count, key_def->part_count);
	for (uint32_t i = cmp_part_count; i < part_count; i++) {
		struct key_part *part = &key_def->parts[i];
		field = tuple_field_raw_by_part(format, tuple_raw, field_map,
						part, MULTIKEY_NONE);
		assert(field != NULL);
		rc = key_part_compare_fields<false, false, false,
					     has_desc_parts>(
			part, field, key);
		mp_next(&key);
		if (rc != 0)
			return rc;
	}
	return 0;
}

/* }}} tuple_compare_with_key */

/* {{{ tuple_hint */

/**
 * A comparison hint is an unsigned integer number that has
 * the following layout:
 *
 *     [         class         |         value         ]
 *      <-- HINT_CLASS_BITS --> <-- HINT_VALUE_BITS -->
 *      <----------------- HINT_BITS ----------------->
 *
 * For simplicity we construct it using the first key part only;
 * other key parts don't participate in hint construction. As a
 * consequence, tuple hints are useless if the first key part
 * doesn't differ among indexed tuples.
 *
 * Hint class stores one of mp_class enum values corresponding
 * to the field type. We store it in upper bits of a hint so
 * as to guarantee that tuple fields that have different types
 * are compared correctly in a scalar index. We use the same
 * layout for indexes of base types too so that we don't need
 * to recalculate hints when altering an index from scalar to
 * one of base types or vice versa without index rebuild.
 *
 * Hint value is stored in lower bits of a hint and chosen so
 * as to satisfy the hint property (see the comment to hint_t).
 * Also, hint values must be compatible among all kinds of numbers
 * (integer, unsigned, floating point) so that we can alter an
 * index between any two of them without touching the hints.
 *
 * The value depends on the field type:
 *
 *  - For an integer field, the hint value equals the number
 *    stored in the field minus the minimal (negative) integer
 *    number that fits in a hint. If the number is too large
 *    or too small to fit in a hint, we use the max or the min
 *    number that fits. This guarantees correct order for all
 *    positive and negative integers.
 *
 *  - For a field storing a floating point number, we use
 *    the hint computed for the integral part. This ensures
 *    compatibility between floating point and integer field
 *    hints.
 *
 *  - For a boolean field, the hint value is simply 0 or 1.
 *
 *  - For a string field, we take the first few characters that
 *    fit in a hint and shift them in such a way that comparing
 *    them is equivalent to strcmp(). If there's a collation,
 *    we use the sort key instead of the string (see coll->hint).
 *
 *  - For a field containing NULL, the value is 0, and we rely on
 *    mp_class comparison rules for arranging nullable fields.
 *
 * Note: comparison hint only makes sense for non-multikey
 * indexes.
 */
#define HINT_BITS		(sizeof(hint_t) * CHAR_BIT)
#define HINT_CLASS_BITS		4
#define HINT_VALUE_BITS		(HINT_BITS - HINT_CLASS_BITS)

/** Number of bytes that fit in a hint value. */
#define HINT_VALUE_BYTES	(HINT_VALUE_BITS / CHAR_BIT)

/** Max unsigned integer that can be stored in a hint value. */
#define HINT_VALUE_MAX		((1ULL << HINT_VALUE_BITS) - 1)

/**
 * Max and min signed integer numbers that fit in a hint value.
 * For numbers > MAX and < MIN we store MAX and MIN, respectively.
 */
#define HINT_VALUE_INT_MAX	((1LL << (HINT_VALUE_BITS - 1)) - 1)
#define HINT_VALUE_INT_MIN	(-(1LL << (HINT_VALUE_BITS - 1)))

/**
 * Max and min floating point numbers whose integral parts fit
 * in a hint value. Note, we can't compare a floating point number
 * with HINT_VALUE_INT_{MIN,MAX} because of rounding errors.
 */
#define HINT_VALUE_DOUBLE_MAX	(exp2(HINT_VALUE_BITS - 1) - 1)
#define HINT_VALUE_DOUBLE_MIN	(-exp2(HINT_VALUE_BITS - 1))

/**
 * We need to squeeze 64 bits of seconds and 32 bits of nanoseconds
 * into 60 bits of hint value. The idea is to represent wide enough
 * years range, and leave the rest of bits occupied from nanoseconds part:
 * - 36 bits is enough for time range of [-208-05-13..6325-04-08]
 * - for nanoseconds there is left 24 bits, which are MSB part of
 *   32-bit value
 */
#define HINT_VALUE_SECS_BITS	36
#define HINT_VALUE_NSEC_BITS	(HINT_VALUE_BITS - HINT_VALUE_SECS_BITS)
#define HINT_VALUE_SECS_MAX	((1LL << (HINT_VALUE_SECS_BITS - 1)) - 1)
#define HINT_VALUE_SECS_MIN	(-(1LL << (HINT_VALUE_SECS_BITS - 1)))
#define HINT_VALUE_NSEC_SHIFT	(sizeof(int32_t) * CHAR_BIT - HINT_VALUE_NSEC_BITS)
#define HINT_VALUE_NSEC_MAX	((1ULL << HINT_VALUE_NSEC_BITS) - 1)

/*
 * HINT_CLASS_BITS should be big enough to store any mp_class value.
 * Note, ((1 << HINT_CLASS_BITS) - 1) is reserved for HINT_NONE.
 */
static_assert(mp_class_max < (1 << HINT_CLASS_BITS) - 1,
	      "mp_class must fit in tuple hint");

static inline hint_t
hint_create(enum mp_class c, uint64_t val)
{
	assert((val >> HINT_VALUE_BITS) == 0);
	return (hint_t)(((uint64_t)c << HINT_VALUE_BITS) | val);
}

static inline hint_t
hint_nil(void)
{
	return hint_create(MP_CLASS_NIL, 0);
}

static inline hint_t
hint_bool(bool b)
{
	return hint_create(MP_CLASS_BOOL, b ? 1 : 0);
}

static inline hint_t
hint_uint(uint64_t u)
{
	uint64_t val = (u >= (uint64_t)HINT_VALUE_INT_MAX ?
			HINT_VALUE_MAX : u - HINT_VALUE_INT_MIN);
	return hint_create(MP_CLASS_NUMBER, val);
}

static inline hint_t
hint_int(int64_t i)
{
	assert(i < 0);
	uint64_t val = (i <= HINT_VALUE_INT_MIN ? 0 : i - HINT_VALUE_INT_MIN);
	return hint_create(MP_CLASS_NUMBER, val);
}

static inline hint_t
hint_double(double d)
{
	if (!isfinite(d))
		return HINT_NONE;

	uint64_t val;
	if (d >= HINT_VALUE_DOUBLE_MAX)
		val = HINT_VALUE_MAX;
	else if (d <= HINT_VALUE_DOUBLE_MIN)
		val = 0;
	else
		val = (int64_t)d - HINT_VALUE_INT_MIN;

	return hint_create(MP_CLASS_NUMBER, val);
}

static inline hint_t
hint_decimal(decimal_t *dec)
{
	uint64_t val = 0;
	int64_t num;
	if (decimal_to_int64(dec, &num) &&
	    num >= HINT_VALUE_INT_MIN && num <= HINT_VALUE_INT_MAX) {
		val = num - HINT_VALUE_INT_MIN;
	} else if (!(dec->bits & DECNEG)) {
		val = HINT_VALUE_MAX;
	}
	/*
	 * In case the number is negative and out of bounds, val
	 * remains zero.
	 */
	return hint_create(MP_CLASS_NUMBER, val);
}

static inline hint_t
hint_uuid_raw(const char *data)
{
	/*
	 * Packed UUID fields are big-endian and are stored in the
	 * order allowing lexicographical comparison, so the first
	 * 8 bytes of the packed representation constitute a big
	 * endian unsigned integer. Use it as a hint.
	 */
	uint64_t val = mp_load_u64(&data);
	/* Make space for class representation. */
	val >>= HINT_CLASS_BITS;
	return hint_create(MP_CLASS_UUID, val);
}

static inline hint_t
hint_datetime(struct datetime *date)
{
	/*
	 * Use at most HINT_VALUE_SECS_BITS from datetime
	 * seconds field as a hint value, and at MSB part
	 * of HINT_VALUE_NSEC_BITS from nanoseconds.
	 */
	int64_t secs = date->epoch;
	/*
	 * Both overflow and underflow
	 */
	bool is_overflow = false;
	uint64_t val;
	if (secs < HINT_VALUE_SECS_MIN) {
		is_overflow = true;
		val = 0;
	} else {
		val = secs - HINT_VALUE_SECS_MIN;
	}
	if (val > HINT_VALUE_SECS_MAX) {
		is_overflow = true;
		val = HINT_VALUE_SECS_MAX;
	}
	val <<= HINT_VALUE_NSEC_BITS;
	if (is_overflow == false) {
		val |= (date->nsec >> HINT_VALUE_NSEC_SHIFT) &
			HINT_VALUE_NSEC_MAX;
	}
	return hint_create(MP_CLASS_DATETIME, val);
}

static inline uint64_t
hint_str_raw(const char *s, uint32_t len)
{
	len = MIN(len, HINT_VALUE_BYTES);
	uint64_t val = 0;
	for (uint32_t i = 0; i < len; i++) {
		val <<= CHAR_BIT;
		val |= (unsigned char)s[i];
	}
	val <<= CHAR_BIT * (HINT_VALUE_BYTES - len);
	return val;
}

static inline hint_t
hint_str(const char *s, uint32_t len)
{
	uint64_t val = hint_str_raw(s, len);
	return hint_create(MP_CLASS_STR, val);
}

static inline hint_t
hint_str_coll(const char *s, uint32_t len, struct coll *coll)
{
	char buf[HINT_VALUE_BYTES];
	uint32_t buf_len = coll->hint(s, len, buf, sizeof(buf), coll);
	uint64_t val = hint_str_raw(buf, buf_len);
	return hint_create(MP_CLASS_STR, val);
}

static inline hint_t
hint_bin(const char *s, uint32_t len)
{
	uint64_t val = hint_str_raw(s, len);
	return hint_create(MP_CLASS_BIN, val);
}

static inline hint_t
field_hint_boolean(const char *field)
{
	assert(mp_typeof(*field) == MP_BOOL);
	return hint_bool(mp_decode_bool(&field));
}

static inline hint_t
field_hint_unsigned(const char *field)
{
	assert(mp_typeof(*field) == MP_UINT);
	return hint_uint(mp_decode_uint(&field));
}

static inline hint_t
field_hint_integer(const char *field)
{
	switch (mp_typeof(*field)) {
	case MP_UINT:
		return hint_uint(mp_decode_uint(&field));
	case MP_INT:
		return hint_int(mp_decode_int(&field));
	default:
		unreachable();
	}
	return HINT_NONE;
}

static inline hint_t
field_hint_float32(const char *field)
{
	assert(mp_typeof(*field) == MP_FLOAT);
	return hint_double(mp_decode_float(&field));
}

static inline hint_t
field_hint_float64(const char *field)
{
	assert(mp_typeof(*field) == MP_DOUBLE);
	return hint_double(mp_decode_double(&field));
}

static inline hint_t
field_hint_double(const char *field)
{
	return hint_double(mp_read_as_double(field));
}

static inline hint_t
field_hint_number(const char *field)
{
	switch (mp_typeof(*field)) {
	case MP_UINT:
		return hint_uint(mp_decode_uint(&field));
	case MP_INT:
		return hint_int(mp_decode_int(&field));
	case MP_FLOAT:
		return hint_double(mp_decode_float(&field));
	case MP_DOUBLE:
		return hint_double(mp_decode_double(&field));
	case MP_EXT:
	{
		int8_t ext_type;
		uint32_t len = mp_decode_extl(&field, &ext_type);
		switch (ext_type) {
		case MP_DECIMAL:
		{
			decimal_t dec;
			return hint_decimal(decimal_unpack(&field, len, &dec));
		}
		default:
			unreachable();
		}
	}
	default:
		unreachable();
	}
	return HINT_NONE;
}

static inline hint_t
field_hint_decimal(const char *field)
{
	assert(mp_typeof(*field) == MP_EXT);
	int8_t ext_type;
	uint32_t len = mp_decode_extl(&field, &ext_type);
	switch (ext_type) {
	case MP_DECIMAL:
	{
		decimal_t dec;
		return hint_decimal(decimal_unpack(&field, len, &dec));
	}
	default:
		unreachable();
	}
}

static inline hint_t
field_hint_uuid(const char *field)
{
	assert(mp_typeof(*field) == MP_EXT);
	int8_t type;
	uint32_t len;
	const char *data = mp_decode_ext(&field, &type, &len);
	assert(type == MP_UUID && len == UUID_PACKED_LEN);
	return hint_uuid_raw(data);
}

static inline hint_t
field_hint_datetime(const char *field)
{
	assert(mp_typeof(*field) == MP_EXT);
	int8_t ext_type;
	uint32_t len = mp_decode_extl(&field, &ext_type);
	assert(ext_type == MP_DATETIME);
	struct datetime date;
	return hint_datetime(datetime_unpack(&field, len, &date));
}

static inline hint_t
field_hint_string(const char *field, struct coll *coll)
{
	assert(mp_typeof(*field) == MP_STR);
	uint32_t len = mp_decode_strl(&field);
	return coll == NULL ? hint_str(field, len) :
			      hint_str_coll(field, len, coll);
}

static inline hint_t
field_hint_varbinary(const char *field)
{
	assert(mp_typeof(*field) == MP_BIN);
	uint32_t len = mp_decode_binl(&field);
	return hint_bin(field, len);
}

static inline hint_t
field_hint_scalar(const char *field, struct coll *coll)
{
	uint32_t len;
	switch(mp_typeof(*field)) {
	case MP_BOOL:
		return hint_bool(mp_decode_bool(&field));
	case MP_UINT:
		return hint_uint(mp_decode_uint(&field));
	case MP_INT:
		return hint_int(mp_decode_int(&field));
	case MP_FLOAT:
		return hint_double(mp_decode_float(&field));
	case MP_DOUBLE:
		return hint_double(mp_decode_double(&field));
	case MP_STR:
		len = mp_decode_strl(&field);
		return coll == NULL ? hint_str(field, len) :
				      hint_str_coll(field, len, coll);
	case MP_BIN:
		len = mp_decode_binl(&field);
		return hint_bin(field, len);
	case MP_EXT:
	{
		int8_t ext_type;
		uint32_t len = mp_decode_extl(&field, &ext_type);
		switch (ext_type) {
		case MP_DECIMAL:
		{
			decimal_t dec;
			return hint_decimal(decimal_unpack(&field, len, &dec));
		}
		case MP_UUID:
			return hint_uuid_raw(field);
		case MP_DATETIME:
		{
			struct datetime date;
			return hint_datetime(datetime_unpack(&field, len, &date));
		}
		default:
			unreachable();
		}
	}
	default:
		unreachable();
	}
	return HINT_NONE;
}

template <enum field_type type, bool is_nullable>
static inline hint_t
field_hint(const char *field, struct coll *coll)
{
	if (is_nullable && mp_typeof(*field) == MP_NIL)
		return hint_nil();
	switch (type) {
	case FIELD_TYPE_BOOLEAN:
		return field_hint_boolean(field);
	case FIELD_TYPE_UNSIGNED:
	case FIELD_TYPE_UINT8:
	case FIELD_TYPE_UINT16:
	case FIELD_TYPE_UINT32:
	case FIELD_TYPE_UINT64:
		return field_hint_unsigned(field);
	case FIELD_TYPE_INTEGER:
	case FIELD_TYPE_INT8:
	case FIELD_TYPE_INT16:
	case FIELD_TYPE_INT32:
	case FIELD_TYPE_INT64:
		return field_hint_integer(field);
	case FIELD_TYPE_NUMBER:
		return field_hint_number(field);
	case FIELD_TYPE_FLOAT32:
		return field_hint_float32(field);
	case FIELD_TYPE_FLOAT64:
		return field_hint_float64(field);
	case FIELD_TYPE_DOUBLE:
		return field_hint_double(field);
	case FIELD_TYPE_STRING:
		return field_hint_string(field, coll);
	case FIELD_TYPE_VARBINARY:
		return field_hint_varbinary(field);
	case FIELD_TYPE_SCALAR:
		return field_hint_scalar(field, coll);
	case FIELD_TYPE_DECIMAL:
		return field_hint_decimal(field);
	case FIELD_TYPE_UUID:
		return field_hint_uuid(field);
	case FIELD_TYPE_DATETIME:
		return field_hint_datetime(field);
	default:
		unreachable();
	}
	return HINT_NONE;
}

template<enum field_type type, bool is_nullable, bool has_desc_parts>
static hint_t
key_hint(const char *key, uint32_t part_count, struct key_def *key_def)
{
	assert(!key_def->is_multikey);
	if (part_count == 0)
		return HINT_NONE;
	hint_t h = field_hint<type, is_nullable>(key, key_def->parts->coll);
	return key_part_hint<has_desc_parts>(key_def->parts, h);
}

template<enum field_type type, bool is_nullable, bool has_desc_parts>
static hint_t
tuple_hint(struct tuple *tuple, struct key_def *key_def)
{
	assert(!key_def->is_multikey);
	const char *field = tuple_field_by_part(tuple, key_def->parts,
						MULTIKEY_NONE);
	hint_t h = is_nullable && field == NULL ? hint_nil() :
		   field_hint<type, is_nullable>(field, key_def->parts->coll);
	return key_part_hint<has_desc_parts>(key_def->parts, h);
}

static hint_t
key_hint_stub(const char *key, uint32_t part_count, struct key_def *key_def)
{
	(void) key;
	(void) part_count;
	(void) key_def;
	/*
	 * Multikey hint for tuple is an index of the key in
	 * array, it always must be defined. While
	 * key_hint_stub assumes that it must be
	 * initialized manually (so it mustn't be called),
	 * the virtual method for a key makes sense. Overriding
	 * this method such way, we extend existend code to
	 * do nothing on key hint calculation an it is valid
	 * because it is never used(unlike tuple hint).
	 */
	assert(key_def->is_multikey || key_def->for_func_index);
	return HINT_NONE;
}

static hint_t
tuple_hint_stub(struct tuple *tuple, struct key_def *key_def)
{
	(void) tuple;
	(void) key_def;
	unreachable();
	return HINT_NONE;
}

template<enum field_type type, bool is_nullable, bool has_desc_parts>
static void
key_def_set_hint_func(struct key_def *def)
{
	def->key_hint = key_hint<type, is_nullable, has_desc_parts>;
	def->tuple_hint = tuple_hint<type, is_nullable, has_desc_parts>;
}

template<enum field_type type, bool is_nullable>
static void
key_def_set_hint_func(struct key_def *def)
{
	if (key_def_has_desc_parts(def))
		key_def_set_hint_func<type, is_nullable, true>(def);
	else
		key_def_set_hint_func<type, is_nullable, false>(def);
}

template<enum field_type type>
static void
key_def_set_hint_func(struct key_def *def)
{
	if (key_part_is_nullable(def->parts))
		key_def_set_hint_func<type, true>(def);
	else
		key_def_set_hint_func<type, false>(def);
}

static void
key_def_set_hint_func(struct key_def *def)
{
	if (def->is_multikey || def->for_func_index) {
		def->key_hint = key_hint_stub;
		def->tuple_hint = tuple_hint_stub;
		return;
	}
	switch (def->parts->type) {
	case FIELD_TYPE_BOOLEAN:
		key_def_set_hint_func<FIELD_TYPE_BOOLEAN>(def);
		break;
	case FIELD_TYPE_UNSIGNED:
		key_def_set_hint_func<FIELD_TYPE_UNSIGNED>(def);
		break;
	case FIELD_TYPE_INTEGER:
		key_def_set_hint_func<FIELD_TYPE_INTEGER>(def);
		break;
	case FIELD_TYPE_NUMBER:
		key_def_set_hint_func<FIELD_TYPE_NUMBER>(def);
		break;
	case FIELD_TYPE_DOUBLE:
		key_def_set_hint_func<FIELD_TYPE_DOUBLE>(def);
		break;
	case FIELD_TYPE_STRING:
		key_def_set_hint_func<FIELD_TYPE_STRING>(def);
		break;
	case FIELD_TYPE_VARBINARY:
		key_def_set_hint_func<FIELD_TYPE_VARBINARY>(def);
		break;
	case FIELD_TYPE_SCALAR:
		key_def_set_hint_func<FIELD_TYPE_SCALAR>(def);
		break;
	case FIELD_TYPE_DECIMAL:
		key_def_set_hint_func<FIELD_TYPE_DECIMAL>(def);
		break;
	case FIELD_TYPE_UUID:
		key_def_set_hint_func<FIELD_TYPE_UUID>(def);
		break;
	case FIELD_TYPE_DATETIME:
		key_def_set_hint_func<FIELD_TYPE_DATETIME>(def);
		break;
	case FIELD_TYPE_INT8:
		key_def_set_hint_func<FIELD_TYPE_INT8>(def);
		break;
	case FIELD_TYPE_UINT8:
		key_def_set_hint_func<FIELD_TYPE_UINT8>(def);
		break;
	case FIELD_TYPE_INT16:
		key_def_set_hint_func<FIELD_TYPE_INT16>(def);
		break;
	case FIELD_TYPE_UINT16:
		key_def_set_hint_func<FIELD_TYPE_UINT16>(def);
		break;
	case FIELD_TYPE_INT32:
		key_def_set_hint_func<FIELD_TYPE_INT32>(def);
		break;
	case FIELD_TYPE_UINT32:
		key_def_set_hint_func<FIELD_TYPE_UINT32>(def);
		break;
	case FIELD_TYPE_INT64:
		key_def_set_hint_func<FIELD_TYPE_INT64>(def);
		break;
	case FIELD_TYPE_UINT64:
		key_def_set_hint_func<FIELD_TYPE_UINT64>(def);
		break;
	case FIELD_TYPE_FLOAT32:
		key_def_set_hint_func<FIELD_TYPE_FLOAT32>(def);
		break;
	case FIELD_TYPE_FLOAT64:
		key_def_set_hint_func<FIELD_TYPE_FLOAT64>(def);
		break;
	default:
		/* Invalid key definition. */
		def->key_hint = NULL;
		def->tuple_hint = NULL;
		break;
	}
}

/* }}} tuple_hint */

static void
key_def_set_compare_func_fast(struct key_def *def)
{
	assert(!def->is_nullable);
	assert(!def->has_optional_parts);
	assert(!def->has_json_paths);
	assert(!key_def_has_collation(def));
	assert(!key_def_has_desc_parts(def));

	tuple_compare_t cmp = NULL;
	tuple_compare_with_key_t cmp_wk = NULL;
	bool is_sequential = key_def_is_sequential(def);

	/*
	 * Use pre-compiled comparators if available, otherwise
	 * fall back on generic comparators.
	 */
	for (uint32_t k = 0; k < lengthof(cmp_arr); k++) {
		uint32_t i = 0;
		for (; i < def->part_count; i++)
			if (def->parts[i].fieldno != cmp_arr[k].p[i * 2] ||
			    def->parts[i].type != cmp_arr[k].p[i * 2 + 1])
				break;
		if (i == def->part_count && cmp_arr[k].p[i * 2] == UINT32_MAX) {
			cmp = cmp_arr[k].f;
			break;
		}
	}
	for (uint32_t k = 0; k < lengthof(cmp_wk_arr); k++) {
		uint32_t i = 0;
		for (; i < def->part_count; i++) {
			if (def->parts[i].fieldno != cmp_wk_arr[k].p[i * 2] ||
			    def->parts[i].type != cmp_wk_arr[k].p[i * 2 + 1])
				break;
		}
		if (i == def->part_count) {
			cmp_wk = cmp_wk_arr[k].f;
			break;
		}
	}
	if (cmp == NULL) {
		cmp = is_sequential ?
			tuple_compare_sequential<false, false, false> :
			tuple_compare_slowpath<false, false, false,
					       false, false>;
	}
	if (cmp_wk == NULL) {
		cmp_wk = is_sequential ?
			tuple_compare_with_key_sequential<false, false, false> :
			tuple_compare_with_key_slowpath<false, false, false,
							false, false>;
	}

	def->tuple_compare = cmp;
	def->tuple_compare_with_key = cmp_wk;
}

template<bool is_nullable, bool has_optional_parts, bool has_desc_parts>
static void
key_def_set_compare_func_plain(struct key_def *def)
{
	assert(!def->has_json_paths);
	if (key_def_is_sequential(def)) {
		def->tuple_compare = tuple_compare_sequential
			<is_nullable, has_optional_parts, has_desc_parts>;
		def->tuple_compare_with_key = tuple_compare_with_key_sequential
			<is_nullable, has_optional_parts, has_desc_parts>;
	} else {
		def->tuple_compare = tuple_compare_slowpath
				    <is_nullable, has_optional_parts,
				     false, false, has_desc_parts>;
		def->tuple_compare_with_key = tuple_compare_with_key_slowpath
					     <is_nullable, has_optional_parts,
					      false, false, has_desc_parts>;
	}
}

/* Proxy-template. */
template<bool is_nullable, bool has_optional_parts>
static void
key_def_set_compare_func_plain(struct key_def *def)
{
	assert(!def->has_json_paths);
	if (key_def_has_desc_parts(def))
		key_def_set_compare_func_plain
			<is_nullable, has_optional_parts, true>(def);
	else
		key_def_set_compare_func_plain
			<is_nullable, has_optional_parts, false>(def);
}

template<bool is_nullable, bool has_optional_parts, bool has_desc_parts>
static void
key_def_set_compare_func_json(struct key_def *def)
{
	if (def->is_multikey) {
		def->tuple_compare = tuple_compare_slowpath
				    <is_nullable, has_optional_parts,
				     true, true, has_desc_parts>;
		def->tuple_compare_with_key = tuple_compare_with_key_slowpath
					     <is_nullable, has_optional_parts,
					      true, true, has_desc_parts>;
	} else {
		def->tuple_compare = tuple_compare_slowpath
				    <is_nullable, has_optional_parts,
				     true, false, has_desc_parts>;
		def->tuple_compare_with_key = tuple_compare_with_key_slowpath
					     <is_nullable, has_optional_parts,
					      true, false, has_desc_parts>;
	}
}

/* Proxy-template. */
template<bool is_nullable, bool has_optional_parts>
static void
key_def_set_compare_func_json(struct key_def *def)
{
	assert(def->has_json_paths);
	if (key_def_has_desc_parts(def))
		key_def_set_compare_func_json
			<is_nullable, has_optional_parts, true>(def);
	else
		key_def_set_compare_func_json
			<is_nullable, has_optional_parts, false>(def);
}

/* Forced non-required comment. */
template<bool is_nullable, bool has_desc_parts>
static void
key_def_set_compare_func_of_func_index(struct key_def *def)
{
	assert(def->for_func_index);
	def->tuple_compare = func_index_compare<is_nullable, has_desc_parts>;
	def->tuple_compare_with_key = func_index_compare_with_key
				     <is_nullable, has_desc_parts>;
}

/* Proxy-template. */
template<bool is_nullable>
static void
key_def_set_compare_func_of_func_index(struct key_def *def)
{
	assert(def->for_func_index);
	if (key_def_has_desc_parts(def))
		key_def_set_compare_func_of_func_index<is_nullable, true>(def);
	else
		key_def_set_compare_func_of_func_index<is_nullable, false>(def);
}

void
key_def_set_compare_func(struct key_def *def)
{
	if (def->for_func_index) {
		if (def->is_nullable)
			key_def_set_compare_func_of_func_index<true>(def);
		else
			key_def_set_compare_func_of_func_index<false>(def);
	} else if (!def->is_nullable && !def->has_json_paths &&
	    !key_def_has_collation(def) && !key_def_has_desc_parts(def)) {
		key_def_set_compare_func_fast(def);
	} else if (!def->has_json_paths) {
		if (def->is_nullable && def->has_optional_parts) {
			key_def_set_compare_func_plain<true, true>(def);
		} else if (def->is_nullable && !def->has_optional_parts) {
			key_def_set_compare_func_plain<true, false>(def);
		} else {
			assert(!def->is_nullable && !def->has_optional_parts);
			key_def_set_compare_func_plain<false, false>(def);
		}
	} else {
		if (def->is_nullable && def->has_optional_parts) {
			key_def_set_compare_func_json<true, true>(def);
		} else if (def->is_nullable && !def->has_optional_parts) {
			key_def_set_compare_func_json<true, false>(def);
		} else {
			assert(!def->is_nullable && !def->has_optional_parts);
			key_def_set_compare_func_json<false, false>(def);
		}
	}
	/*
	 * We are setting compare functions to NULL in case the key_def
	 * contains non-comparable type. Thus in case we later discover
	 * compare function equal to NULL we assume that the key_def
	 * contains incomparable type. It has to be revised if the
	 * new case where we are setting compare functions to NULL
	 * appears.
	 */
	if (key_def_incomparable_type(def) != field_type_MAX) {
		def->tuple_compare = NULL;
		def->tuple_compare_with_key = NULL;
	}
	key_def_set_hint_func(def);
}
