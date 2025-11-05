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

#include "bit/bit.h"

extern inline uint8_t
load_u8(const void *p);

extern inline uint16_t
load_u16(const void *p);

extern inline uint32_t
load_u32(const void *p);

extern inline uint64_t
load_u64(const void *p);

extern inline float
load_float(const void *p);

extern inline double
load_double(const void *p);

extern inline void
store_u8(void *p, uint8_t v);

extern inline void
store_u16(void *p, uint16_t v);

extern inline void
store_u32(void *p, uint32_t v);

extern inline void
store_u64(void *p, uint64_t v);

extern inline void
store_float(void *p, float v);

extern inline void
store_double(void *p, double v);

extern inline void
store_bool(void *p, bool v);

extern inline bool
bit_test(const void *data, size_t pos);

extern inline bool
bit_set(void *data, size_t pos);

extern inline bool
bit_clear(void *data, size_t pos);

extern inline void
bit_set_range(void *data, size_t pos, size_t count, bool val);

extern inline int
bit_ctz_u32(uint32_t x);

extern inline int
bit_ctz_u64(uint64_t x);

extern inline int
bit_clz_u32(uint32_t x);

extern inline int
bit_clz_u64(uint64_t x);

extern inline int
bit_count_u32(uint32_t x);

extern inline int
bit_count_u64(uint64_t x);

extern inline uint32_t
bit_rotl_u32(uint32_t x, int r);

extern inline uint64_t
bit_rotl_u64(uint64_t x, int r);

extern inline uint32_t
bit_rotr_u32(uint32_t x, int r);

extern inline uint64_t
bit_rotr_u64(uint64_t x, int r);

extern inline uint16_t
bswap_u16(uint16_t x);

extern inline uint32_t
bswap_u32(uint32_t x);

extern inline uint64_t
bswap_u64(uint64_t x);

#define BITINDEX_NAIVE(type, x, bitsize) {				\
	/* naive generic implementation, worst case */			\
	type bit = 1;							\
	int i = 0;							\
	for (unsigned k = 0; k < bitsize; k++) {				\
		if (x & bit) {						\
			indexes[i++] = offset + k + 1;			\
		}							\
		bit <<= 1;						\
	}								\
									\
	indexes[i] = 0;							\
	return indexes + i;						\
}

