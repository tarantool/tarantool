/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

/* {{{ decimal structure and constants */

/** See @ref box_decimal_t. */
struct box_decimal {
	/**
	 * Decimal data.
	 *
	 * The format is unspecified and can be changed.
	 *
	 * The tail of the array may be in an unallocated memory.
	 * Don't access this array directly. Use box_decimal_*()
	 * functions instead.
	 */
	uint64_t internal[8];
};

/**
 * Storage for a decimal number.
 *
 * This type is large enough to hold a decimal value. The primary
 * usage is to allocate a decimal on the stack and pass it to a
 * box_decimal_*() function.
 *
 * Take it as opacue structure with ability to allocate a value
 * on the stack.
 *
 * Use box_decimal_copy() to copy the value, don't use memcpy().
 * The real data can be smaller than this type. Moreover,
 * tarantool may allocate less bytes for @ref box_decimal_t value,
 * so direct read/write of the structure may lead to access to an
 * unallocated memory.
 *
 * The alignment of the structure is not less than alignment of
 * decimal values allocated by tarantool. It can be larger and it
 * is another reason to don't access the structure content
 * directly.
 *
 * Last, but not least: the data format is unspecified and can be
 * changed. Modules and applications that aims to be compatible
 * with future tarantool versions must not lean on particular data
 * representation within the structure.
 */
typedef struct box_decimal box_decimal_t;

/**
 * Buffer of this size is enough to hold any
 * box_decimal_to_string() result.
 */
#define BOX_DECIMAL_STRING_BUFFER_SIZE 128

/* }}} decimal structure and constants */

/* {{{ decimal properties */

/**
 * Decimal precision.
 *
 * It is equivalent to amount of decimal digits in the string
 * representation (not counting minus, decimal separator and the
 * leading zero in 0.ddd...ddd number, but counting trailing zeros
 * if any).
 *
 * @param dec decimal number
 * @return precision
 */
API_EXPORT int
box_decimal_precision(const box_decimal_t *dec);

/**
 * Decimal scale.
 *
 * It is equivalent to amount of digits after the decimal
 * separator.
 *
 * @param dec decimal number
 * @return scale
 */
API_EXPORT int
box_decimal_scale(const box_decimal_t *dec);

/**
 * Whether the fractional part of a decimal number is 0.
 *
 * @param dec decimal number
 * @return true if the fractional part is zero
 * @return false otherwise
 */
API_EXPORT bool
box_decimal_is_int(const box_decimal_t *dec);

/**
 * Whether a decimal number is negative.
 *
 * @param dec decimal number
 * @return true if the decimal is less than zero
 * @return false otherwise
 */
API_EXPORT bool
box_decimal_is_neg(const box_decimal_t *dec);

/* }}} decimal properties */

/* {{{ decimal constructors */

/**
 * Initialize a zero decimal number.
 *
 * @param dec where to store the decimal result
 * @return the decimal result
 */
API_EXPORT box_decimal_t *
box_decimal_zero(box_decimal_t *dec);

/**
 * Initialize a decimal with a value from a string.
 *
 * The fractional part may be rounded if a maximum precision is
 * reached.
 *
 * @param dec where to store the decimal result
 * @param str source string value
 * @return NULL if the string is invalid or the number is too big
 * @return decimal result
 */
API_EXPORT box_decimal_t *
box_decimal_from_string(box_decimal_t *dec, const char *str);

/**
 * Initialize a decimal from double.
 *
 * @param dec where to store the decimal result
 * @param d source double value
 * @return NULL if the double is NaN, Infinity or greater than a
 *         maximum precision for decimal values
 * @return decimal result
 */
API_EXPORT box_decimal_t *
box_decimal_from_double(box_decimal_t *dec, double d);

/**
 * Initialize a decimal with a signed integer value.
 *
 * @param dec where to store the decimal result
 * @param num source integer value (signed)
 * @return decimal result
 */
API_EXPORT box_decimal_t *
box_decimal_from_int64(box_decimal_t *dec, int64_t num);

/**
 * Initialize a decimal with a unsigned integer value.
 *
 * @param dec where to store the decimal result
 * @param num source integer value (unsigned)
 * @return decimal result
 */
API_EXPORT box_decimal_t *
box_decimal_from_uint64(box_decimal_t *dec, uint64_t num);

/**
 * Copy decimal value from one storage to another.
 *
 * Use this function where you would use memcpy() if the precise
 * data size would be known.
 *
 * The memory areas must not overlap.
 *
 * @param dest where to store the copy
 * @param src from where to copy
 * @return the copy (@a dest)
 */
API_EXPORT box_decimal_t *
box_decimal_copy(box_decimal_t *dest, const box_decimal_t *src);

/* }}} decimal constructors */

/* {{{ decimal conversions */

