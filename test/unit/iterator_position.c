#include "fiber.h"
#include "memory.h"
#include "msgpuck.h"
#include "index.h"
#include "random.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#define KEY_BUF_LEN 100
#define POS_BUF_LEN (KEY_BUF_LEN + 20)

static char key_buf[KEY_BUF_LEN];
static char pos_buf[POS_BUF_LEN];
static char unpack_buf[KEY_BUF_LEN];

/**
 * Pack random key to key_buf, key_buf_no_header points to key
 * without MP_ARRAY header.
 */
static uint32_t
pack_random_key(uint32_t size)
{
	char *buf = key_buf;
	buf = mp_encode_array(buf, size);
	for (uint32_t i = 0; i < size; ++i) {
		uint32_t key = real_random();
		buf = mp_encode_uint(buf, key);
	}
	return buf - key_buf;
}

static void
simple_check(void)
{
	plan(2);
	header();

	uint32_t part_count = real_random_in_range(1, 10);
	uint32_t key_len = pack_random_key(part_count);
	uint32_t pack_size =
		iterator_position_pack_bufsize(key_buf, key_buf + key_len);
	/* Fail if size of buffer is not enough for the test. */
	fail_if(pack_size > POS_BUF_LEN);
	const char *packed_pos, *packed_pos_end;
	iterator_position_pack(key_buf, key_buf + key_len,
			       pos_buf, pack_size, &packed_pos,
			       &packed_pos_end);
	const char *begin, *end;
	int rc = iterator_position_unpack(packed_pos, packed_pos_end,
					  unpack_buf, KEY_BUF_LEN,
					  &begin, &end);
	ok(rc == 0, "Position must be unpacked");
	ok(end - begin == key_len &&
	   memcmp(key_buf, begin, key_len) == 0,
	   "Keys must match");

	footer();
	check_plan();
}

static void
unpack_invalid_check(void)
{
	plan(1);
	header();

	uint32_t part_count = real_random_in_range(2, 7);
	uint32_t key_len = pack_random_key(part_count);
	int rc = 0;

	const char *begin, *end;
	const char *packed_pos, *packed_pos_end;

	uint32_t pack_size = iterator_position_pack_bufsize(key_buf,
							    key_buf + key_len);
	iterator_position_pack(key_buf, key_buf + key_len, pos_buf, pack_size,
			       &packed_pos, &packed_pos_end);

	pos_buf[0] = 0;
	rc = iterator_position_unpack(packed_pos, packed_pos_end, unpack_buf,
				      KEY_BUF_LEN, &begin, &end);

	ok(rc != 0, "Position without MP_ARRAY header must not be unpacked");

	footer();
	check_plan();
}

static void
cropped_buffer_check(void)
{
	plan(1);
	header();

	uint32_t part_count = real_random_in_range(2, 10);
	uint32_t key_len = pack_random_key(part_count);
	int rc = 0;

	const char *begin, *end;
	const char *packed_pos, *packed_pos_end;

	uint32_t pack_size = iterator_position_pack_bufsize(key_buf,
							    key_buf + key_len);
	fail_if(pack_size > POS_BUF_LEN);
	iterator_position_pack(key_buf, key_buf + key_len,
			       pos_buf, pack_size, &packed_pos,
			       &packed_pos_end);
	bool unpacked = false;
	for (uint32_t i = 1; packed_pos + i < packed_pos_end && !unpacked;
	     ++i) {
		rc = iterator_position_unpack(packed_pos, packed_pos + i,
					      unpack_buf, KEY_BUF_LEN,
					      &begin, &end);
		unpacked = unpacked || rc == 0;
	}

	ok(!unpacked, "Position with cropped map key must not be unpacked");

	footer();
	check_plan();
}

static int
test_main(void)
{
	plan(3);
	header();

	simple_check();
	unpack_invalid_check();
	cropped_buffer_check();

	footer();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	random_init();

	int rc = test_main();

	random_free();
	fiber_free();
	memory_free();
	return rc;
}
