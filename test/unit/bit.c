#include <bit/bit.h>

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>

#include "unit.h"

static uint64_t vals[] = {
	0UL, 1UL, 2UL, 32768UL, 65535UL, 65536UL, 726075912UL, 858993459UL,
	1073741824UL, 1245250552UL, 1431655765UL, 1656977767UL, 2147483648UL,
	2283114629UL, 2502548245UL, 4294967295UL, 708915120906848425UL,
	1960191741125985428UL, 3689348814741910323UL, 5578377670650038654UL,
	9223372036854775808UL, 10755112315580060033UL, 11163782031541429823UL,
	13903686156871869732UL, 14237897302422917095UL, 14302190498657618739UL,
	15766411510232741269UL, 15984546468465238145UL, 18446744073709551615UL
};

static void
test_ctz_clz(void)
{
	header();

	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		if (vals[i] == 0)
			continue;

		uint64_t val64 = vals[i];
		uint32_t val32 = (uint32_t) vals[i];

		printf("bit_ctz_u64(%" PRIu64 ") => %d\n", val64,
			bit_ctz_u64(val64));
		printf("bit_clz_u64(%" PRIu64 ") => %d\n", val64,
			bit_clz_u64(val64));

		if (vals[i] > UINT32_MAX)
			continue;

		printf("bit_ctz_u32(%" PRIu32 ") => %d\n", val32,
			bit_ctz_u32(val32));
		printf("bit_clz_u32(%" PRIu32 ") => %d\n", val32,
			bit_clz_u32(val32));
	}

	footer();
}

static void
test_count(void)
{
	header();

	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint64_t val64 = vals[i];
		uint32_t val32 = (uint32_t) vals[i];

		printf("bit_count_u64(%" PRIu64 ") => %d\n", val64,
			bit_count_u64(val64));

		if (vals[i] > UINT32_MAX)
			continue;

		printf("bit_count_u32(%" PRIu32 ") => %d\n", val32,
			bit_count_u32(val32));
	}

	footer();
}

static void
test_rotl_rotr_one(int rot)
{
	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint64_t val64 = vals[i];
		uint32_t val32 = (uint32_t) vals[i];

		printf("bit_rotl_u64(%" PRIu64 ", %d) => %" PRIu64 "\n",
		       val64, rot, bit_rotl_u64(val64, rot));
		printf("bit_rotr_u64(%" PRIu64 ", %d) => %" PRIu64 "\n",
		       val64, rot, bit_rotr_u64(val64, rot));

		if (vals[i] > UINT32_MAX || rot >= 32)
			continue;

		printf("bit_rotl_u32(%" PRIu32 ", %d) => %" PRIu32 "\n",
		       val32, rot, bit_rotl_u32(val32, rot));
		printf("bit_rotr_u32(%" PRIu32 ", %d) => %" PRIu32 "\n",
		       val32, rot, bit_rotr_u32(val32, rot));
	}
}

static void
test_rotl_rotr(void)
{
	header();

	int rots[] = { 1, 15, 16, 31, 32, 63 };
	for (unsigned r = 0; r < sizeof(rots) / sizeof(rots[0]); r++) {
		test_rotl_rotr_one(rots[r]);
	}

	footer();
}

static void
test_bswap(void)
{
	header();

	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint64_t val64 = vals[i];
		uint32_t val32 = (uint32_t) vals[i];

		printf("bswap_u64(%" PRIu64 ") => %" PRIu64 "\n", val64,
			bswap_u64(val64));

		if (vals[i] > UINT32_MAX)
			continue;

		printf("bswap_u32(%" PRIu32 ") => %" PRIu32 "\n", val32,
			bswap_u32(val32));
	}

	footer();
}

static inline void
test_index_print(const int *start, const int *end)
{
	for (const int *cur = start; cur < end; cur++) {
		printf("%d ", *cur);
	}
}

static void
test_index(void)
{
	header();

	int indexes[sizeof(int64_t) * CHAR_BIT + 1];

	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint64_t val64 = vals[i];
		uint32_t val32 = (uint32_t) vals[i];

		printf("bit_index_u64(%" PRIu64 ", *, -1) => ", val64);
		test_index_print(indexes, bit_index_u64(val64, indexes, -1));
		printf("\n");

		if (vals[i] > UINT32_MAX)
			continue;

		printf("bit_index_u32(%" PRIu32 ", *, -1) => ", val32);
		test_index_print(indexes, bit_index_u32(val32, indexes, -1));
		printf("\n");
	}

	footer();
}