/**
 * Write the decimal as a string into the passed buffer.
 *
 * The buffer must have at least
 * @ref BOX_DECIMAL_STRING_BUFFER_SIZE bytes.
 *
 * @param dec source decimal number
 * @param buf where to write @a dec string representation
 */
API_EXPORT void
box_decimal_to_string(const box_decimal_t *dec, char *buf);

/**
 * Convert a given decimal to int64_t.
 *
 * @param dec source decimal number
 * @param num where to store @a dec integer representation
 * @return NULL if the value does not fit into int64_t
 * @return @a dec
 */
API_EXPORT const box_decimal_t *
box_decimal_to_int64(const box_decimal_t *dec, int64_t *num);

/**
 * Convert a given decimal to uint64_t.
 *
 * @param dec source decimal number
 * @param num where to store @a dec integer representation
 * @return NULL if the value does not fit into uint64_t
 * @return @a dec
 */
API_EXPORT const box_decimal_t *
box_decimal_to_uint64(const box_decimal_t *dec, uint64_t *num);

/* }}} decimal conversions */

/* {{{ decimal rounding */

/**
 * Round to nearest decimal at given @a scale, half goes away from
 * zero.
 *
 * round(-0.66, 1) -> -0.7
 * round(-0.65, 1) -> -0.7
 * round(-0.64, 1) -> -0.6
 *
 * round(0.64, 1) -> 0.6
 * round(0.65, 1) -> 0.7
 * round(0.66, 1) -> 0.7
 *
 * Resulting decimal number has not more than @a scale digits
 * after the decimal point.
 *
 * If @a scale if greater than current @a dec scale, do nothing.
 *
 * @param dec decimal number
 * @param scale target scale
 * @return NULL if @a scale is out of supported range
 * @return @a dec (changed)
 */
API_EXPORT box_decimal_t *
box_decimal_round(box_decimal_t *dec, int scale);

/**
 * Apply a floor function to a decimal, i.e. round it towards
 * zero to a decimal with given @a scale.
 *
 * floor(-0.66, 1) -> -0.6
 * floor(-0.65, 1) -> -0.6
 * floor(-0.64, 1) -> -0.6
 *
 * floor(0.64, 1) -> 0.6
 * floor(0.65, 1) -> 0.6
 * floor(0.66, 1) -> 0.6
 *
 * @sa box_decimal_round
 *
 * @param dec decimal number
 * @param scale target scale
 * @return NULL if @a scale is out of supported range
 * @return @a dec (changed)
 */
API_EXPORT box_decimal_t *
box_decimal_floor(box_decimal_t *dec, int scale);

/**
 * Remove trailing zeros from the fractional part of a number.
 *
 * @param dec decimal number
 * @return @a dec (changed)
 */
API_EXPORT box_decimal_t *
box_decimal_trim(box_decimal_t *dec);

/**
 * Set scale of @a dec to @a scale.
 *
 * If @a scale is less than scale(@a dec), round the decimal.
 * Otherwise append a sufficient amount of trailing fractional
 * zeros.
 *
 * @sa box_decimal_round
 * @sa box_decimal_trim
 *
 * @param dec decimal number
 * @param scale target scale
 * @return NULL if scale is out of supported range (less than zero
 *              or too big)
 * @return @a dec (changed)
 */
API_EXPORT box_decimal_t *
box_decimal_rescale(box_decimal_t *dec, int scale);

/* }}} decimal rounding */

/* {{{ decimal arithmetic */

/**
 * Compare two decimal values.
 *
 * @param lhs left hand side operand
 * @param rhs right hand side operand
 * @return -1 if lhs < rhs
 * @return 0 if lhs = rhs
 * @return 1 if lhs > rhs
 */
API_EXPORT int
box_decimal_compare(const box_decimal_t *lhs, const box_decimal_t *rhs);

/**
 * Get the absolute value of a decimal.
 *
 * @a res is set to the absolute value of @a dec.
 *
 * box_decimal_abs(&a, &a) is allowed.
 *
 * @param res where to store the result
 * @param dec decimal operand
 * @return @a res
 */
API_EXPORT box_decimal_t *
box_decimal_abs(box_decimal_t *res, const box_decimal_t *dec);

/**
 * Perform unary minus operation.
 *
 * @a res is set to -dec.
 *
 * @param res where to store the result
 * @param dec decimal operand
 * @return @a res
 */
API_EXPORT box_decimal_t *
box_decimal_minus(box_decimal_t *res, const box_decimal_t *dec);

