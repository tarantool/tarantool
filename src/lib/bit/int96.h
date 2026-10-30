#ifndef TARANTOOL_LIB_BIT_INT96_H_INCLUDED
#define TARANTOOL_LIB_BIT_INT96_H_INCLUDED
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

#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>

/**
 * 96-bit signed integer.
 * 1)Negative integer is stored as 96-bit two's complement
 * 2)Stores an integer modulo n, where n = 2**96
 * Actually (1) == (2), as the wave-particle duality.
 * Designed for storing integers in range [INT64_MIN, UINT64_MAX],
 * and detecting overflow (out of range [INT64_MIN, UINT64_MAX])
 * after operations (addition, subtraction) on them.
 * The base fact is when two uint64_t or int64_t values
 * are converted to int96, and then added or subtracted, the
 * int96 arithmetics cannot overflow. Actually you need at least
 * billions of adding UINT64_MAX or INT64_MIN to make it overflow.
 * Addition is implemented directly;
 * For subtraction use addition of inverted number.
 */

/**
 * struct 96-bit signed integer
 */
struct int96_num {
	/* most significant 64 bits */
	uint64_t high64;
	/* least significant order 32 bits */
	/* (high dword - zero bits) */
	uint64_t low32;
};

/**
 * Assign to unsigned 64-bit
 */
static inline void
int96_set_unsigned(struct int96_num *num, uint64_t val)
{
	const uint64_t mask = 0xFFFFFFFFu;
	num->high64 = val >> 32;
	num->low32 = val & mask;
}

/**
 * Assign to signed 64-bit
 */
static inline void
int96_set_signed(struct int96_num *num, int64_t val)
{
	const uint64_t mask = 0xFFFFFFFFu;
	num->high64 = (uint64_t)(val >> 32);
	num->low32 = ((uint64_t)val) & mask;
}

/**
 * Invert number (assign a to -a)
 */
static inline void
int96_invert(struct int96_num *num)
{
	const uint64_t mask = 0xFFFFFFFFu;
	assert(!(num->low32 & ~mask));
	num->high64 = ~num->high64;
	num->low32 = (~num->low32) & mask;
	num->low32++;
	num->high64 += num->low32 >> 32;
	num->low32 &= mask;
}

/**
 * Add to number 'to' another number 'what'
 */
static inline void
int96_add(struct int96_num *to, const struct int96_num *what)
{
	const uint64_t mask = 0xFFFFFFFFu;
	assert(!(to->low32 & ~mask));
	assert(!(what->low32 & ~mask));
	to->low32 += what->low32;
	to->high64 += to->low32 >> 32;
	to->high64 += what->high64;
	to->low32 &= mask;
}

/**
 * Get lowers 64 bit of a number (that is C cast to uint64_t)
 */
static inline uint64_t
int96_get_low64bit(const struct int96_num *num)
{
	return num->low32 | (num->high64 << 32);
}

/**
 * Returns true if a number fits [0, UINT64_MAX] range
 */
static inline bool
int96_is_uint64(const struct int96_num *num)
{
	return (num->high64 >> 32) == 0;
}

/**
 * Get number as uint64_t,
 * the number is expected to be valid range (assert)
 */
static inline uint64_t
int96_extract_uint64(const struct int96_num *num)
{
	assert(int96_is_uint64(num));
	return int96_get_low64bit(num);
}

/**
 * Returns true if a number fits [INT64_MIN, 0) range
 */
static inline bool
int96_is_neg_int64(const struct int96_num *num)
{
	return (num->high64 >> 31) == 0x1FFFFFFFFull;
}

/**
 * Get number as negative int64_t,
 * the number is expected to be valid range (assert)
 */
static inline int64_t
int96_extract_neg_int64(const struct int96_num *num)
{
	assert(int96_is_neg_int64(num));
	return (int64_t)int96_get_low64bit(num);
}

#endif /* #ifndef TARANTOOL_LIB_BIT_INT96_H_INCLUDED */
