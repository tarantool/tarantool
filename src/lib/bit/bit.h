#ifndef TARANTOOL_LIB_BIT_BIT_H_INCLUDED
#define TARANTOOL_LIB_BIT_BIT_H_INCLUDED
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

/**
 * @file
 * @brief Bit manipulation library
 */
#include "trivia/config.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(HAVE_FFSL) || defined(HAVE_FFSLL)
#include <string.h>
#include <strings.h>
#endif /* defined(HAVE_FFSL) || defined(HAVE_FFSLL) */
#include <limits.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** @cond false **/
#define bit_likely(x)    __builtin_expect((x),1)
#define bit_unlikely(x)  __builtin_expect((x),0)

struct unaligned_mem
{
	union
	{
		uint8_t  u8;
		uint16_t u16;
		uint32_t u32;
		uint64_t u64;
		float	 f;
		double	 lf;
		bool     b;
	};
} __attribute__((__packed__));
/** @endcond **/

/**
 * @brief Unaligned load from memory.
 * @param p pointer
 * @return number
 */
inline uint8_t
load_u8(const void *p)
{
	return ((const struct unaligned_mem *)p)->u8;
}

/** @copydoc load_u8 */
inline uint16_t
load_u16(const void *p)
{
	return ((const struct unaligned_mem *)p)->u16;
}

/** @copydoc load_u8 */
inline uint32_t
load_u32(const void *p)
{
	return ((const struct unaligned_mem *)p)->u32;
}

/** @copydoc load_u8 */
inline uint64_t
load_u64(const void *p)
{
	return ((const struct unaligned_mem *)p)->u64;
}

/** @copydoc load_u8 */
inline float
load_float(const void *p)
{
	return ((const struct unaligned_mem *)p)->f;
}

/** @copydoc load_u8 */
inline double
load_double(const void *p)
{
	return ((const struct unaligned_mem *)p)->lf;
}

/** @copydoc load_u8 */
inline bool
load_bool(const void *p)
{
	return ((const struct unaligned_mem *)p)->b;
}

/**
 * @brief Unaligned store to memory.
 * @param p pointer
 * @param v number
 */
inline void
store_u8(void *p, uint8_t v)
{
	((struct unaligned_mem *)p)->u8 = v;
}

/** @copydoc store_u8 */
inline void
store_u16(void *p, uint16_t v)
{
	((struct unaligned_mem *)p)->u16 = v;
}

/** @copydoc store_u8 */
inline void
store_u32(void *p, uint32_t v)
{
	((struct unaligned_mem *)p)->u32 = v;
}

/** @copydoc store_u8 */
inline void
store_u64(void *p, uint64_t v)
{
	((struct unaligned_mem *)p)->u64 = v;
}

/** @copydoc store_u8 */
inline void
store_float(void *p, float v)
{
	((struct unaligned_mem *)p)->f = v;
}

/** @copydoc store_u8 */
inline void
store_double(void *p, double v)
{
	((struct unaligned_mem *)p)->lf = v;
}

/** @copydoc store_bool */
inline void
store_bool(void *p, bool b)
{
	((struct unaligned_mem *)p)->b = b;
}

/**
 * @brief Test bit \a pos in memory chunk \a data
 * @param data memory chunk
 * @param pos bit number (zero-based)
*  @retval true bit \a pos is set in \a data
 * @retval false otherwise
 */
inline bool
bit_test(const void *data, size_t pos)
{
	size_t chunk = pos / CHAR_BIT;
	size_t offset = pos % CHAR_BIT;

	const unsigned char *cdata = (const unsigned char  *) data;
	return (cdata[chunk] >> offset) & 0x1;
}

/**
 * @brief Set bit \a pos in a memory chunk \a data
 * @param data memory chunk
 * @param pos bit number (zero-based)
 * @return previous value
 * @see bit_test
 * @see bit_clear
 */
inline bool
bit_set(void *data, size_t pos)
{
	size_t chunk = pos / CHAR_BIT;
	size_t offset = pos % CHAR_BIT;

	unsigned char *cdata = (unsigned char  *) data;
	bool prev = (cdata[chunk] >> offset) & 0x1;
	cdata[chunk] |= (1U << offset);
	return prev;
}

/**
 * @brief Clear bit \a pos in memory chunk \a data
 * @param data memory chunk
 * @param pos bit number (zero-based)
 * @return previous value
 * @see bit_test
 * @see bit_set
 */
inline bool
bit_clear(void *data, size_t pos)
{
	size_t chunk = pos / CHAR_BIT;
	size_t offset = pos % CHAR_BIT;

	unsigned char *cdata = (unsigned char *) data;
	bool prev = (cdata[chunk] >> offset) & 0x1;
	cdata[chunk] &= ~(1U << offset);
	return prev;
}

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
inline int
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

/**
 * @copydoc bit_ctz_u32
 */
inline int
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
inline int
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
inline int
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
inline int
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
inline int
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
inline uint32_t
bit_rotl_u32(uint32_t x, int r)
{
	/* gcc recognises this code and generates a rotate instruction */
	return ((x << r) | (x >> (32 - r)));
}

