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
static const char *key_buf_no_header;
static char pos_buf[POS_BUF_LEN];

/**
 * Pack random key to key_buf, key_buf_no_header points to key
 * without MP_ARRAY header.
 */
static uint32_t
pack_random_key(uint32_t size)
{
	char *buf = key_buf;
	buf = mp_encode_array(buf, size);
	key_buf_no_header = buf;
	for (uint32_t i = 0; i < size; ++i) {
		uint32_t key = real_random();
		buf = mp_encode_uint(buf, key);
	}
	return buf - key_buf_no_header;
}

static void
simple_check(void)
{
	plan(4);
	header();

	uint32_t part_count = real_random_in_range(1, 10);
	uint32_t key_len = pack_random_key(part_count);
	uint32_t pack_size =
		iterator_position_pack_size(key_buf_no_header,
					    key_buf_no_header + key_len,
					    part_count);
	/* Fail if size of buffer is not enough for the test. */
	fail_if(pack_size > POS_BUF_LEN);
	iterator_position_pack(key_buf_no_header, key_buf_no_header + key_len,
			       part_count, pos_buf, pos_buf + pack_size);
	const char *begin, *end;
	uint32_t pos_part_count;
	int rc = iterator_position_unpack(pos_buf, pos_buf + pack_size,
					  &begin, &end, &pos_part_count);
	ok(rc == 0, "Position must be unpacked");
	ok(end - begin == key_len &&
	   part_count == pos_part_count &&
	   memcmp(key_buf_no_header, begin, key_len) == 0,
	   "Keys must match");

	/*
	 * Invalidate position (change MP_ARRAY header of key to another one)
	 * and try to unpack.
	 */
	ptrdiff_t offset = begin - pos_buf - 1;
	mp_encode_map(pos_buf + offset, part_count);
	rc = iterator_position_unpack(pos_buf, pos_buf + pack_size,
				      &begin, &end, &pos_part_count);
	ok(rc != 0, "Invalid position must not be unpacked");
	mp_encode_strl(pos_buf + offset, key_len);
	rc = iterator_position_unpack(pos_buf, pos_buf + pack_size,
				      &begin, &end, &pos_part_count);
	ok(rc != 0, "Invalid position must not be unpacked");

	footer();
	check_plan();
}

static void
unpack_invalid_check(void)
{
	plan(4);
	header();

	uint32_t part_count = real_random_in_range(1, 10);
	uint32_t key_len = pack_random_key(part_count);
	int rc = 0;

	const char *begin, *end;
	uint32_t pc;

	uint32_t pack_size = iterator_position_pack_size(key_buf,
							 key_buf + key_len,
							 part_count);
	const char *pos_buf_end = pos_buf + pack_size;
	fail_if(pack_size > POS_BUF_LEN);
	iterator_position_pack(key_buf, key_buf + key_len, part_count,
			       pos_buf, pos_buf_end);
	mp_encode_strl(pos_buf, key_len);
	rc = iterator_position_unpack(pos_buf, pos_buf_end, &begin, &end, &pc);
	ok(rc != 0, "Position which is not MP_BIN must not be unpacked");

	iterator_position_pack(key_buf, key_buf + key_len, part_count,
			       pos_buf, pos_buf_end);
	mp_encode_array(pos_buf + 2, 1);
	rc = iterator_position_unpack(pos_buf, pos_buf_end, &begin, &end, &pc);
	ok(rc != 0, "Position which is not MP_MAP must not be unpacked");

	iterator_position_pack(key_buf, key_buf + key_len, part_count,
			       pos_buf, pos_buf_end);
	pos_buf[3] = 1;
	rc = iterator_position_unpack(pos_buf, pos_buf_end, &begin, &end, &pc);
	ok(rc != 0, "Position with invalid map key must not be unpacked");

	iterator_position_pack(key_buf, key_buf + key_len, part_count,
			       pos_buf, pos_buf_end);
	mp_encode_map(pos_buf + 4, part_count);
	rc = iterator_position_unpack(pos_buf, pos_buf_end, &begin, &end, &pc);
	ok(rc != 0, "Position with key that isn't array must not be unpacked");

	footer();
	check_plan();
}

static void
cropped_buffer_check(void)
{
	plan(5);
	header();

	uint32_t part_count = real_random_in_range(2, 10);
	uint32_t key_len = pack_random_key(part_count);
	int rc = 0;

	const char *begin, *end;
	uint32_t pc;

	uint32_t pack_size = iterator_position_pack_size(key_buf,
							 key_buf + key_len,
							 part_count);
	const char *pos_buf_end = pos_buf + pack_size;
	fail_if(pack_size > POS_BUF_LEN);
	iterator_position_pack(key_buf, key_buf + key_len, part_count,
			       pos_buf, pos_buf_end);
	rc = iterator_position_unpack(pos_buf, pos_buf + 1, &begin, &end, &pc);
	ok(rc != 0, "Position with cropped MP_BIN must not be unpacked");

	rc = iterator_position_unpack(pos_buf, pos_buf + 2, &begin, &end, &pc);
	ok(rc != 0, "Position with cropped MP_MAP must not be unpacked");

	rc = iterator_position_unpack(pos_buf, pos_buf + 3, &begin, &end, &pc);
	ok(rc != 0, "Position with cropped map key must not be unpacked");

	rc = iterator_position_unpack(pos_buf, pos_buf + 4, &begin, &end, &pc);
	ok(rc != 0, "Position with cropped MP_ARRAY must not be unpacked");

	rc = iterator_position_unpack(pos_buf, pos_buf + 5, &begin, &end, &pc);
	ok(rc != 0, "Position with cropped key must not be unpacked");

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
