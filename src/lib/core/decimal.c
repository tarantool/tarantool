/*
 * Copyright 2019, Tarantool AUTHORS, please see AUTHORS file.
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

#include "decimal.h"
#include <decNumber/decContext.h>
#include <decNumber/decPacked.h>
#include "lib/core/tt_static.h"
#include "lib/msgpuck/msgpuck.h"
#include <stddef.h>
#include <stdlib.h>
#include <float.h> /* DBL_DIG */
#include <math.h> /* isnan(), isinf(). */
#include "trivia/util.h"

#define DECIMAL_ROUNDING DEC_ROUND_HALF_UP

/** A single context for all the arithmetic operations. */
static __thread decContext decimal_context = {
	/* Maximum precision during operations. */
	DECIMAL_MAX_DIGITS,
	/* Maximal adjusted exponent. */
	DEC_MAX_MATH,
	/* Minimal adjusted exponent. */
	-DEC_MAX_MATH,
	/* Rounding mode: .5 rounds away from 0. */
	DECIMAL_ROUNDING,
	/* Turn off signalling for failed operations. */
	0,
	/* Status holding occurred events. Initially empty. */
	0,
	/* Turn off exponent clamping. */
	0
};

/**
 * A finalizer for all the operations.
 * Check the operation context status and empty it,
 * check that result isn't a NaN or infinity.
 *
 * @return NULL if finalization failed.
 *         result pointer otherwise.
 */
static decimal_t *
decimal_check_status(decimal_t *dec, decContext *context)
{
	uint32_t status = decContextGetStatus(context);
	decContextZeroStatus(context);
	/*
	 * Clear warnings. Every value less than 0.1 is
	 * subnormal, DEC_Inexact and DEC_Rounded result
	 * from rounding. DEC_Inexact with DEC_Subnormal
	 * together result in DEC_Underflow. DEC_Clamped
	 * happens after underflow if rounding to zero.
	 */
	status &= ~(uint32_t)(DEC_Inexact | DEC_Rounded | DEC_Underflow |
			      DEC_Subnormal | DEC_Clamped);
	return status || !decNumberIsFinite(dec) ? NULL : dec;
}

int decimal_precision(const decimal_t *dec) {
	return dec->exponent <= 0 ? MAX(dec->digits, -dec->exponent) :
				    dec->digits + dec->exponent;
}

int  decimal_scale(const decimal_t *dec) {
	return dec->exponent < 0 ? -dec->exponent : 0;
}

bool
decimal_fits_fixed_point(decimal_t *dec, int precision, int scale)
{
	decimal_t tmp = *dec;
	decNumberReduce(&tmp, dec, &decimal_context);
	VERIFY(decimal_check_status(&tmp, &decimal_context) != NULL);
	int d = tmp.exponent + scale;
	if (d < 0)
		return false;
	return tmp.digits + d <= precision;
}

decimal_t *
decimal_scale_from_int32(decimal_t *dec, int32_t value, int scale)
{
	decimal_t tmp;
	decimal_t dec_scale;
	decNumberFromInt32(&tmp, value);
	decNumberFromInt32(&dec_scale, -scale);
	decNumberScaleB(dec, &tmp, &dec_scale, &decimal_context);
	return decimal_check_status(dec, &decimal_context);
}

decimal_t *
decimal_scale_from_int64(decimal_t *dec, int64_t value, int scale)
{
	decimal_t tmp;
	decimal_t dec_scale;
	decNumberFromInt64(&tmp, value);
	decNumberFromInt32(&dec_scale, -scale);
	decNumberScaleB(dec, &tmp, &dec_scale, &decimal_context);
	return decimal_check_status(dec, &decimal_context);
}

/** Big integer arithmetic below assumes little endianness. */
#if defined(HAVE_BYTE_ORDER_BIG_ENDIAN)
#error "big endian is not supported"
#endif

