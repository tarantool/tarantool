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
#include "coll/coll.h"
#include "trivia/util.h" /* NOINLINE */
#include <math.h>
#include "lib/core/decimal.h"
#include "lib/core/mp_decimal.h"
#include "uuid/mp_uuid.h"
#include "lib/core/mp_extension_types.h"

/* {{{ tuple_compare */

/**
 * Compare two tuple hints.
 *
 * Returns:
 *
 *   -1 if the first tuple is less than the second tuple
 *   +1 if the first tuple is greater than the second tuple
 *    0 if the first tuple may be less than, equal to, or
 *      greater than the second tuple, and a full tuple
 *      comparison is needed to determine the order.
 */
static inline int
hint_cmp(hint_t hint_a, hint_t hint_b)
{
	if (hint_a != HINT_NONE && hint_b != HINT_NONE && hint_a != hint_b)
		return hint_a < hint_b ? -1 : 1;
	return 0;
}

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
	MP_CLASS_UUID,
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

static int
mp_compare_bool(const char *field_a, const char *field_b)
{
	int a_val = mp_decode_bool(&field_a);
	int b_val = mp_decode_bool(&field_b);
	return COMPARE_RESULT(a_val, b_val);
}

static int
mp_compare_double(const char *field_a, const char *field_b)
{
	double a_val = mp_decode_double(&field_a);
	double b_val = mp_decode_double(&field_b);
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

static int
mp_compare_decimal_any_number(decimal_t *lhs, const char *rhs,
			      enum mp_type rhs_type, int k)
{
	decimal_t rhs_dec;
	switch (rhs_type) {
	case MP_FLOAT:
	{
		double d = mp_decode_float(&rhs);
		decimal_from_double(&rhs_dec, d);
		break;
	}
	case MP_DOUBLE:
	{
		double d = mp_decode_double(&rhs);
		decimal_from_double(&rhs_dec, d);
		break;
	}
	case MP_INT:
	{
		int64_t num = mp_decode_int(&rhs);
		decimal_from_int64(&rhs_dec, num);
		break;
	}
	case MP_UINT:
	{
		uint64_t num = mp_decode_uint(&rhs);
		decimal_from_uint64(&rhs_dec, num);
		break;
	}
	case MP_EXT:
	{
		int8_t ext_type;
		uint32_t len = mp_decode_extl(&rhs, &ext_type);
		switch (ext_type) {
		case MP_DECIMAL:
			decimal_unpack(&rhs, len, &rhs_dec);
			break;
		default:
			unreachable();
		}
		break;
	}
	default:
		unreachable();
	}
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

typedef int (*mp_compare_f)(const char *, const char *);
static mp_compare_f mp_class_comparators[] = {
	/* .MP_CLASS_NIL    = */ NULL,
	/* .MP_CLASS_BOOL   = */ mp_compare_bool,
	/* .MP_CLASS_NUMBER = */ mp_compare_number,
	/* .MP_CLASS_STR    = */ mp_compare_str,
	/* .MP_CLASS_BIN    = */ mp_compare_bin,
	/* .MP_CLASS_UUID   = */ NULL,
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
		return mp_compare_integer_with_type(field_a,
						    mp_typeof(*field_a),
						    field_b,
						    mp_typeof(*field_b));
	case FIELD_TYPE_NUMBER:
		return mp_compare_number(field_a, field_b);
	case FIELD_TYPE_DOUBLE:
		return mp_compare_double(field_a, field_b);
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
		return mp_compare_uint(field_a, field_b);
	case FIELD_TYPE_STRING:
		return coll != NULL ?
		       mp_compare_str_coll(field_a, field_b, coll) :
		       mp_compare_str(field_a, field_b);
	case FIELD_TYPE_INTEGER:
		return mp_compare_integer_with_type(field_a, a_type,
						    field_b, b_type);
	case FIELD_TYPE_NUMBER:
		return mp_compare_number_with_type(field_a, a_type,
						   field_b, b_type);
	case FIELD_TYPE_DOUBLE:
		return mp_compare_double(field_a, field_b);
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
	default:
		unreachable();
		return 0;
	}
}

template<bool is_nullable, bool has_optional_parts, bool has_json_paths,
	 bool is_multikey>
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
	struct key_part *part = key_def->parts;
	const char *tuple_a_raw = tuple_data(tuple_a);
	const char *tuple_b_raw = tuple_data(tuple_b);
	if (key_def->part_count == 1 && part->fieldno == 0 &&
	    (!has_json_paths || part->path == NULL)) {
		/*
		 * First field can not be optional - empty tuples
		 * can not exist.
		 */
		assert(!has_optional_parts);
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
		return tuple_compare_field_with_type(tuple_a_raw,  a_type,
						     tuple_b_raw, b_type,
						     part->type, part->coll);
	}

	bool was_null_met = false;
	struct tuple_format *format_a = tuple_format(tuple_a);
	struct tuple_format *format_b = tuple_format(tuple_b);
	const uint8_t *field_map_a = tuple_field_map(tuple_a);
	const uint8_t *field_map_b = tuple_field_map(tuple_b);
	struct key_part *end;
	const char *field_a, *field_b;
	enum mp_type a_type, b_type;
	if (is_nullable)
		end = part + key_def->unique_part_count;
	else
		end = part + key_def->part_count;

	for (; part < end; part++) {
		if (is_multikey) {
			field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
							  field_map_a, part,
							  (int)tuple_a_hint,
							  tuple_is_tiny(tuple_a));
			field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
							  field_map_b, part,
							  (int)tuple_b_hint,
							  tuple_is_tiny(tuple_b));
		} else if (has_json_paths) {
			field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
							  field_map_a, part,
							  MULTIKEY_NONE,
							  tuple_is_tiny(tuple_a));
			field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
							  field_map_b, part,
							  MULTIKEY_NONE,
							  tuple_is_tiny(tuple_b));
		} else {
			field_a = tuple_field_raw(format_a, tuple_a_raw,
						  field_map_a, part->fieldno,
						  tuple_is_tiny(tuple_a));
			field_b = tuple_field_raw(format_b, tuple_b_raw,
						  field_map_b, part->fieldno,
						  tuple_is_tiny(tuple_b));
		}
		assert(has_optional_parts ||
		       (field_a != NULL && field_b != NULL));
		if (! is_nullable) {
			rc = tuple_compare_field(field_a, field_b, part->type,
						 part->coll);
			if (rc != 0)
				return rc;
			else
				continue;
		}
		if (has_optional_parts) {
			a_type = field_a != NULL ? mp_typeof(*field_a) : MP_NIL;
			b_type = field_b != NULL ? mp_typeof(*field_b) : MP_NIL;
		} else {
			a_type = mp_typeof(*field_a);
			b_type = mp_typeof(*field_b);
		}
		if (a_type == MP_NIL) {
			if (b_type != MP_NIL)
				return -1;
			was_null_met = true;
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			rc = tuple_compare_field_with_type(field_a, a_type,
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
		if (is_multikey) {
			field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
							  field_map_a, part,
							  (int)tuple_a_hint,
							  tuple_is_tiny(tuple_a));
			field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
							  field_map_b, part,
							  (int)tuple_b_hint,
							  tuple_is_tiny(tuple_b));
		} else if (has_json_paths) {
			field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
							  field_map_a, part,
							  MULTIKEY_NONE,
							  tuple_is_tiny(tuple_a));
			field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
							  field_map_b, part,
							  MULTIKEY_NONE,
							  tuple_is_tiny(tuple_b));
		} else {
			field_a = tuple_field_raw(format_a, tuple_a_raw,
						  field_map_a, part->fieldno,
						  tuple_is_tiny(tuple_a));
			field_b = tuple_field_raw(format_b, tuple_b_raw,
						  field_map_b, part->fieldno,
						  tuple_is_tiny(tuple_b));
		}
		/*
		 * Extended parts are primary, and they can not
		 * be absent or be NULLs.
		 */
		assert(field_a != NULL && field_b != NULL);
		rc = tuple_compare_field(field_a, field_b, part->type,
					 part->coll);
		if (rc != 0)
			return rc;
	}
	return 0;
}

