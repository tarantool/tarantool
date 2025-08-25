#include <bit/bit.h>

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/**
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

	int ctz64[] = { 
		0, 1, 15, 0, 16, 3, 0, 30, 3, 0, 0, 31, 0, 0, 0, 0, 2, 0, 1,
		63, 0, 0, 2, 0, 0, 0, 0, 0
	};
	int clz64[] = { 
		63, 62, 48, 48, 47, 34, 34, 33, 33, 33, 33, 32, 32, 32, 32,
		4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	int ctz32[] = { 0, 1, 15, 0, 16, 3, 0, 30, 3, 0, 0, 31, 0, 0, 0 };
	int clz32[] = { 31, 30, 16, 16, 15, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0 };

	plan(sizeof(ctz64) / sizeof(ctz64[0]) + 
	     sizeof(clz64) / sizeof(clz64[0]) +
	     sizeof(ctz32) / sizeof(ctz32[0]) +
	     sizeof(clz32) / sizeof(clz32[0]));

	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		if (vals[i] == 0)
			continue;

		uint64_t val64 = vals[i];
		uint32_t val32 = (uint32_t)vals[i];

		is(bit_ctz_u64(val64), ctz64[i - 1], "bit_ctz_u64(%" PRIu64
		   ") should return %d", val64, ctz64[i - 1]);
		is(bit_clz_u64(val64), clz64[i - 1], "bit_clz_u64(%" PRIu64
		   ") should return %d", val64, clz64[i - 1]);

		if (vals[i] > UINT32_MAX)
			continue;

		is(bit_ctz_u32(val32), ctz32[i - 1], "bit_ctz_u32(%" PRIu32
		   ") should return %d", val32, ctz32[i - 1]);
		is(bit_clz_u32(val32), clz32[i - 1], "bit_clz_u32(%" PRIu32
		   ") should return %d", val32, clz32[i - 1]);
	}

	check_plan();
	footer();
}

static void
test_clz_least_significant(void)
{
	header();

	size_t results[] = {
		0, 7, 6, 5, 4, 3, 2, 1, 1, 0, 6, 5, 4, 3, 2, 1, 2, 1, 0, 5,
		4, 3, 2, 1, 3, 2, 1, 0, 4, 3, 2, 1, 4, 3, 2, 1, 0, 3, 2, 1,
		5, 4, 3, 2, 1, 0, 2, 1, 6, 5, 4, 3, 2, 1, 0, 1, 7, 6, 5, 4,
		3, 2, 1, 0, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3,
		2, 1, 1, 0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
		2, 1, 0, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 3, 2, 1,
		0, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 4, 3, 2, 1, 0, 11,
		10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 5, 4, 3, 2, 1, 0, 10, 9, 8,
		7, 6, 5, 4, 3, 2, 1, 6, 5, 4, 3, 2, 1, 0, 9, 8, 7, 6, 5, 4,
		3, 2, 1, 7, 6, 5, 4, 3, 2, 1, 0, 8, 7, 6, 5, 4, 3, 2, 1, 8,
		7, 6, 5, 4, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 9, 8, 7, 6, 5,
		4, 3, 2, 1, 0, 6, 5, 4, 3, 2, 1, 10, 9, 8, 7, 6, 5, 4, 3, 2,
		1, 0, 5, 4, 3, 2, 1, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
		4, 3, 2, 1, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 3, 2,
		1, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 2, 1, 14,
		13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 15, 14, 13,
		12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 23, 22, 21, 20,
		19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3,
		2, 1, 1, 0, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11,
		10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 2, 1, 0, 21, 20, 19, 18, 17,
		16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 3, 2,
		1, 0, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7,
		6, 5, 4, 3, 2, 1, 4, 3, 2, 1, 0, 19, 18, 17, 16, 15, 14, 13,
		12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 5, 4, 3, 2, 1, 0, 18,
		17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
		6, 5, 4, 3, 2, 1, 0, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8,
		7, 6, 5, 4, 3, 2, 1, 7, 6, 5, 4, 3, 2, 1, 0, 16, 15, 14, 13,
		12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 8, 7, 6, 5, 4, 3, 2,
		1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 9,
		8, 7, 6, 5, 4, 3, 2, 1, 0, 14, 13, 12, 11, 10, 9, 8, 7, 6,
		5, 4, 3, 2, 1, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 13, 12, 11,
		10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 11, 10, 9, 8, 7, 6, 5, 4, 3,
		2, 1, 0, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 12, 11, 10,
		9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 11, 10, 9, 8, 7, 6, 5, 4, 3,
		2, 1, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 10, 9,
		8, 7, 6, 5, 4, 3, 2, 1, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5,
		4, 3, 2, 1, 0, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15, 14, 13, 12,
		11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 8, 7, 6, 5, 4, 3, 2,
		1, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
		7, 6, 5, 4, 3, 2, 1, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8,
		7, 6, 5, 4, 3, 2, 1, 0, 6, 5, 4, 3, 2, 1, 18, 17, 16, 15,
		14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 5, 4, 3,
		2, 1, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5,
		4, 3, 2, 1, 0, 4, 3, 2, 1, 20, 19, 18, 17, 16, 15, 14, 13,
		12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 3, 2, 1, 21, 20,
		19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3,
		2, 1, 0, 2, 1, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12,
		11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 23, 22, 21, 20, 19,
		18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2,
		1, 0
	};

	plan(sizeof(results) / sizeof(results[0]));

	int result_idx = 0;
	size_t val_repr_size = sizeof("{0x%02x, 0x%02x, 0x%02x}");
	char *val_repr = xmalloc(val_repr_size);
	for (int i = 1; i < 4; i++) {
		uint8_t *val = xcalloc(i, sizeof(val[0]));
		for (int j = 0; j < i * CHAR_BIT; j++) {
			val[j / CHAR_BIT] |= 0x1 << (j % CHAR_BIT);
			for (int k = 0; k < i * CHAR_BIT; k++) {
				memset(val_repr, 0, val_repr_size);
				strcat(val_repr, "{");
				for (int n = 0; n < i; n++) {
					char *byte_repr = 
						xmalloc(sizeof("0x%02x"));
					sprintf(byte_repr, "0x%02x",
						(int)val[n]);
					strcat(val_repr, byte_repr);
					free(byte_repr);
					if (n < i - 1)
						strcat(val_repr, ", ");
				}
				strcat(val_repr, "}");

				is(bit_clz_least_significant(
					val, k, (i * CHAR_BIT) - k),
				   results[result_idx], 
				   "bit_clz_least_significant(%s, %d, %d) " 
				   "should return %zu", val_repr, k, 
				   (i * CHAR_BIT) - k, results[result_idx]);
				result_idx++;
			}
			val[j / CHAR_BIT] ^= val[j / CHAR_BIT];
		}
		free(val);
	}
	free(val_repr);

	check_plan();
	footer();
}

static void
test_count(void)
{
	header();

	int count64[] = { 
		0, 1, 1, 1, 16, 1, 11, 16, 1, 14, 16, 17, 1, 10, 16, 32, 29,
		19, 32, 31, 1, 24, 35, 28, 33, 37, 33, 25, 64
	};
	int count32[] = { 
		0, 1, 1, 1, 16, 1, 11, 16, 1, 14, 16, 17, 1, 10, 16, 32, 
	};

	plan(sizeof(count64) / sizeof(count64[0]) + 
	     sizeof(count32) / sizeof(count32[0]));

	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint64_t val64 = vals[i];
		uint32_t val32 = (uint32_t)vals[i];

		is(bit_count_u64(val64), count64[i], "bit_count_u64(%" PRIu64
		   ") should return %d", val64, count64[i]);

		if (vals[i] > UINT32_MAX)
			continue;

		is(bit_count_u32(val32), count32[i], "bit_count_u32(%" PRIu32
		   ") should return %d", val32, count32[i]);
	}

	check_plan();
	footer();
}

static void
test_rotl_rotr_one(int rot, uint64_t *rotl64, size_t rotl64sz,
	 	   uint64_t *rotr64, size_t rotr64sz, uint32_t *rotl32, 
		   size_t rotl32sz, uint32_t *rotr32, size_t rotr32sz)
{
	plan(rotl64sz + rotr64sz + rotl32sz + rotr32sz);

	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint64_t val64 = vals[i];
		uint32_t val32 = (uint32_t)vals[i];

		is(bit_rotl_u64(val64, rot), rotl64[i], 
		   "bit_rotl_u64(%" PRIu64 ", %d) should return %" PRIu64,
		   val64, rot, rotl64[i]);
		is(bit_rotr_u64(val64, rot), rotr64[i], 
		   "bit_rotr_u64(%" PRIu64 ", %d) should return %" PRIu64,
		   val64, rot, rotr64[i]);

		if (vals[i] > UINT32_MAX || rot >= 32)
			continue;

		is(bit_rotl_u32(val32, rot), rotl32[i], 
		   "bit_rotl_u32(%" PRIu32 ", %d) should return %" PRIu32,
		   val32, rot, rotl32[i]);
		is(bit_rotr_u32(val32, rot), rotr32[i], 
		   "bit_rotr_u32(%" PRIu32 ", %d) should return %" PRIu32,
		   val32, rot, rotr32[i]);
	}

	check_plan();
}

static void
test_rotl_rotr(void)
{
	header();

	int rots[] = { 1, 15, 16, 31, 32, 63 };
	plan(sizeof(rots) / sizeof(rots[0]));

	uint64_t rotl1_64[] = {
		0, 2, 4, 65536, 131070, 131072, 1452151824, 1717986918,
		2147483648, 2490501104, 2863311530, 3313955534, 4294967296,
		4566229258, 5005096490, 8589934590, 1417830241813696850,
		7378697629483820646, 1, 3880819989373308031,
		10029050531136282575, 13086078946755930923,
		18446744073709551615
	};
	size_t rotl1_64sz = sizeof(rotl1_64) / sizeof(rotl1_64[0]);
	uint64_t rotr1_64[] = { 
		0, 9223372036854775808, 1, 16384, 9223372036854808575,
		32768, 363037956, 9223372037284272537, 536870912, 622625276,
		9223372037570603690, 9223372037683264691, 1073741824,
		9223372037996333122, 9223372038106049930,
		9223372039002259455, 9577829597308200020,
		11068046444225730969, 4611686018427387904,
		14805263052625490719, 16342320688066234355,
		17106577791971146442, 18446744073709551615
	};
	size_t rotr1_64sz = sizeof(rotr1_64) / sizeof(rotr1_64[0]);
	uint32_t rotl1_32[] = {
		0, 2, 4, 65536, 131070, 131072, 1452151824, 1717986918,
		2147483648, 2490501104, 2863311530, 3313955534, 1,
		271261963, 710129195, 4294967295, 3920383482251970856,
		11156755341300077308, 3063480557450568451,
		9360628240034187849, 10157636923605685863,
		13522348863220924675
	};
	size_t rotl1_32sz = sizeof(rotl1_32) / sizeof(rotl1_32[0]);
	uint32_t rotr1_32[] = { 
		0, 2147483648, 1, 16384, 2147516415, 32768, 363037956,
		2576980377, 536870912, 622625276, 2863311530, 2975972531,
		1073741824, 3289040962, 3398757770, 4294967295,
		980095870562992714, 2789188835325019327,
		14600928194644805824, 6951843078435934866,
		16374467286183585177, 17215645271087394880
	};
	size_t rotr1_32sz = sizeof(rotr1_32) / sizeof(rotr1_32[0]);

	uint64_t rotl15_64[] = {};
	uint64_t rotr15_64[] = {};
	uint32_t rotl15_32[] = {};
	uint32_t rotr15_32[] = {};

	uint64_t rotl16_64[] = {};
	uint64_t rotr16_64[] = {};
	uint32_t rotl16_32[] = {};
	uint32_t rotr16_32[] = {};

	uint64_t rotl31_64[] = {};
	uint64_t rotr31_64[] = {};
	uint32_t rotl31_32[] = {};
	uint32_t rotr31_32[] = {};

	uint64_t rotl32_64[] = {};
	uint64_t rotr32_64[] = {};
	uint32_t rotl32_32[] = {};
	uint32_t rotr32_32[] = {};

	uint64_t rotl63_64[] = {};
	uint64_t rotr63_64[] = {};
	uint32_t rotl63_32[] = {};
	uint32_t rotr63_32[] = {};

	for (unsigned r = 0; r < sizeof(rots) / sizeof(rots[0]); r++) {
		switch (rots[r]) {
		case 1:
			test_rotl_rotr_one(rots[r], rotl1_64, rotl1_64sz, 
					   rotr1_64, rotr1_64sz, rotl1_32,
					   rotl1_32sz, rotr1_32, rotr1_32sz);
			break;
		default:
			continue;
		}
		
	}

	check_plan();
	footer();
}
*/

