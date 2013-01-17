#ifndef TARANTOOL_LIB_BIT_BIT_H_INCLUDED
#define TARANTOOL_LIB_BIT_BIT_H_INCLUDED
/*
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

/**
 * @file
 * @brief Bit manipulation library
 */
#include "config.h"

#include <stddef.h>
#include <stdint.h>
#if defined(HAVE_FFSL) || defined(HAVE_FFSLL)
#include <string.h>
#include <strings.h>
#endif /* defined(HAVE_FFSL) || defined(HAVE_FFSLL) */
#include <limits.h>

/**
 * @cond false
 * @brief Naive implementation of ctz.
 */
#define CTZ_NAIVE(x, bitsize) {						\
	if (x == 0) {							\
		return (bitsize);					\
	}								\
									\
	int r = 0;							\
	for (; (x & 1) == 0; r++) {					\
		x >>= 1;						\
	}								\
									\
	return r;							\
}
/** @endcond */

/**
 * @brief Count Trailing Zeros.
 * Returns the number of trailing 0-bits in @a x, starting at the least
 * significant bit position. If @a x is 0, the result is undefined.
 * @param x integer
 * @see __builtin_ctz()
 * @return the number trailing 0-bits
 */
 __attribute__((const)) inline int
bit_ctz_u32(uint32_t x)
{
#if defined(HAVE_BUILTIN_CTZ)
	return __builtin_ctz(x);
#elif defined(HAVE_FFSL)
	return ffsl(x) - 1;
#else
	CTZ_NAIVE(x, sizeof(uint32_t) * CHAR_BIT);
#endif
}

#include <stdio.h>
#include <inttypes.h>

/**
 * @copydoc bit_ctz_u32
 */
 __attribute__((const)) inline int
bit_ctz_u64(uint64_t x)
{
#if   defined(HAVE_BUILTIN_CTZLL)
	return __builtin_ctzll(x);
#elif defined(HAVE_FFSLL)
	return ffsll(x) - 1;
#else
	CTZ_NAIVE(x, sizeof(uint64_t) * CHAR_BIT);
#endif
}

#undef CTZ_NAIVE

/**
 * @cond false
 * @brief Naive implementation of clz.
 */
#define CLZ_NAIVE(x, bitsize) {						\
	if (x == 0) {							\
		return  (bitsize);					\
	}								\
									\
	int r = (bitsize);						\
	for (; x; r--) {						\
		x >>= 1;						\
	}								\
									\
	return r;							\
}
/** @endcond */

/**
 * @brief Count Leading Zeros.
 * Returns the number of leading 0-bits in @a x, starting at the most
 * significant bit position. If @a x is 0, the result is undefined.
 * @param x integer
 * @see __builtin_clz()
 * @return the number of leading 0-bits
 */
 __attribute__((const)) inline int
bit_clz_u32(uint32_t x)
{
#if   defined(HAVE_BUILTIN_CLZ)
	return __builtin_clz(x);
#else /* !defined(HAVE_BUILTIN_CLZ) */
	CLZ_NAIVE(x, sizeof(uint32_t) * CHAR_BIT);
#endif
}

/**
 * @copydoc bit_clz_u32
 */
__attribute__((const)) inline int
bit_clz_u64(uint64_t x)
{
#if   defined(HAVE_BUILTIN_CLZLL)
	return __builtin_clzll(x);
#else /* !defined(HAVE_BUILTIN_CLZLL) */
	CLZ_NAIVE(x, sizeof(uint64_t) * CHAR_BIT);
#endif
}

#undef CLZ_NAIVE

/**
 * @cond false
 * @brief Naive implementation of popcount.
 */
#define POPCOUNT_NAIVE(x, bitsize)  {					\
	int r;								\
	for (r = 0; x; r++) {						\
		x &= (x-1);						\
	}								\
									\
	return r;							\
}
/** @endcond */

/**
 * @brief Returns the number of 1-bits in @a x.
 * @param x integer
 * @see __builtin_popcount()
 * @return the number of 1-bits in @a x
 */
__attribute__((const)) inline int
bit_count_u32(uint32_t x)
{
#if   defined(HAVE_BUILTIN_POPCOUNT)
	return __builtin_popcount(x);
#else /* !defined(HAVE_BUILTIN_POPCOUNT) */
	POPCOUNT_NAIVE(x, sizeof(uint32_t) * CHAR_BIT);
#endif
}