template<bool is_nullable, bool has_optional_parts, bool has_json_paths,
	 bool is_multikey>
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
	const uint8_t *field_map = tuple_field_map(tuple);
	enum mp_type a_type, b_type;
	if (likely(part_count == 1)) {
		const char *field;
		if (is_multikey) {
			field = tuple_field_raw_by_part(format, tuple_raw,
							field_map, part,
							(int)tuple_hint,
							tuple_is_tiny(tuple));
		} else if (has_json_paths) {
			field = tuple_field_raw_by_part(format, tuple_raw,
							field_map, part,
							MULTIKEY_NONE,
							tuple_is_tiny(tuple));
		} else {
			field = tuple_field_raw(format, tuple_raw, field_map,
						part->fieldno,
						tuple_is_tiny(tuple));
		}
		if (! is_nullable) {
			return tuple_compare_field(field, key, part->type,
						   part->coll);
		}
		if (has_optional_parts)
			a_type = field != NULL ? mp_typeof(*field) : MP_NIL;
		else
			a_type = mp_typeof(*field);
		b_type = mp_typeof(*key);
		if (a_type == MP_NIL) {
			return b_type == MP_NIL ? 0 : -1;
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			return tuple_compare_field_with_type(field, a_type, key,
							     b_type, part->type,
							     part->coll);
		}
	}

	struct key_part *end = part + part_count;
	for (; part < end; ++part, mp_next(&key)) {
		const char *field;
		if (is_multikey) {
			field = tuple_field_raw_by_part(format, tuple_raw,
							field_map, part,
							(int)tuple_hint,
							tuple_is_tiny(tuple));
		} else if (has_json_paths) {
			field = tuple_field_raw_by_part(format, tuple_raw,
							field_map, part,
							MULTIKEY_NONE,
							tuple_is_tiny(tuple));
		} else {
			field = tuple_field_raw(format, tuple_raw, field_map,
						part->fieldno, tuple_is_tiny(tuple));
		}
		if (! is_nullable) {
			rc = tuple_compare_field(field, key, part->type,
						 part->coll);
			if (rc != 0)
				return rc;
			else
				continue;
		}
		if (has_optional_parts)
			a_type = field != NULL ? mp_typeof(*field) : MP_NIL;
		else
			a_type = mp_typeof(*field);
		b_type = mp_typeof(*key);
		if (a_type == MP_NIL) {
			if (b_type != MP_NIL)
				return -1;
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			rc = tuple_compare_field_with_type(field, a_type, key,
							   b_type, part->type,
							   part->coll);
			if (rc != 0)
				return rc;
		}
	}
	return 0;
}