// static void
// test_bswap(void)
// {
// 	header();

// 	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
// 		uint64_t val64 = vals[i];
// 		uint32_t val32 = (uint32_t) vals[i];

// 		printf("bswap_u64(%" PRIu64 ") => %" PRIu64 "\n", val64,
// 			bswap_u64(val64));

// 		if (vals[i] > UINT32_MAX)
// 			continue;

// 		printf("bswap_u32(%" PRIu32 ") => %" PRIu32 "\n", val32,
// 			bswap_u32(val32));
// 	}

// 	footer();
// }

// static inline void
// test_index_print(const int *start, const int *end)
// {
// 	for (const int *cur = start; cur < end; cur++) {
// 		printf("%d ", *cur);
// 	}
// }

// static void
// test_index(void)
// {
// 	header();

// 	int indexes[sizeof(int64_t) * CHAR_BIT + 1];

// 	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
// 		uint64_t val64 = vals[i];
// 		uint32_t val32 = (uint32_t) vals[i];

// 		printf("bit_index_u64(%" PRIu64 ", *, -1) => ", val64);
// 		test_index_print(indexes, bit_index_u64(val64, indexes, -1));
// 		printf("\n");

// 		if (vals[i] > UINT32_MAX)
// 			continue;

// 		printf("bit_index_u32(%" PRIu32 ", *, -1) => ", val32);
// 		test_index_print(indexes, bit_index_u32(val32, indexes, -1));
// 		printf("\n");
// 	}