/**
 * Divide positive wide integer given in array `value' of length `len' by
 * `divisor'. The result is stored in wide integer given by array `result' of
 * length `len'. Remainder is stored in `remainder'.
 */
static void
int_divide(const uint64_t *value, int len, uint64_t divisor, uint64_t *result,
	   uint64_t *remainder)
{
	memset(result, 0, sizeof(*result) * len);
	*remainder = 0;
	for (int i = len * 64 - 1; i >= 0; i--) {
		*remainder = *remainder << 1 |
			     ((value[i / 64] >> (i % 64)) & 1);
		if (*remainder >= divisor) {
			*remainder -= divisor;
			result[i / 64] |= (1ULL << (i % 64));
		}
	}
}

/**
 * Negate inplace positive wide integer given by `value' and `len'. Negative
 * value bits are in 2's complement format.
 */
static void
int_negate(uint64_t *value, int len)
{
	int carry = 1;
	for (int i = 0; i < len; i++) {
		value[i] = ~value[i] + carry;
		carry = carry && value[i] == 0 ? 1 : 0;
	}
}

/**
 * 1000 multiplier/divisor below based on this expectation. Also the digits
 * calculations in decimal_from_wide_int().
 */
static_assert(DECDPUN == 3, "unexpected DECDPUN");

/**
 * Initialize decimal from wide integer given by array `value' of length `len'.
 * Integer is 2's complement signed. Similar to `decNumberFromInt64' but works
 * with wide integers.
 *
 * The overflow is possible only for 256-bit values due to decimal mantissa
 * size.
 *
 * Return true on success and false in case of overflow.
 */
static bool
decimal_from_wide_int(decimal_t *dec, const uint64_t *value, int len)
{
	assert(len <= 4);
	uint64_t zero[4];
	decNumberZero(dec);
	memset(zero, 0, sizeof(*zero) * len);
	if (memcmp(value, zero, sizeof(*value) * len) == 0)
		return true;
	int neg = value[len - 1] >> 63;
	uint64_t tmp[4];
	memcpy(tmp, value, sizeof(*tmp) * len);
	if (neg)
		int_negate(tmp, len);
	dec->digits = 0;
	int i;
	for (i = 0; memcmp(tmp, zero, sizeof(*tmp) * len) != 0; i++) {
		uint64_t quotient[4];
		uint64_t remainder;
		if (i >= DECNUMUNITS)
			return false;
		int_divide(tmp, len, 1000, quotient, &remainder);
		memcpy(tmp, quotient, sizeof(*tmp) * len);
		dec->lsu[i] = remainder;
		dec->digits += DECDPUN;
	}
	if (dec->lsu[i - 1] < 10)
		dec->digits -= 2;
	else if (dec->lsu[i - 1] < 100)
		dec->digits -= 1;
	if (dec->digits > DECIMAL_MAX_DIGITS)
		return false;
	if (neg)
		dec->bits |= DECNEG;
	return true;
}

decimal_t *
decimal_scale_from_int128(decimal_t *dec, const uint64_t *value, int scale)
{
	decimal_t tmp;
	decimal_t dec_scale;
	VERIFY(decimal_from_wide_int(&tmp, value, /*len=*/2));
	decNumberFromInt32(&dec_scale, -scale);
	decNumberScaleB(dec, &tmp, &dec_scale, &decimal_context);
	return decimal_check_status(dec, &decimal_context);
}

decimal_t *
decimal_scale_from_int256(decimal_t *dec, const uint64_t *value, int scale)
{
	decimal_t tmp;
	decimal_t dec_scale;
	if (!decimal_from_wide_int(&tmp, value, /*len=*/4))
		return NULL;
	decNumberFromInt32(&dec_scale, -scale);
	decNumberScaleB(dec, &tmp, &dec_scale, &decimal_context);
	return decimal_check_status(dec, &decimal_context);
}