template<bool is_nullable>
static inline int
key_compare_parts(const char *key_a, const char *key_b, uint32_t part_count,
		  struct key_def *key_def)
{
	assert(is_nullable == key_def->is_nullable);
	assert((key_a != NULL && key_b != NULL) || part_count == 0);
	struct key_part *part = key_def->parts;
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
			return tuple_compare_field_with_type(key_a, a_type,
							     key_b, b_type,
							     part->type,
							     part->coll);
		}
	}

	struct key_part *end = part + part_count;
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
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			rc = tuple_compare_field_with_type(key_a, a_type, key_b,
							   b_type, part->type,
							   part->coll);
			if (rc != 0)
				return rc;
		}
	}
	return 0;
}

template<bool is_nullable, bool has_optional_parts>
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
	rc = key_compare_parts<is_nullable>(tuple_key, key, cmp_part_count,
					    key_def);
	if (!has_optional_parts || rc != 0)
		return rc;
	/*
	 * If some tuple indexed fields are absent, then check
	 * corresponding key fields to be equal to NULL.
	 */
	if (field_count < part_count) {
		/*
		 * Key's and tuple's first field_count fields are
		 * equal, and their bsize too.
		 */
		key += tuple_bsize(tuple) - mp_sizeof_array(field_count);
		for (uint32_t i = field_count; i < part_count;
		     ++i, mp_next(&key)) {
			if (mp_typeof(*key) != MP_NIL)
				return -1;
		}
	}
	return 0;
}

int
key_compare(const char *key_a, hint_t key_a_hint,
	    const char *key_b, hint_t key_b_hint, struct key_def *key_def)
{
	int rc = hint_cmp(key_a_hint, key_b_hint);
	if (rc != 0)
		return rc;
	uint32_t part_count_a = mp_decode_array(&key_a);
	uint32_t part_count_b = mp_decode_array(&key_b);
	assert(part_count_a <= key_def->part_count);
	assert(part_count_b <= key_def->part_count);
	uint32_t part_count = MIN(part_count_a, part_count_b);
	assert(part_count <= key_def->part_count);
	if (! key_def->is_nullable) {
		return key_compare_parts<false>(key_a, key_b, part_count,
						key_def);
	} else {
		return key_compare_parts<true>(key_a, key_b, part_count,
					       key_def);
	}
}