int *
bit_index_u32(uint32_t x, int *indexes, int offset)
{
#if  defined(HAVE_BUILTIN_CTZ)
	int prev_pos = 0;
	int i = 0;

#if defined(HAVE_BUILTIN_POPCOUNT)
	/* fast implementation using built-in popcount function */
	const int count = bit_count_u32(x);
	while (i < count) {
#else
	/* sligtly slower implementation without using built-in popcount */
	while(x) {
#endif
		/* use ctz */
		const int a = bit_ctz_u32(x);

		prev_pos += a + 1;
		x >>= a;
		x >>= 1;
		indexes[i++] = offset + prev_pos;
	}

	indexes[i] = 0;
	return indexes + i;
#else /* !defined(HAVE_BUILTIN_CTZ) */
	BITINDEX_NAIVE(uint32_t, x, sizeof(uint32_t) * CHAR_BIT);
#endif
}

int *
bit_index_u64(uint64_t x, int *indexes, int offset) {
#if  defined(HAVE_BUILTIN_CTZLL)
	int prev_pos = 0;
	int i = 0;

#if defined(HAVE_BUILTIN_POPCOUNTLL)
	/* fast implementation using built-in popcount function */
	const int count = bit_count_u64(x);
	while (i < count) {
#else
	/* sligtly slower implementation without using built-in popcount */
	while(x) {
#endif
		/* use ctz */
		const int a = bit_ctz_u64(x);

		prev_pos += a + 1;
		x >>= a;
		x >>= 1;
		indexes[i++] = offset + prev_pos;
	}

	indexes[i] = 0;
	return indexes + i;
#else /* !defined(HAVE_CTZ) */
	BITINDEX_NAIVE(uint64_t, x, sizeof(uint64_t) * CHAR_BIT);
#endif
}

#undef BITINDEX_NAIVE

extern inline void
bit_iterator_init(struct bit_iterator *it, const void *data, size_t size,
		  bool set);

extern inline size_t
bit_iterator_next(struct bit_iterator *it);

/*
 * The code below is taken from Apache Arrow C++ library and modified.
 * Please see the NOTICE file.
 */

struct bitmap_word_align_params {
	/**
	 * Unaligned (we require word alignment)
	 * bit prefix of the bitmap (cannot be processed
	 * via popcount).
	 */
	uint64_t leading_bits;
	/**
	 * Bit suffix of the bitmap which is smaller
	 * than a word in size, which means popcount
	 * cannot be used on it.
	 */
	uint64_t trailing_bits;
	/**
	 * Bit offset from the start of the bitmap to
	 * the start of the @a trailing_bits.
	 */
	size_t trailing_bit_offset;
	/**
	 * Pointer to the start of the word aligned
	 * part of the bitmap (can be processed via popcount).
	 */
	const uint8_t *aligned_start;
	/** Count of bits in the word aligned part of the bitmap. */
	size_t aligned_bits;
	/** Count of words in the word aligned part of the bitmap. */
	size_t aligned_words;
};

/**
 * @brief Implementation of bitmap aligning up to multiple
 * of words.
 */
static inline struct bitmap_word_align_params
bitmap_word_align(size_t align_in_bytes, const uint8_t *data,
		  size_t bit_offset, size_t length)
{
	assert(IS_POWER_OF_2(align_in_bytes));
	const size_t align_in_bits = align_in_bytes * 8;
	/**
	 * Compute a "bit address" that we can align up to
	 * `align_in_bits`. We don't care about losing the
	 * upper bits since we are only	interested in the
	 * difference between both addresses.
	 */
	const uint64_t bit_addr = (size_t)data * 8 + (uint64_t)bit_offset;
	const uint64_t aligned_bit_addr =
		ROUND_UP_TO_POWER_OF_2(bit_addr, align_in_bits);
	struct bitmap_word_align_params p;
	p.leading_bits = MIN((uint64_t)length, aligned_bit_addr - bit_addr);
	p.aligned_words = (length - p.leading_bits) / align_in_bits;
	p.aligned_bits = p.aligned_words * align_in_bits;
	p.trailing_bits = length - p.leading_bits - p.aligned_bits;
	p.trailing_bit_offset = bit_offset + p.leading_bits + p.aligned_bits;
	p.aligned_start = data + (bit_offset + p.leading_bits) / 8;
	return p;
}

size_t
bit_count(const uint8_t *data, size_t bit_offset, size_t length)
{
	const size_t pop_len = sizeof(size_t) * 8;
	size_t count = 0;
	if (unlikely(length == 0)) {
		return 0;
	}

	const struct bitmap_word_align_params p =
		bitmap_word_align(pop_len / 8, data, bit_offset, length);
	/** Count possibly unaligned prefix. */
	for (size_t i = bit_offset; i < bit_offset + p.leading_bits; ++i) {
		if (bit_test(data, i)) {
			++count;
		}
	}

	if (p.aligned_words > 0) {
		/**
		 * popcount as much as possible with the widest possible count
		 */
		const uint64_t *u64_data = (const uint64_t *)p.aligned_start;
		assert(((size_t)u64_data & 7) == 0);
		const uint64_t *end = u64_data + p.aligned_words;

		const size_t k_count_unroll_factor = 4;
		const size_t words_rounded = ROUND_DOWN(p.aligned_words,
							k_count_unroll_factor);
		size_t count_unroll[k_count_unroll_factor];
		memset(count_unroll, 0, sizeof(count_unroll));

		/** Unroll the loop for better performance */
		for (size_t i = 0;
		     i < words_rounded;
		     i += k_count_unroll_factor) {
			for (size_t k = 0; k < k_count_unroll_factor; k++) {
				count_unroll[k] += bit_count_u64(u64_data[k]);
			}
			u64_data += k_count_unroll_factor;
		}
		for (size_t k = 0; k < k_count_unroll_factor; k++) {
			count += count_unroll[k];
		}

		/** The trailing part */
		for (; u64_data < end; ++u64_data) {
			count += bit_count_u64(*u64_data);
		}
	}

	/**
	 * Account for left over bits (in theory we could fall back to smaller
	 * versions of popcount but the code complexity is likely not worth it)
	 */
	for (size_t i = p.trailing_bit_offset; i < bit_offset + length; ++i) {
		if (bit_test(data, i)) {
			++count;
		}
	}

	return count;
}

void
bit_copy_range(uint8_t *restrict dst, size_t dst_i, const uint8_t *restrict src,
	       size_t src_i, size_t count)
{
	if (count == 0)
		return;

	/* Assure the buffers are non-overlapping. */
	assert(src > dst + DIV_ROUND_UP(dst_i + count, CHAR_BIT) - 1 ||
	       dst > src + DIV_ROUND_UP(src_i + count, CHAR_BIT) - 1);

	/*
	 * We can have:
	 * - a head of bits in the start;
	 * - a bunch of whole bytes in the middle;
	 * - a tail of bits in the end.
	 */
	size_t dst_i_byte = dst_i / CHAR_BIT;
	size_t dst_i_bit = dst_i % CHAR_BIT;
	size_t src_i_byte = src_i / CHAR_BIT;
	size_t src_i_bit = src_i % CHAR_BIT;

	/* We select shift directions based on this. */
	ssize_t diff_bit = dst_i_bit - src_i_bit;

	/* The head may be the only byte to copy to. */
	size_t dst_head_size = dst_i_bit + count < CHAR_BIT ?
			       count : CHAR_BIT - dst_i_bit;
	size_t dst_rest_size = count - dst_head_size;    /* Can be 0. */
	size_t dst_body_size = dst_rest_size / CHAR_BIT; /* In bytes. */
	size_t dst_tail_size = dst_rest_size % CHAR_BIT; /* In bits. */

	/*
	 *    dst_i_bit
	 *        |
	 * Dst: - D D D - - - -
	 *        \___/
	 *          |
	 *    dst_head_mask (but in most cases the head is until end of byte)
	 */
	size_t dst_head_mask = ((1 << dst_head_size) - 1) << dst_i_bit;
	size_t dst_tail_mask = (1 << dst_tail_size) - 1;

	if (diff_bit <= 0) {
		/*
		 *      Head              Body              Tail
		 * Dst: - - - D D D D D   D D D D D D D D   D - - - - - - -
		 * Src: - - - - - S S S   S S S S S S S S   S S S - - - - -
		 *            \_/         \_/ \_________/
		 *             |           |       |
		 *           shift       shift  shift_in
		 */
		size_t shift = -diff_bit;
		size_t shift_in = CHAR_BIT - shift;

		/*
		 * We're copying offsetted data from src to dst. So, in general,
		 * we shift the source left for `shift` bits, then we take first
		 * `shift` bits of the next source byte in. Given example above
		 * (note the shift right moves bits left since they're written
		 * from the least to the most significant):
		 *
		 * Src[0]: [- - - - - 0 1 2] >> shift    = [- - - 0 1 2 - -]
		 * Src[1]: [3 4 5 6 7 8 9 a] << shift_in = [- - - - - - 3 4]
		 * Combine the two into result to write:   [- - - 0 1 2 3 4]
		 *
		 * Effectively we combine `shift_in` last bits of Src[0] and
		 * `shift` first bits of Src[1]. But we can only do that if we
		 * have anything to shift in. It's totally possible to have a
		 * situation like this:
		 *
		 * Dst: - - - D D D - -
		 * Src: - - - - - S S S
		 *
		 * Here we only have one byte of source and should not attempt
		 * to read next ones, since all the source bits required are
		 * located in the single byte. In this case we just shift the
		 * bits of the first source byte right to align them with Dst.
		 *
		 * So we have to check if we have next bytes to shift first bits
		 * from in order to prevent OOB reads.
		 */
		unsigned src_0 = src[src_i_byte];
		/* The source is at least 2 bytes? Can read the second byte. */
		bool can_read_src1 = src_i_bit + count > CHAR_BIT;
		unsigned src_1 = can_read_src1 ? src[src_i_byte + 1] : 0;
		/* Copy the head bits. */
		dst[dst_i_byte] = (dst[dst_i_byte] & ~dst_head_mask) |
				  (((src_0 >> shift) | (src_1 << shift_in)) &
				   dst_head_mask);

		/* Copy the body bytes. */
		for (size_t i = 0; i < dst_body_size; i++) {
			size_t dst_curr = dst_i_byte + 1 + i;
			size_t src_curr = src_i_byte + 1 + i;
			unsigned src_0 = src[src_curr];
			/*
			 * If we have a non-zero shift, we must have the next
			 * byte available to shift-in first bits from.
			 */
			bool can_read_src1 = shift != 0;
			unsigned src_1 = can_read_src1 ? src[src_curr + 1] : 0;
			dst[dst_curr] = (src_0 >> shift) | (src_1 << shift_in);
		}

		/* Copy the tail bits. */
		if (dst_tail_size > 0) {
			size_t dst_curr = dst_i_byte + 1 + dst_body_size;
			size_t src_curr = src_i_byte + 1 + dst_body_size;

			/*
			 * We have the destination tail, so if we access the
			 * next byte after the one we're currently on, we can
			 * run out of bounds. Let's see when that happens:
			 *
			 * Dst: - - - D D D D D   D D D D D - - -
			 * Src: - - - - - S S S   S S S S S S S -
			 *
			 * Here we can't read the next byte cause all the bits
			 * we need are located in the last byte of the source.
			 * But we can have something like this:
			 *
			 *      Head            Tail            Next byte
			 * Dst: - D D D D D D D D D D D D - - - - - - - - - - -
			 *                                \___/
			 *                                  |
			 *                      (CHAR_BIT - dst_tail_size)
			 *
			 * Src: - - - - - S S S S S S S S S S S S - - - - - - -
			 *        \_____/
			 *           |
			 *         shift
			 *
			 * Here the source bits we need are located in the next
			 * source byte. This happens when the amount of non-dst
			 * bits in the tail is less than `shift`.
			 */
			bool can_read_src1 = shift > (CHAR_BIT - dst_tail_size);
			unsigned src_0 = src[src_curr];
			unsigned src_1 = can_read_src1 ? src[src_curr + 1] : 0;
			dst[dst_curr] = (dst[dst_curr] & ~dst_tail_mask) |
					(((src_0 >> shift) |
					  (src_1 << shift_in)) &
					 dst_tail_mask);
		}
	} else {
		/*
		 *      Head              Body              Tail
		 * Dst: - - - - - D D D   D D D D D D D D   D D D - - - - -
		 *            \_/
		 *             |
		 *           shift
		 *
		 *       shift_in
		 *       ____|____
		 *      /         \
		 * Src: - - - S S S S S   S S S S S S S S   S - - - - - - -
		 *            \_/   \_/
		 *             |     |
		 *           shift  carry
		 */
		size_t shift = diff_bit;
		size_t shift_in = CHAR_BIT - shift;

		unsigned src_0 = src[src_i_byte];
		/* Copy the head bits. */
		dst[dst_i_byte] = (dst[dst_i_byte] & ~dst_head_mask) |
				  ((src_0 << shift) & dst_head_mask);

		/* Copy the body bytes. */
		for (size_t i = 0; i < dst_body_size; i++) {
			size_t dst_curr = dst_i_byte + 1 + i;
			size_t src_curr = src_i_byte + 1 + i;
			unsigned carry = src[src_curr - 1] >> shift_in;
			unsigned src_0 = src[src_curr];
			dst[dst_curr] = carry | (src_0 << shift);
		}

		/* Copy the tail bits. */
		if (dst_tail_size > 0) {
			size_t dst_curr = dst_i_byte + 1 + dst_body_size;
			size_t src_curr = src_i_byte + 1 + dst_body_size;
			unsigned carry = src[src_curr - 1] >> shift_in;

			/*
			 * It may so happen that the amount of bytes the source
			 * is scattered over is smaller than the destination, in
			 * this case we can't read the next source byte because
			 * we can go out of the source buffer bounds, e. g.:
			 *
			 *      Head            Body            Tail
			 * Dst: - - - - - - D D D D D D D D D D D D - - - - - -
			 * Src: - - - S S S S S S S S S S S S - - - - - - - - -
			 *                                \___/ \_____________/
			 *                                  |          |
			 *                           carry -+    Out of bounds
			 *
			 * This condition may be simplified: this only happens
			 * when we shift for the tail size or more.
			 */
			bool can_read_last = shift < dst_tail_size;
			unsigned src_0 = can_read_last ? src[src_curr] : 0;
			dst[dst_curr] = (dst[dst_curr] & ~dst_tail_mask) |
					((carry | (src_0 << shift)) &
					 dst_tail_mask);
		}
	}
}
