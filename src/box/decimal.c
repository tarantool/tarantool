#include <string.h>
#include "box/decimal.h"
#include "core/decimal.h"
#include "core/mp_decimal.h"

static_assert(BOX_DECIMAL_STRING_BUFFER_SIZE >= DECIMAL_MAX_STR_LEN,
	      "API buffer size should be not less than implementation");

#define BOX_DECIMAL_UNARY_OP(op)					\
box_decimal_t *								\
box_decimal_##op(box_decimal_t *res, const box_decimal_t *dec)		\
{									\
	return (box_decimal_t *)decimal_##op((decimal_t *)res,		\
					     (const decimal_t *)dec);	\
}

#define BOX_DECIMAL_BINARY_OP(op)					\
box_decimal_t *								\
box_decimal_##op(box_decimal_t *res, const box_decimal_t *lhs,		\
		 const box_decimal_t *rhs)				\
{									\
	return (box_decimal_t *)decimal_##op((decimal_t *)res,		\
					     (const decimal_t *)lhs,	\
					     (const decimal_t *)rhs);	\
}

/* {{{ decimal properties */

int
box_decimal_precision(const box_decimal_t *dec)
{
	return decimal_precision((const decimal_t *)dec);
}

int
box_decimal_scale(const box_decimal_t *dec)
{
	return decimal_scale((const decimal_t *)dec);
}

bool
box_decimal_is_int(const box_decimal_t *dec)
{
	return decimal_is_int((const decimal_t *)dec);
}

bool
box_decimal_is_neg(const box_decimal_t *dec)
{
	return decimal_is_neg((const decimal_t *)dec);
}

/* }}} decimal properties */

/* {{{ decimal constructors */

box_decimal_t *
box_decimal_zero(box_decimal_t *dec)
{
	return (box_decimal_t *)decimal_zero((decimal_t *)dec);
}

box_decimal_t *
box_decimal_from_string(box_decimal_t *dec, const char *str)
{
	return (box_decimal_t *)decimal_from_string((decimal_t *)dec, str);
}

box_decimal_t *
box_decimal_from_double(box_decimal_t *dec, double d)
{
	return (box_decimal_t *)decimal_from_double((decimal_t *)dec, d);
}

box_decimal_t *
box_decimal_from_int64(box_decimal_t *dec, int64_t num)
{
	return (box_decimal_t *)decimal_from_int64((decimal_t *)dec, num);
}

box_decimal_t *
box_decimal_from_uint64(box_decimal_t *dec, uint64_t num)
{
	return (box_decimal_t *)decimal_from_uint64((decimal_t *)dec, num);
}

box_decimal_t *
box_decimal_copy(box_decimal_t *dest, const box_decimal_t *src)
{
	/*
	 * Copy only known part of the value. The rest of the
	 * storage may be unallocated (if we got it from
	 * tarantool).
	 */
	memcpy(dest, src, sizeof(decimal_t));
	return dest;
}

/* }}} decimal constructors */

/* {{{ decimal conversions */

void
box_decimal_to_string(const box_decimal_t *dec, char *buf)
{
	decimal_to_string((const decimal_t *)dec, buf);
}

const box_decimal_t *
box_decimal_to_int64(const box_decimal_t *dec, int64_t *num)
{
	return (const box_decimal_t *)decimal_to_int64(
		(const decimal_t *)dec, num);
}

const box_decimal_t *
box_decimal_to_uint64(const box_decimal_t *dec, uint64_t *num)
{
	return (const box_decimal_t *)decimal_to_uint64(
		(const decimal_t *)dec, num);
}

/* }}} decimal conversions */

/* {{{ decimal rounding */

box_decimal_t *
box_decimal_round(box_decimal_t *dec, int scale)
{
	return (box_decimal_t *)decimal_round((decimal_t *)dec, scale);
}

box_decimal_t *
box_decimal_floor(box_decimal_t *dec, int scale)
{
	return (box_decimal_t *)decimal_floor((decimal_t *)dec, scale);
}

box_decimal_t *
box_decimal_trim(box_decimal_t *dec)
{
	return (box_decimal_t *)decimal_trim((decimal_t *)dec);
}

box_decimal_t *
box_decimal_rescale(box_decimal_t *dec, int scale)
{
	return (box_decimal_t *)decimal_rescale((decimal_t *)dec, scale);
}

/* }}} decimal rounding */

/* {{{ decimal arithmetic */

int
box_decimal_compare(const box_decimal_t *lhs, const box_decimal_t *rhs)
{
	return decimal_compare((const decimal_t *)lhs, (const decimal_t *)rhs);
}

BOX_DECIMAL_UNARY_OP(abs)
BOX_DECIMAL_UNARY_OP(minus)
BOX_DECIMAL_BINARY_OP(add)
BOX_DECIMAL_BINARY_OP(sub)
BOX_DECIMAL_BINARY_OP(mul)
BOX_DECIMAL_BINARY_OP(div)
BOX_DECIMAL_BINARY_OP(remainder)

/* }}} decimal arithmetic */

/* {{{ decimal math functions */

BOX_DECIMAL_UNARY_OP(log10)
BOX_DECIMAL_UNARY_OP(ln)
BOX_DECIMAL_BINARY_OP(pow)
BOX_DECIMAL_UNARY_OP(exp)
BOX_DECIMAL_UNARY_OP(sqrt)

/* }}} decimal math functions */

/* {{{ decimal encoding to/decoding from msgpack */

uint32_t
box_decimal_mp_sizeof(const box_decimal_t *dec)
{
	return mp_sizeof_decimal((const decimal_t *)dec);
}

char *
box_decimal_mp_encode(const box_decimal_t *dec, char *data)
{
	return mp_encode_decimal(data, (const decimal_t *)dec);
}

box_decimal_t *
box_decimal_mp_decode(box_decimal_t *dec, const char **data)
{
	return (box_decimal_t *)mp_decode_decimal(data, (decimal_t *)dec);
}

box_decimal_t *
box_decimal_mp_decode_data(box_decimal_t *dec, const char **data,
			   uint32_t size)
{
	return (box_decimal_t *)decimal_unpack(data, size, (decimal_t *)dec);
}

/* }}} decimal encoding to/decoding from msgpack */