const decimal_t *
decimal_scale_to_int32(const decimal_t *dec, int scale, int32_t *value)
{
	decimal_t tmp;
	decimal_t dec_scale;
	decimal_t dec_zero;
	decNumberFromInt32(&dec_scale, scale);
	decNumberScaleB(&tmp, dec, &dec_scale, &decimal_context);
	if (decimal_check_status(&tmp, &decimal_context) == NULL)
		return NULL;
	decNumberZero(&dec_zero);
	decNumberRescale(&tmp, &tmp, &dec_zero, &decimal_context);
	if (decimal_check_status(&tmp, &decimal_context) == NULL)
		return NULL;
	*value = decNumberToInt32(&tmp, &decimal_context);
	return decimal_check_status(&tmp,
				    &decimal_context) != NULL ? dec : NULL;
}

const decimal_t *
decimal_scale_to_int64(const decimal_t *dec, int scale, int64_t *value)
{
	decimal_t tmp;
	decimal_t dec_scale;
	decimal_t dec_zero;
	decNumberFromInt32(&dec_scale, scale);
	decNumberScaleB(&tmp, dec, &dec_scale, &decimal_context);
	if (decimal_check_status(&tmp, &decimal_context) == NULL)
		return NULL;
	decNumberZero(&dec_zero);
	decNumberRescale(&tmp, &tmp, &dec_zero, &decimal_context);
	if (decimal_check_status(&tmp, &decimal_context) == NULL)
		return NULL;
	*value = decNumberToInt64(&tmp, &decimal_context);
	return decimal_check_status(&tmp,
				    &decimal_context) != NULL ? dec : NULL;
}

/**
 * Multiply positive wide integer given in array `value' of length `len'
 * by `multiplier'. The result is stored in wide integer given by array
 * `result' of length `len'.
 *
 * Return true on success and false in case of overflow.
 */
static bool
int_multiply(const uint64_t *value, int len, uint32_t multiplier,
	     uint64_t *result)
{
	uint64_t carry = 0;
	for (int i = 0; i < len; i++) {
		__uint128_t product = (__uint128_t)value[i] * multiplier +
				      carry;
		result[i] = (uint64_t)product;
		carry = (uint64_t)(product >> 64);
	}
	return carry == 0;
}

/**
 * Add positive wide integers. The operands are given by arrays `a' and `b'
 * of length `len'. The result is stored in `a'.
 *
 * Return true on success and false in case of overflow.
 */
static bool
int_add_equal(uint64_t *a, const uint64_t *b, int len)
{
	uint64_t carry = 0;
	for (int i = 0; i < len; i++) {
		a[i] += b[i] + carry;
		carry = a[i] < b[i];
	}
	return carry == 0;
}

/**
 * Converts integer in `dec' to wide int given by array `value' of length
 * `len'. Similar to `decNumberToInt64' but works with wide integers.
 *
 * Return true on success and fail in case of overflow.
 */
static bool
decimal_to_wide_int(const decimal_t *dec, uint64_t *value, int len)
{
	assert(dec->exponent == 0);
	assert(len <= 4);
	uint64_t mul[4];
	memset(value, 0, sizeof(*value) * len);
	memset(mul, 0, sizeof(*mul) * len);
	mul[0] = 1;
	const decNumberUnit *up = dec->lsu;
	for (int d = 0; d < dec->digits; up++, d += DECDPUN) {
		uint64_t tmp[4];
		if (up != dec->lsu) {
			if (!int_multiply(mul, len, /*multiplier=*/1000, tmp))
				return false;
			memcpy(mul, tmp, sizeof(*mul) * len);
		}
		if (!int_multiply(mul, len, *up, tmp))
			return false;
		if (!int_add_equal(value, tmp, len))
			return false;
	}
	if (dec->bits & DECNEG) {
		if (value[len - 1] > (uint64_t)INT64_MIN)
			return false;
		if (value[len - 1] == (uint64_t)INT64_MIN) {
			for (int i = 0; i < len - 1; i++) {
				if (value[i] > 0)
					return false;
			}
		}
		int_negate(value, len);
		return true;
	} else {
		return value[len - 1] <= INT64_MAX;
	}
}