// 	footer();
// }

// static void
// test_bit_iter(void)
// {
// 	header();

// 	struct bit_iterator it;
// 	uint64_t *data = vals + 6;
// 	size_t size = 10;

// 	size_t pos = 0;

// 	printf("Set: ");
// 	bit_iterator_init(&it, data, size, true);
// 	while ( (pos = bit_iterator_next(&it)) != SIZE_MAX) {
// 		printf("%zu, ", pos);
// 		fail_unless(bit_test(data, pos));
// 	}
// 	printf("\n");

// 	printf("Clear: ");
// 	bit_iterator_init(&it, data, size, false);
// 	while ( (pos = bit_iterator_next(&it)) != SIZE_MAX) {
// 		printf("%zu, ", pos);
// 		fail_if(bit_test(data, pos));
// 	}
// 	printf("\n");

// 	footer();
// }

// static void
// test_bit_iter_empty(void)
// {
// 	header();

// 	struct bit_iterator it;

// 	bit_iterator_init(&it, NULL, 0, true);
// 	fail_unless(bit_iterator_next(&it) == SIZE_MAX);

// 	bit_iterator_init(&it, NULL, 0, false);
// 	fail_unless(bit_iterator_next(&it) == SIZE_MAX);

// 	footer();
// }

// /**
//  * Check that bit iterator works correctly with bit sequences of size that are
//  * not multiple of uint64_t.
//  */
// static void
// test_bit_iter_fractional(void)
// {
// 	header();