template <bool is_nullable, bool has_optional_parts>
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
		return key_compare_parts<false>(key_a, key_b,
						key_def->part_count, key_def);
	}
	bool was_null_met = false;
	struct key_part *part = key_def->parts;
	struct key_part *end = part + key_def->unique_part_count;
	uint32_t i = 0;
	for (; part < end; ++part, ++i) {
		enum mp_type a_type, b_type;
		if (has_optional_parts) {
			a_type = i >= fc_a ? MP_NIL : mp_typeof(*key_a);
			b_type = i >= fc_b ? MP_NIL : mp_typeof(*key_b);
		} else {
			a_type = mp_typeof(*key_a);
			b_type = mp_typeof(*key_b);
		}
		if (a_type == MP_NIL) {
			if (b_type != MP_NIL)
				return -1;
			was_null_met = true;
		} else if (b_type == MP_NIL) {
			return 1;
		} else {
			rc = tuple_compare_field_with_type(key_a, a_type, key_b,
							   b_type, part->type,
							   part->coll);
			if (rc != 0)
				return rc;
		}
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
		rc = tuple_compare_field(key_a, key_b, part->type,
					 part->coll);
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
						  IDX2, tuple_is_tiny(tuple_a));
			field_b = tuple_field_raw(format_b, tuple_data(tuple_b),
						  tuple_field_map(tuple_b),
						  IDX2, tuple_is_tiny(tuple_b));
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
					  tuple_field_map(tuple_a), IDX,
					  tuple_is_tiny(tuple_a));
		field_b = tuple_field_raw(format_b, tuple_data(tuple_b),
					  tuple_field_map(tuple_b), IDX,
					  tuple_is_tiny(tuple_b));
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
						tuple_field_map(tuple), IDX2,
						tuple_is_tiny(tuple));
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
						    IDX, tuple_is_tiny(tuple));
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

/**
 * A functional index tuple compare.
 * tuple_a_hint and tuple_b_hint are expected to be valid
 * pointers to functional key memory. These keys have been already
 * validated and are represented as a MsgPack array with exactly
 * func_index_part_count parts. The cmp_def has part_count > func_index_part_count,
 * since it was  produced by key_def_merge() of the functional key part
 * and the primary key. So its tail parts are taken from primary
 * index key definition.
 */
template<bool is_nullable>
static inline int
func_index_compare(struct tuple *tuple_a, hint_t tuple_a_hint,
		   struct tuple *tuple_b, hint_t tuple_b_hint,
		   struct key_def *cmp_def)
{
	assert(cmp_def->for_func_index);
	assert(is_nullable == cmp_def->is_nullable);

	const char *key_a = (const char *)tuple_a_hint;
	const char *key_b = (const char *)tuple_b_hint;
	assert(mp_typeof(*key_a) == MP_ARRAY);
	uint32_t part_count_a = mp_decode_array(&key_a);
	assert(mp_typeof(*key_b) == MP_ARRAY);
	uint32_t part_count_b = mp_decode_array(&key_b);

	uint32_t key_part_count = MIN(part_count_a, part_count_b);
	int rc = key_compare_parts<is_nullable>(key_a, key_b, key_part_count,
						cmp_def);
	if (rc != 0)
		return rc;
	/*
	 * Primary index definition key compare.
	 * It cannot contain nullable parts so the code is
	 * simplified correspondingly.
	 */
	const char *tuple_a_raw = tuple_data(tuple_a);
	const char *tuple_b_raw = tuple_data(tuple_b);
	struct tuple_format *format_a = tuple_format(tuple_a);
	struct tuple_format *format_b = tuple_format(tuple_b);
	const uint8_t *field_map_a = tuple_field_map(tuple_a);
	const uint8_t *field_map_b = tuple_field_map(tuple_b);
	const char *field_a, *field_b;
	for (uint32_t i = key_part_count; i < cmp_def->part_count; i++) {
		struct key_part *part = &cmp_def->parts[i];
		field_a = tuple_field_raw_by_part(format_a, tuple_a_raw,
						  field_map_a, part,
						  MULTIKEY_NONE,
						  tuple_is_tiny(tuple_a));
		field_b = tuple_field_raw_by_part(format_b, tuple_b_raw,
						  field_map_b, part,
						  MULTIKEY_NONE,
						  tuple_is_tiny(tuple_b));
		assert(field_a != NULL && field_b != NULL);
		rc = tuple_compare_field(field_a, field_b, part->type,
					 part->coll);
		if (rc != 0)
			return rc;
		else
			continue;
	}
	return 0;
}

/**
 * A functional index key compare.
 * tuple_hint is expected to be a valid pointer to
 * functional key memory and is compared with the given key by
 * using the functional index key definition.
 */