const decimal_t *
decimal_scale_to_int128(const decimal_t *dec, int scale, uint64_t *value)
{
	decimal_t tmp;
	decimal_t dec_scale;
	decimal_t dec_zero;
	decNumberFromInt32(&dec_scale, scale);
	decNumberScaleB(&tmp, dec, &dec_scale, &decimal_context);
	if (decimal_check_status(&tmp, &decimal_context) == NULL)
		return NULL;
	decNumberZero(&dec_zero);
	decNumberRescale(&tmp, &tmp, &dec_zero, &decimal_context);
	if (decimal_check_status(&tmp, &decimal_context) == NULL)
		return NULL;
	return decimal_to_wide_int(&tmp, value, /*len=*/2) ? dec : NULL;
}

const decimal_t *
decimal_scale_to_int256(const decimal_t *dec, int scale, uint64_t *value)
{
	decimal_t tmp;
	decimal_t dec_scale;
	decimal_t dec_zero;
	decNumberFromInt32(&dec_scale, scale);
	decNumberScaleB(&tmp, dec, &dec_scale, &decimal_context);
	if (decimal_check_status(&tmp, &decimal_context) == NULL)
		return NULL;
	assert(decimal_check_status(&tmp, &decimal_context) != NULL);
	decNumberZero(&dec_zero);
	decNumberRescale(&tmp, &tmp, &dec_zero, &decimal_context);
	if (decimal_check_status(&tmp, &decimal_context) == NULL)
		return NULL;
	return decimal_to_wide_int(&tmp, value, /*len=*/4) ? dec : NULL;
}

decimal_t *
decimal_zero(decimal_t *dec)
{
	decNumberZero(dec);
	return dec;
}

bool
decimal_is_int(const decimal_t *dec)
{
	return decNumberIsInt(dec);
}

bool
decimal_is_neg(const decimal_t *dec)
{
	return decNumberIsNegative(dec) && !decNumberIsZero(dec);
}

decimal_t *
decimal_from_string(decimal_t *dec, const char *str)
{
	const char *end = decNumberFromString(dec, str, &decimal_context);
	if (*end != '\0') {
		decContextZeroStatus(&decimal_context);
		return NULL;
	}
	return decimal_check_status(dec, &decimal_context);
}

decimal_t *
strtodec(decimal_t *dec, const char *str, const char **endptr) {
	const char *end = decNumberFromString(dec, str, &decimal_context);
	if (endptr != NULL)
		*endptr = end;
	return decimal_check_status(dec, &decimal_context);
}

decimal_t *
decimal_from_double(decimal_t *dec, double d)
{
	char buf[DECIMAL_MAX_DIGITS + 3];
	if (isinf(d) || isnan(d))
		return NULL;
	/*
	 * DBL_DIG is 15, it is the guaranteed amount of
	 * correct significant decimal digits in a double
	 * value.  There is no point in using higher precision,
	 * since every non-representable number has a long
	 * tail of erroneous digits:
	 * `23.42` -> `23.420000000000001705302565824240446091`
	 */
	snprintf(buf, sizeof(buf), "%.*g", DBL_DIG, d);
	return decimal_from_string(dec, buf);
}

decimal_t *
decimal_from_int64(decimal_t *dec, int64_t num)
{
	return decNumberFromInt64(dec, num);
}

decimal_t *
decimal_from_uint64(decimal_t *dec, uint64_t num)
{
	return decNumberFromUInt64(dec, num);
}

const char *
decimal_str(const decimal_t *dec)
{
	char *buf = tt_static_buf();
	/* No errors are possible. */
	char *tmp = decNumberToString(dec, buf);
	assert(buf == tmp);
	(void)tmp;
	return buf;
}

void
decimal_to_string(const decimal_t *dec, char *str)
{
	char *tmp = decNumberToString(dec, str);
	assert(str == tmp);
	(void)tmp;
}