// 	struct bit_iterator it;
// 	uint64_t data[2] = {UINT64_MAX, UINT64_MAX};

// 	for (size_t size = 1; size <= 16; size++) {
// 		bit_iterator_init(&it, &data, size, true);

// 		size_t expect_count = size * CHAR_BIT;
// 		size_t expect_pos = 0;
// 		size_t pos;

// 		while ( (pos = bit_iterator_next(&it)) != SIZE_MAX) {
// 			fail_unless(expect_count > 0);
// 			fail_unless(pos == expect_pos);
// 			expect_count--;
// 			expect_pos++;
// 		}

// 		fail_unless(expect_count == 0);
// 	}

// 	footer();
// }

// static void
// test_bitmap_size(void)
// {
// 	header();
// 	fail_unless(BITMAP_SIZE(1) == sizeof(long));
// 	fail_unless(BITMAP_SIZE(10) == sizeof(long));
// 	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT) == sizeof(long));
// 	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT + 1) == sizeof(long) * 2);
// 	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT * 4) == sizeof(long) * 4);
// 	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT * 4 - 1) == sizeof(long) * 4);
// 	fail_unless(BITMAP_SIZE(sizeof(long) * CHAR_BIT * 9 / 2) == sizeof(long) * 5);
// 	footer();
// }

// /**
//  * Check all possible valid inputs of `bit_set_range()'.
//  */
// static void
// test_bit_set_range(void)
// {
// 	header();

// 	const size_t data_size = 64; /* In bytes. */
// 	const size_t data_count = data_size * CHAR_BIT; /* In bits. */

// 	for (size_t pos = 0; pos < data_count; pos++) {
// 		for (size_t count = 0; count <= data_count - pos; count++) {
// 			for (int val = 0; val <= 1; val++) {
// 				uint8_t data[data_size];
// 				uint8_t ref[data_size];

// 				/* Initialize buffers. */
// 				memset(data, 0xA5, sizeof(data));
// 				memset(ref, 0xA5, sizeof(ref));
// 				/* Calculate reference result. */
// 				for (size_t i = pos; i < pos + count; i++) {
// 					if (val == 0)
// 						bit_clear(ref, i);
// 					else
// 						bit_set(ref, i);
// 				}
// 				/* The function under test. */
// 				bit_set_range(data, pos, count, val);
// 				/* Compare results. */
// 				fail_if(memcmp(data, ref, sizeof(data)) != 0);
// 			}
// 		}
// 	}