/**
 * @copydoc bit_rotl_u32
 */
inline uint64_t
bit_rotl_u64(uint64_t x, int r)
{
	/* gcc recognises this code and generates a rotate instruction */
	return ((x << r) | (x >> (64 - r)));
}

/**
 * @copydoc bit_rotl_u32
 */
__attribute__ ((const)) inline uintmax_t
bit_rotl_umax(uintmax_t x, int r)
{
	/* gcc recognises this code and generates a rotate instruction */
	return ((x << r) | (x >> (sizeof(uintmax_t) * CHAR_BIT - r)));
}
/**
 * @brief Rotate @a x right by @a r bits
 * @param x integer
 * @param r number for bits to rotate
 * @return @a x rotated right by @a r bits
 * @todo Move this method to bit.h
 */
inline uint32_t
bit_rotr_u32(uint32_t x, int r)
{
	/* gcc recognises this code and generates a rotate instruction */
	return ((x >> r) | (x << (32 - r)));
}

/**
 * @copydoc bit_rotr_u32
 */
inline uint64_t
bit_rotr_u64(uint64_t x, int r)
{
	/* gcc recognises this code and generates a rotate instruction */
	return ((x >> r) | (x << (64 - r)));
}

/**
 * @copydoc bswap_u32
 */
inline uint16_t
bswap_u16(uint16_t x)
{
#if defined(HAVE_BUILTIN_BSWAP16)
	return __builtin_bswap16(x);
#else /* !defined(HAVE_BUILTIN_BSWAP16) */
	return	((x << 8) & UINT16_C(0xff00)) |
		((x >> 8) & UINT16_C(0x00ff));
#endif
}

/**
 * @brief Returns a byte order swapped integer @a x.
 * This function does not take into account host architecture
 * (as it done by htonl / ntohl functions) and always returns @a x
 * with byte order swapped (BE -> LE if @a x is in BE and vice versa).
 * @param x integer
 * @return @a x with swapped bytes
 */
inline uint32_t
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
inline uint64_t
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

/** @cond false **/
#if defined(__x86_64__)
/* Use bigger words on x86_64 */
#define ITER_UINT uint64_t
#define ITER_CTZ bit_ctz_u64
#else
#define ITER_UINT uint32_t
#define ITER_CTZ bit_ctz_u32
#endif
/** @endcond **/

/**
 * @brief The Bit Iterator
 */
struct bit_iterator {
	/** @cond false **/
	/** Current word to process using ctz **/
	ITER_UINT word;
	/** A bitmask that XORed with word (for set = false iteration) **/
	ITER_UINT word_xor;
	/** A base offset of the word in bits **/
	size_t word_base;
	/** A pointer to the start of a memory chunk **/
	const char *start;
	/** A pointer to the next part of a memory chunk */
	const char *next;
	/** A pointer to the end of a memory chunk */
	const char *end;
	/** @endcond **/
};

/**
 * @brief Initialize bit iterator \a it
 * @param it bit iterator
 * @param data memory chunk
 * @param size size of the memory chunk \a data
 * @param set true to iterate over set bits or false to iterate over clear bits
 */
inline void
bit_iterator_init(struct bit_iterator *it, const void *data, size_t size,
		  bool set)
{
	it->start = (const char *) data;
	it->next = it->start;
	it->end = it->next + size;
	if (bit_unlikely(size == 0)) {
		it->word = 0;
		return;
	}

	it->word_xor = set ? 0 : (ITER_UINT) -1;
	it->word_base = 0;

	/* Check if size is a multiple of sizeof(ITER_UINT) */
	const char *e = it->next + size % sizeof(ITER_UINT);
	if (bit_likely(it->next == e)) {
		it->word = *(ITER_UINT *) it->next;
		it->next += sizeof(ITER_UINT);
	} else {
		it->word = it->word_xor;
		char *w = (char *) &it->word;
		while (it->next < e)
			*w++ = *it->next++;
	}
	it->word ^= it->word_xor;
}

/**
 * @brief Return a number of a next set bit in \a it or \a SIZE_MAX
 * if no bits are remain in \a it
 * @param it bit iterator
 * @retval a zero-based number of a next set bit in iterator \a it
 * @retval SIZE_MAX if \a it does not have more set bits
 */
inline size_t
bit_iterator_next(struct bit_iterator *it)
{
	while (bit_unlikely(it->word == 0)) {
		if (bit_unlikely(it->next >= it->end))
			return SIZE_MAX;

		/* Extract the next word from memory */
		it->word = *(ITER_UINT *) it->next;
		it->word ^= it->word_xor;
		it->word_base = (it->next - it->start) * CHAR_BIT;
		it->next += sizeof(ITER_UINT);
	}

	/* Find the position of a first trailing bit in the current word */
	int bit = ITER_CTZ(it->word);
	/* Remove the first trailing bit from the current word */
	it->word &= it->word - 1;
	/* Add start position if the current word to the found bit */
	return it->word_base + bit;
}

#undef ITER_CTZ
#undef ITER_UINT

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_BIT_BIT_H_INCLUDED */