static decimal_t *
decimal_to_integer(decimal_t *dec)
{
	decimal_t z;
	decNumberZero(&z);
	if (decimal_scale(dec) != 0) {
		/*
		 * Rounding mode is important here.
		 * We want to be consistent with double
		 * to int conversion so that comparison
		 * hints work correctly.
		 */
		decimal_floor(dec, 0);
	}
	/* Zero the number exponent for decNumberToInt64. */
	decNumberRescale(dec, dec, &z, &decimal_context);
	return decimal_check_status(dec, &decimal_context);
}

const decimal_t *
decimal_to_int64(const decimal_t *dec, int64_t *num)
{
	decimal_t d = *dec;
	if (decimal_to_integer(&d) == NULL)
		return NULL;
	*num = decNumberToInt64(&d, &decimal_context);
	return decimal_check_status(&d, &decimal_context) != NULL ? dec : NULL;
}

const decimal_t *
decimal_to_uint64(const decimal_t *dec, uint64_t *num)
{
	decimal_t d = *dec;
	if (decimal_to_integer(&d) == NULL)
		return NULL;
	*num = decNumberToUInt64(&d, &decimal_context);
	return decimal_check_status(&d, &decimal_context) != NULL ? dec : NULL;
}

int
decimal_compare(const decimal_t *lhs, const decimal_t *rhs)
{
	decNumber res;
	decNumberCompare(&res, lhs, rhs, &decimal_context);
	int r = decNumberToInt32(&res, &decimal_context);
	assert(decimal_check_status(&res, &decimal_context) != NULL);
	return r;
}

static decimal_t *
decimal_round_with_mode(decimal_t *dec, int scale, enum rounding mode)
{
	if (scale < 0 || scale > DECIMAL_MAX_DIGITS)
		return NULL;

	if (scale >= decimal_scale(dec))
		return dec;

	int ndig = MAX(decimal_precision(dec) - decimal_scale(dec) + scale, 1);
	decContext context = {
		ndig, /* Precision */
		ndig, /* emax */
		scale != 0 ? -1 : 0, /* emin */
		mode, /* rounding */
		0, /* no traps */
		0, /* zero status */
		0 /* no clamping */
	};

	decNumberPlus(dec, dec, &context);
	assert(decimal_check_status(dec, &context) != NULL);
	return dec;
}

inline decimal_t *
decimal_round(decimal_t *dec, int scale)
{
	return decimal_round_with_mode(dec, scale, DECIMAL_ROUNDING);
}

inline decimal_t *
decimal_floor(decimal_t *dec, int scale)
{
	return decimal_round_with_mode(dec, scale, DEC_ROUND_DOWN);
}

decimal_t *
decimal_trim(decimal_t *dec)
{
	decimal_t *res = decNumberTrim(dec);

	/* No errors are possible */
	assert(res == dec);
	return res;
}

decimal_t *
decimal_rescale(decimal_t *dec, int scale)
{
	if (scale < 0)
		return NULL;
	if (scale <= decimal_scale(dec))
		return decimal_round(dec, scale);
	/* how much zeros shoud we append. */
	int delta = scale + dec->exponent;
	if (scale > DECIMAL_MAX_DIGITS || dec->digits + delta > DECIMAL_MAX_DIGITS)
		return NULL;
	decimal_t new_scale;
	decimal_from_int64(&new_scale, -scale);
	decNumberRescale(dec, dec, &new_scale, &decimal_context);
	assert(decimal_check_status(dec, &decimal_context) != NULL);
	return dec;
}

decimal_t *
decimal_remainder(decimal_t *res, const decimal_t *lhs, const decimal_t *rhs)
{
	decNumberRemainder(res, lhs, rhs, &decimal_context);
	return decimal_check_status(res, &decimal_context);
}