static void
test_bit_iter(void)
{
	header();

	struct bit_iterator it;
	uint64_t *data = vals + 6;
	size_t size = 10;

	size_t pos = 0;

	printf("Set: ");
	bit_iterator_init(&it, data, size, true);
	while ( (pos = bit_iterator_next(&it)) != SIZE_MAX) {
		printf("%zu, ", pos);
		fail_unless(bit_test(data, pos));
	}
	printf("\n");

	printf("Clear: ");
	bit_iterator_init(&it, data, size, false);
	while ( (pos = bit_iterator_next(&it)) != SIZE_MAX) {
		printf("%zu, ", pos);
		fail_if(bit_test(data, pos));
	}
	printf("\n");

	footer();
}

static void
test_bit_iter_empty(void)
{
	header();

	struct bit_iterator it;

	bit_iterator_init(&it, NULL, 0, true);
	fail_unless(bit_iterator_next(&it) == SIZE_MAX);

	bit_iterator_init(&it, NULL, 0, false);
	fail_unless(bit_iterator_next(&it) == SIZE_MAX);

	footer();
}

/**
 * Check that bit iterator works correctly with bit sequences of size that are
 * not multiple of uint64_t.
 */
static void
test_bit_iter_fractional(void)
{
	header();

	struct bit_iterator it;
	uint64_t data[2] = {UINT64_MAX, UINT64_MAX};

	for (size_t size = 1; size <= 16; size++) {
		bit_iterator_init(&it, &data, size, true);

		size_t expect_count = size * CHAR_BIT;
		size_t expect_pos = 0;
		size_t pos;

		while ( (pos = bit_iterator_next(&it)) != SIZE_MAX) {
			fail_unless(expect_count > 0);
			fail_unless(pos == expect_pos);
			expect_count--;
			expect_pos++;
		}

		fail_unless(expect_count == 0);
	}

	footer();
}

static void
test_bitmap_size(void)
{
	header();
	fail_unless(BITMAP_SIZE(1) == sizeof(long));
	fail_unless(BITMAP_SIZE(10) == sizeof(long));
	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT) == sizeof(long));
	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT + 1) == sizeof(long) * 2);
	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT * 4) == sizeof(long) * 4);
	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT * 4 - 1) == sizeof(long) * 4);
	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT * 9 / 2) == sizeof(long) * 5);
	footer();
}

/**
 * Check all possible valid inputs of `bit_set_range()'.
 */
static void
test_bit_set_range(void)
{
	header();

	const size_t data_size = 64; /* In bytes. */
	const size_t data_count = data_size * CHAR_BIT; /* In bits. */

	for (size_t pos = 0; pos < data_count; pos++) {
		for (size_t count = 0; count <= data_count - pos; count++) {
			for (int val = 0; val <= 1; val++) {
				uint8_t data[data_size];
				uint8_t ref[data_size];

				/* Initialize buffers. */
				memset(data, 0xA5, sizeof(data));
				memset(ref, 0xA5, sizeof(ref));
				/* Calculate reference result. */
				for (size_t i = pos; i < pos + count; i++) {
					if (val == 0)
						bit_clear(ref, i);
					else
						bit_set(ref, i);
				}
				/* The function under test. */
				bit_set_range(data, pos, count, val);
				/* Compare results. */
				fail_if(memcmp(data, ref, sizeof(data)) != 0);
			}
		}
	}

	footer();
}

/**
 * Check all possible valid inputs of `bit_copy_range()`.
 */
static inline void
test_bit_copy_range(bool src_val)
{
	header();
	printf("Source value: %s\n", src_val ? "true" : "false");

	const size_t data_size = 64; /* In bytes. */
	const size_t data_count = data_size * CHAR_BIT; /* In bits. */
	const uint8_t src_byte = src_val ? 0xff : 0x00;
	const uint8_t dst_byte = src_val ? 0x00 : 0xff;

	uint8_t src[data_size];
	memset(src, src_byte, sizeof(src));
	for (size_t src_i = 0; src_i < data_count; src_i++) {
		for (size_t dst_i = 0; dst_i < data_count; dst_i++) {
			size_t src_max = data_count - src_i;
			size_t dst_max = data_count - dst_i;
			for (size_t c = 1; c <= src_max && c <= dst_max; c++) {
				uint8_t dst[data_size];
				uint8_t ref[data_size];

				/* Initialize the buffer. */
				memset(dst, dst_byte, sizeof(dst));
				/* Calculate the reference mask. */
				memset(ref, dst_byte, sizeof(ref));
				bit_set_range(ref, dst_i, c, src_val);
				/* The function under test. */
				bit_copy_range(dst, dst_i, src, src_i, c);
				/* Compare results. */
				fail_if(memcmp(dst, ref, sizeof(dst)) != 0);
			}
		}
	}

	footer();
}

int
main(void)
{
	test_ctz_clz();
	test_count();
	test_rotl_rotr();
	test_bswap();
	test_index();
	test_bit_iter();
	test_bit_iter_empty();
	test_bit_iter_fractional();
	test_bitmap_size();
	test_bit_set_range();
	test_bit_copy_range(true);
	test_bit_copy_range(false);
}