/**
 * Calculate a sum of two decimal numbers.
 *
 * @param res where to hold the result
 * @param lhs left hand side operand
 * @param rhs right hand side operand
 * @return NULL on an error (an overflow for example)
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_add(box_decimal_t *res, const box_decimal_t *lhs,
		const box_decimal_t *rhs);

/**
 * Subtract one decimal number from another.
 *
 * @param res where to hold the result
 * @param lhs left hand side operand
 * @param rhs right hand side operand
 * @return NULL on an error (an overflow for example)
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_sub(box_decimal_t *res, const box_decimal_t *lhs,
		const box_decimal_t *rhs);

/**
 * Multiply two decimal numbers.
 *
 * @param res where to hold the result
 * @param lhs left hand side operand
 * @param rhs right hand side operand
 * @return NULL on an error (an overflow for example)
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_mul(box_decimal_t *res, const box_decimal_t *lhs,
		const box_decimal_t *rhs);

/**
 * Divide one decimal number on another.
 *
 * @param res where to hold the result
 * @param lhs left hand side operand
 * @param rhs right hand side operand
 * @return NULL on an error
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_div(box_decimal_t *res, const box_decimal_t *lhs,
		const box_decimal_t *rhs);

/**
 * Get the remainder of diviging two decimals.
 *
 * @a res is set to the remainder of dividing @a lhs by @a rhs.
 *
 * @param res where to hold the result
 * @param lhs left hand side operand
 * @param rhs right hand side operand
 * @return NULL on an error
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_remainder(box_decimal_t *res, const box_decimal_t *lhs,
		      const box_decimal_t *rhs);

/* }}} decimal arithmetic */

/* {{{ decimal math functions */

/**
 * Calculate a common logarithm (base 10).
 *
 * @param res where to hold the result
 * @param dec decimal operand
 * @return NULL on an error
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_log10(box_decimal_t *res, const box_decimal_t *dec);

/**
 * Calculate a natural logarithm (base e).
 *
 * @param res where to hold the result
 * @param dec decimal operand
 * @return NULL on an error
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_ln(box_decimal_t *res, const box_decimal_t *dec);

/**
 * Calculate @a lhs raised to the power of @a rhs.
 *
 * @param res where to hold the result
 * @param lhs left hand side operand, base
 * @param rhs right hand side operand, power
 * @return NULL on an error
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_pow(box_decimal_t *res, const box_decimal_t *lhs,
		const box_decimal_t *rhs);

/**
 * Calculate exp(@a dec), i.e. pow(e, @a dec).
 *
 * @param res where to hold the result
 * @param dec decimal operand
 * @return NULL on an error
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_exp(box_decimal_t *res, const box_decimal_t *dec);

/**
 * Calculate a square root.
 *
 * @param res where to hold the result
 * @param dec decimal operand
 * @return NULL on an error
 * @return decimal result (@a res)
 */
API_EXPORT box_decimal_t *
box_decimal_sqrt(box_decimal_t *res, const box_decimal_t *dec);

/* }}} decimal math functions */

/* {{{ decimal encoding to/decoding from msgpack */

/**
 * Calculate exact buffer size needed to store msgpack
 * representation of a decimal.
 *
 * @param dec decimal operand
 * @return the buffer size
 */
uint32_t
box_decimal_mp_sizeof(const box_decimal_t *dec);

/**
 * Encode a decimal as msgpack.
 *
 * @param dec decimal number to encode
 * @param data buffer to write the result
 * @return @a data + box_decimal_mp_sizeof(@a dec)
 */
char *
box_decimal_mp_encode(const box_decimal_t *dec, char *data);

/**
 * Decode a decimal from msgpack @a data.
 *
 * @param dec where to store the decoded decimal
 * @param data pointer to a buffer with the msgpack data
 * @return NULL if the msgpack data does not represent a valid
 *         decimal value
 * @return the decoded decimal
 * @post *data = *data + box_decimal_mp_sizeof(retval)
 */
box_decimal_t *
box_decimal_mp_decode(box_decimal_t *dec, const char **data);

/**
 * Decode a decimal from msgpack @a data without the msgpack
 * extension header.
 *
 * \code
 * box_decimal_mp_decode_data() must be called for this position
 *                                        |
 *                                        v
 * <msgpack type> <size> <extension type> <data>
 * ^
 * |
 * box_decimal_mp_decode() must be called for this position
 * \endcode
 *
 * This function is suitable to finish decoding after calling
 * mp_decode_extl() (from the msgpuck library).
 *
 * @param dec where to store the decoded decimal
 * @param data pointer to a buffer with the msgpack data
 * @param size size of the decimal data in the buffer; this value
 *             is returned by mp_decode_extl()
 * @return NULL if the msgpack data does not represent a valid
 *         decimal value
 * @return the decoded decimal
 * @post *data = *data + @a size
 */
box_decimal_t *
box_decimal_mp_decode_data(box_decimal_t *dec, const char **data,
			   uint32_t size);

/* }}} decimal encoding to/decoding from msgpack */

/** \endcond public */

/*
 * How many bytes are allocated in user's code, how they're
 * aligned.
 *
 * Those values are part of the ABI and so shouldn't vary.
 */
static_assert(sizeof(box_decimal_t) == 64, "sizeof(box_decimal_t) == 64");
static_assert(alignof(box_decimal_t) == 8, "alignof(box_decimal_t) == 8");

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