decimal_t *
decimal_abs(decimal_t *res, const decimal_t *dec)
{
	decNumberAbs(res, dec, &decimal_context);
	assert(decimal_check_status(res, &decimal_context) != NULL);
	return res;
}

decimal_t *
decimal_minus(decimal_t *res, const decimal_t *dec)
{
	decNumberMinus(res, dec, &decimal_context);
	assert(decimal_check_status(res, &decimal_context) != NULL);
	return res;
}

decimal_t *
decimal_add(decimal_t *res, const decimal_t *lhs, const decimal_t *rhs)
{
	decNumberAdd(res, lhs, rhs, &decimal_context);
	return decimal_check_status(res, &decimal_context);
}

decimal_t *
decimal_sub(decimal_t *res, const decimal_t *lhs, const decimal_t *rhs)
{
	decNumberSubtract(res, lhs, rhs, &decimal_context);

	return decimal_check_status(res, &decimal_context);
}

decimal_t *
decimal_mul(decimal_t *res, const decimal_t *lhs, const decimal_t *rhs)
{
	decNumberMultiply(res, lhs, rhs, &decimal_context);

	return decimal_check_status(res, &decimal_context);
}

decimal_t *
decimal_div(decimal_t *res, const decimal_t *lhs, const decimal_t *rhs)
{
	decNumberDivide(res, lhs, rhs, &decimal_context);

	return decimal_check_status(res, &decimal_context);
}

decimal_t *
decimal_log10(decimal_t *res, const decimal_t *lhs)
{
	decNumberLog10(res, lhs, &decimal_context);

	return decimal_check_status(res, &decimal_context);
}

decimal_t *
decimal_ln(decimal_t *res, const decimal_t *lhs)
{
	/*
	 * ln hangs in an infinite loop when result is
	 * between -10 ^ emin and 10 ^ emin.
	 * For small x, ln(1 + x) = x. Say, we take the
	 * smallest allowed value for
	 * (1 + x) = 1 + 10 ^ -(DECIMAL_MAX_DIGITS - 1).
	 * For ln to work for this value we need to set emin to
	 * -DECIMAL_MAX_DIGITS.
	 */
	int32_t emin = decimal_context.emin;
	decimal_context.emin = -DECIMAL_MAX_DIGITS;

	decNumberLn(res, lhs, &decimal_context);

	decimal_context.emin = emin;
	if (decimal_check_status(res, &decimal_context) == NULL) {
		return NULL;
	} else {
		/*
		 * The increased EMIN allows for scale up to
		 * 2 * (DECIMAL_MAX_DIGITS - 1).
		 * Round back to DECIMAL_MAX_DIGITS - 1.
		 */
		decimal_round(res, DECIMAL_MAX_DIGITS - 1);
		return res;
	}
}

decimal_t *
decimal_pow(decimal_t *res, const decimal_t *lhs, const decimal_t *rhs)
{
	decNumberPower(res, lhs, rhs, &decimal_context);

	return decimal_check_status(res, &decimal_context);
}

decimal_t *
decimal_exp(decimal_t *res, const decimal_t *lhs)
{
	decNumberExp(res, lhs, &decimal_context);

	return decimal_check_status(res, &decimal_context);
}

decimal_t *
decimal_sqrt(decimal_t *res, const decimal_t *lhs)
{
	decNumberSquareRoot(res, lhs, &decimal_context);

	return decimal_check_status(res, &decimal_context);
}

uint32_t
decimal_len(const decimal_t *dec)
{
	uint32_t sizeof_scale = dec->exponent > 0 ? mp_sizeof_int(-dec->exponent) :
						    mp_sizeof_uint(-dec->exponent);
	/* sizeof_scale  + ceil((digits + 1) / 2) */
	return sizeof_scale + 1 + dec->digits / 2;
}