// 	footer();
// }

// /**
//  * Check all possible valid inputs of `bit_copy_range()`.
//  */
// static inline void
// test_bit_copy_range(bool src_val)
// {
// 	header();
// 	printf("Source value: %s\n", src_val ? "true" : "false");

// 	const size_t data_size = 64; /* In bytes. */
// 	const size_t data_count = data_size * CHAR_BIT; /* In bits. */
// 	const uint8_t src_byte = src_val ? 0xff : 0x00;
// 	const uint8_t dst_byte = src_val ? 0x00 : 0xff;

// 	uint8_t src[data_size];
// 	memset(src, src_byte, sizeof(src));
// 	for (size_t src_i = 0; src_i < data_count; src_i++) {
// 		for (size_t dst_i = 0; dst_i < data_count; dst_i++) {
// 			size_t src_max = data_count - src_i;
// 			size_t dst_max = data_count - dst_i;
// 			for (size_t c = 1; c <= src_max && c <= dst_max; c++) {
// 				uint8_t dst[data_size];
// 				uint8_t ref[data_size];

// 				/* Initialize the buffer. */
// 				memset(dst, dst_byte, sizeof(dst));
// 				/* Calculate the reference mask. */
// 				memset(ref, dst_byte, sizeof(ref));
// 				bit_set_range(ref, dst_i, c, src_val);
// 				/* The function under test. */
// 				bit_copy_range(dst, dst_i, src, src_i, c);
// 				/* Compare results. */
// 				fail_if(memcmp(dst, ref, sizeof(dst)) != 0);
// 			}
// 		}
// 	}

// 	footer();
// }

// static void
// random_bytes(size_t n, unsigned char *out)
// {
// 	for (size_t i = 0; i < n; i++) {
// 		out[i] = rand() % 256;
// 	}
// }

// static inline size_t
// bit_count_slow(const uint8_t *data, size_t bit_offset, size_t length)
// {
// 	size_t count = 0;
// 	for (size_t i = bit_offset; i < bit_offset + length; ++i) {
// 		if (bit_test(data, i)) {
// 			++count;
// 		}
// 	}
// 	return count;
// }

// /**
//  * This test is taken from Apache Arrow C++ library and modified.
//  * Please, see the NOTICE file.
//  */
// static void
// test_bit_count(void)
// {
// 	header();

// 	const size_t buf_size = 1000;
// 	alignas(8) unsigned char buf[buf_size];
// 	memset(buf, 0, sizeof(buf));
// 	const size_t buf_bits = buf_size * 8;

// 	random_bytes(buf_size, buf);

// 	/** Check start addresses with 64-bit alignment and without */
// 	size_t byte_offsets[3] = {0, 1, 7};
// 	size_t num_bits[3] = {buf_bits - 96, buf_bits - 101, buf_bits - 127};
// 	for (int i = 0; i < 3; i++) {
// 		for (int j = 0; j < 3; j++) {
// 			ssize_t offsets[] = {
// 				0, 12, 16, 32, 37, 63, 64, 128,
// 				num_bits[j] - 30, num_bits[j] - 64
// 			};
// 			for (size_t k = 0; k < lengthof(offsets); k++) {
// 				ssize_t offset = offsets[k];
// 				if (offset < 0 ||
// 				    (size_t)offset > num_bits[i]) {
// 					continue;
// 				}
// 				size_t result =
// 					bit_count(buf + byte_offsets[i],
// 						  (size_t)offset,
// 						  num_bits[i] - (size_t)offset);
// 				size_t expected =
// 					bit_count_slow(buf + byte_offsets[i],
// 						       (size_t)offset,
// 						       num_bits[i] -
// 								(size_t)offset);
// 				fail_if(result != expected);
// 			}
// 		}
// 	}

// 	footer();
// }

// int
// main(void)
// {
// 	plan(4);
// 	srand(time(NULL));

// 	test_ctz_clz();
// 	test_clz_least_significant();
// 	test_count();
// 	test_rotl_rotr();
// 	// test_bswap();
// 	// test_index();
// 	// test_bit_iter();
// 	// test_bit_iter_empty();
// 	// test_bit_iter_fractional();
// 	// test_bitmap_size();
// 	// test_bit_set_range();
// 	// test_bit_copy_range(true);
// 	// test_bit_copy_range(false);
// 	// test_bit_count();
// 	return check_plan();
// }