/**
 * @copydoc bit_count_u32
 */
__attribute__((const)) inline int
bit_count_u64(uint64_t x)
{
#if   defined(HAVE_BUILTIN_POPCOUNTLL)
	return __builtin_popcountll(x);
#else /* !defined(HAVE_BUILTIN_POPCOUNTLL) */
	POPCOUNT_NAIVE(x, sizeof(uint64_t) * CHAR_BIT);
#endif
}

#undef POPCOUNT_NAIVE

/**
 * @brief Rotate @a x left by @a r bits
 * @param x integer
 * @param r number for bits to rotate
 * @return @a x rotated left by @a r bits
 */
__attribute__ ((const)) inline uint32_t
bit_rotl_u32(uint32_t x, int r)
{
	/* gcc recognises this code and generates a rotate instruction */
	return ((x << r) | (x >> (32 - r)));
}

/**
 * @copydoc bit_rotl_u32
 */
__attribute__ ((const)) inline uint64_t
bit_rotl_u64(uint64_t x, int r)
{
	/* gcc recognises this code and generates a rotate instruction */
	return ((x << r) | (x >> (64 - r)));
}

/**
 * @brief Rotate @a x right by @a r bits
 * @param x integer
 * @param r number for bits to rotate
 * @return @a x rotated right by @a r bits
 * @todo Move this method to bit.h
 */
__attribute__ ((const)) inline uint32_t
bit_rotr_u32(uint32_t x, int r)
{
	/* gcc recognises this code and generates a rotate instruction */
	return ((x >> r) | (x << (32 - r)));
}

/**
 * @copydoc bit_rotr_u32
 */
__attribute__ ((const)) inline uint64_t
bit_rotr_u64(uint64_t x, int r)
{
	/* gcc recognises this code and generates a rotate instruction */
	return ((x >> r) | (x << (64 - r)));
}

/**
 * @brief Returns a byte order swapped integer @a x.
 * This function does not take into account host architecture
 * (as it done by htonl / ntohl functions) and always returns @a x
 * with byte order swapped (BE -> LE if @a x is in BE and vice versa).
 * @param x integer
 * @return @a x with swapped bytes
 */
__attribute__ ((const)) inline uint32_t
bswap_u32(uint32_t x)
{
#if defined(HAVE_BUILTIN_BSWAP32)
	return __builtin_bswap32(x);
#else /* !defined(HAVE_BUILTIN_BSWAP32) */
	return	((x << 24) & UINT32_C(0xff000000)) |
		((x <<  8) & UINT32_C(0x00ff0000)) |
		((x >>  8) & UINT32_C(0x0000ff00)) |
		((x >> 24) & UINT32_C(0x000000ff));
#endif
}

/**
 * @copydoc bswap_u32
 */
__attribute__ ((const)) inline uint64_t
bswap_u64(uint64_t x)
{
#if defined(HAVE_BUILTIN_BSWAP64)
	return __builtin_bswap64(x);
#else /* !defined(HAVE_BUILTIN_BSWAP64) */
	return  ( (x << 56) & UINT64_C(0xff00000000000000)) |
		( (x << 40) & UINT64_C(0x00ff000000000000)) |
		( (x << 24) & UINT64_C(0x0000ff0000000000)) |
		( (x <<  8) & UINT64_C(0x000000ff00000000)) |
		( (x >>  8) & UINT64_C(0x00000000ff000000)) |
		( (x >> 24) & UINT64_C(0x0000000000ff0000)) |
		( (x >> 40) & UINT64_C(0x000000000000ff00)) |
		( (x >> 56) & UINT64_C(0x00000000000000ff));
#endif
}

/**
 * @brief Index bits in the @a x, i.e. find all positions where bits are set.
 * This method fills @a indexes array with found positions in increasing order.
 * @a offset is added to each index before putting it into @a indexes.
 * @param x integer
 * @param indexes memory array where found indexes are stored
 * @param offset a number added to each index
 * @return pointer to last+1 element in indexes array
 */
int *
bit_index_u32(uint32_t x, int *indexes, int offset);

/**
 * @copydoc bit_index_u32
 */
int *
bit_index_u64(uint64_t x, int *indexes, int offset);

#endif /* TARANTOOL_LIB_BIT_BIT_H_INCLUDED */