char *
decimal_pack(char *data, const decimal_t *dec)
{
	uint32_t len = decimal_len(dec);
	char *svp = data;
	/* encode scale */
	if (dec->exponent > 0) {
		data = mp_encode_int(data, -dec->exponent);
	} else {
		data = mp_encode_uint(data, -dec->exponent);
	}
	len -= data - svp;
	int32_t scale;
	char *tmp = (char *)decPackedFromNumber((uint8_t *)data, len, &scale, dec);
	assert(tmp == data);
	(void)tmp;
	data += len;
	return data;
}

/*
 * Amount of digits the structure can actually hold. Might be bigger than
 * DECIMAL_MAX_DIGITS if DECIMAL_MAX_DIGITS is not divisible by DECDPUN.
 */
#define DECIMAL_DIGIT_CAPACITY (DECNUMUNITS * DECDPUN)
static_assert(DECIMAL_DIGIT_CAPACITY >= DECIMAL_MAX_DIGITS,
	      "DECIMAL_DIGIT_CAPACITY must be big enough to hold "
	      "DECIMAL_MAX_DIGITS");

/*
 * In decimal_unpack() we check BCD (binary coded decimal) input length
 * to make sure it will fit into decimal_t. Length should be less than
 * or equal to (DECIMAL_DIGIT_CAPACITY + 1) / 2.
 *
 * At the same time we should make sure that this limit allows us
 * to pass DECIMAL_MAX_DIGITS in BCD. BCD contains 2 digits per byte,
 * except one last nibble containing the sign. So an input string of length
 * L might fill up 2 * L - 1 digits.
 *
 * If DECIMAL_DIGIT_CAPACITY is odd then than limit allows to pass
 * DECIMAL_DIGIT_CAPACITY digits which is surely greater than
 * DECIMAL_MAX_DIGITS (we check it in assertion above and also it is
 * true by decNumber design).
 *
 * If DECIMAL_DIGIT_CAPACITY is even then BCD can pass
 * DECIMAL_DIGIT_CAPACITY - 1 digits.
 */
static_assert(DECIMAL_DIGIT_CAPACITY % 2 == 1 ||
	      DECIMAL_DIGIT_CAPACITY - 1 >= DECIMAL_MAX_DIGITS,
	      "DECIMAL_DIGIT_CAPACITY got even, now it must be strictly "
	      "greater than DECIMAL_MAX_DIGITS");

decimal_t *
decimal_unpack(const char **data, uint32_t len, decimal_t *dec)
{
	/*
	 * MsgPack extensions have length greater or equal than 1 by
	 * specification.
	 */
	assert(len > 0);

	int32_t scale;
	const char *end = *data + len;
	const char *p = *data;
	enum mp_type type = mp_typeof(*p);
	if (type == MP_UINT) {
		if (mp_check_uint(p, end) > 0)
			return NULL;
		scale = mp_decode_uint(&p);
	} else if (type == MP_INT) {
		if (mp_check_int(p, end) > 0)
			return NULL;
		scale = mp_decode_int(&p);
	} else {
		return NULL;
	}
	len -= p - *data;
	/* First check that there is enough space to store the digits. */
	if (len > (DECIMAL_DIGIT_CAPACITY + 1) / 2)
		return NULL;
	/* No digits to decode. */
	if (len == 0)
		return NULL;
	if (decPackedToNumber((uint8_t *)p, len, &scale, dec) == NULL)
		return NULL;
	/*
	 * Check for precision, adjusted exponent and handle case of
	 * subnormal numbers when allowed exponent is lower but
	 * the precision is reduced accordingly.
	 */
	int32_t adj_exp = dec->exponent + dec->digits - 1;
	int32_t emin_sub = decimal_context.emin - DECIMAL_MAX_DIGITS + 1;
	if (!(dec->digits <= DECIMAL_MAX_DIGITS &&
	      adj_exp <= decimal_context.emax &&
	      ((adj_exp >= decimal_context.emin) ||
	       (adj_exp >= emin_sub && dec->digits <= adj_exp - emin_sub + 1))))
		return NULL;
	*data = end;
	return dec;
}