template<bool is_nullable>
static inline int
func_index_compare_with_key(struct tuple *tuple, hint_t tuple_hint,
			    const char *key, uint32_t part_count,
			    hint_t key_hint, struct key_def *key_def)
{
	(void)tuple; (void)key_hint;
	assert(key_def->for_func_index);
	assert(is_nullable == key_def->is_nullable);
	const char *tuple_key = (const char *)tuple_hint;
	assert(mp_typeof(*tuple_key) == MP_ARRAY);

	uint32_t tuple_key_count = mp_decode_array(&tuple_key);
	part_count = MIN(part_count, tuple_key_count);
	part_count = MIN(part_count, key_def->part_count);
	return key_compare_parts<is_nullable>(tuple_key, key, part_count,
					      key_def);
}

#undef KEY_COMPARATOR

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
field_hint_double(const char *field)
{
	assert(mp_typeof(*field) == MP_DOUBLE);
	return hint_double(mp_decode_double(&field));
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
		return field_hint_unsigned(field);
	case FIELD_TYPE_INTEGER:
		return field_hint_integer(field);
	case FIELD_TYPE_NUMBER:
		return field_hint_number(field);
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
	default:
		unreachable();
	}
	return HINT_NONE;
}

template <enum field_type type, bool is_nullable>
static hint_t
key_hint(const char *key, uint32_t part_count, struct key_def *key_def)
{
	assert(!key_def->is_multikey);
	if (part_count == 0)
		return HINT_NONE;
	return field_hint<type, is_nullable>(key, key_def->parts->coll);
}

template <enum field_type type, bool is_nullable>
static hint_t
tuple_hint(struct tuple *tuple, struct key_def *key_def)
{
	assert(!key_def->is_multikey);
	const char *field = tuple_field_by_part(tuple, key_def->parts,
						MULTIKEY_NONE);
	if (is_nullable && field == NULL)
		return hint_nil();
	return field_hint<type, is_nullable>(field, key_def->parts->coll);
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
key_hint_stub(struct tuple *tuple, struct key_def *key_def)
{
	(void) tuple;
	(void) key_def;
	unreachable();
	return HINT_NONE;
}

template<enum field_type type, bool is_nullable>
static void
key_def_set_hint_func(struct key_def *def)
{
	def->key_hint = key_hint<type, is_nullable>;
	def->tuple_hint = tuple_hint<type, is_nullable>;
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
		def->tuple_hint = key_hint_stub;
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
			tuple_compare_sequential<false, false> :
			tuple_compare_slowpath<false, false, false, false>;
	}
	if (cmp_wk == NULL) {
		cmp_wk = is_sequential ?
			tuple_compare_with_key_sequential<false, false> :
			tuple_compare_with_key_slowpath<false, false,
							false, false>;
	}

	def->tuple_compare = cmp;
	def->tuple_compare_with_key = cmp_wk;
}

template<bool is_nullable, bool has_optional_parts>
static void
key_def_set_compare_func_plain(struct key_def *def)
{
	assert(!def->has_json_paths);
	if (key_def_is_sequential(def)) {
		def->tuple_compare = tuple_compare_sequential
					<is_nullable, has_optional_parts>;
		def->tuple_compare_with_key = tuple_compare_with_key_sequential
					<is_nullable, has_optional_parts>;
	} else {
		def->tuple_compare = tuple_compare_slowpath
				<is_nullable, has_optional_parts, false, false>;
		def->tuple_compare_with_key = tuple_compare_with_key_slowpath
				<is_nullable, has_optional_parts, false, false>;
	}
}

template<bool is_nullable, bool has_optional_parts>
static void
key_def_set_compare_func_json(struct key_def *def)
{
	assert(def->has_json_paths);
	if (def->is_multikey) {
		def->tuple_compare = tuple_compare_slowpath
				<is_nullable, has_optional_parts, true, true>;
		def->tuple_compare_with_key = tuple_compare_with_key_slowpath
				<is_nullable, has_optional_parts, true, true>;
	} else {
		def->tuple_compare = tuple_compare_slowpath
				<is_nullable, has_optional_parts, true, false>;
		def->tuple_compare_with_key = tuple_compare_with_key_slowpath
				<is_nullable, has_optional_parts, true, false>;
	}
}

template<bool is_nullable>
static void
key_def_set_compare_func_for_func_index(struct key_def *def)
{
	assert(def->for_func_index);
	def->tuple_compare = func_index_compare<is_nullable>;
	def->tuple_compare_with_key = func_index_compare_with_key<is_nullable>;
}

void
key_def_set_compare_func(struct key_def *def)
{
	if (def->for_func_index) {
		if (def->is_nullable)
			key_def_set_compare_func_for_func_index<true>(def);
		else
			key_def_set_compare_func_for_func_index<false>(def);
	} else if (!key_def_has_collation(def) &&
	    !def->is_nullable && !def->has_json_paths) {
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
