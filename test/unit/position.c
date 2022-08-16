#include "unit.h"
#include "position.h"
#include "msgpuck.h"
#include "random.h"
#include "time.h"

#define KEY_BUF_LEN 50
#define POS_BUF_LEN (KEY_BUF_LEN + 20)

char key_buf[KEY_BUF_LEN];
char pos_buf[POS_BUF_LEN];

static uint32_t
pack_random_key(uint32_t size)
{
	char *buf = key_buf;
	buf = mp_encode_array(buf, size);
	for (uint32_t i = 0; i < size; ++i) {
		uint32_t key = rand();
		buf = mp_encode_uint(buf, key);
	}
	return buf - key_buf;
}

static void
simple_check()
{
	header();

	uint32_t size = rand() % 8 + 2;
	uint32_t key_len = pack_random_key(size);
	struct position pos;
	pos.key = key_buf;
	pos.key_size = key_len;
	uint32_t pack_size = position_pack_size(&pos);
	assert(POS_BUF_LEN >= pack_size);
	position_pack(&pos, pos_buf);
	int rc = position_unpack(pos_buf, &pos);
	ok(rc == 0, "Position must be unpacked");
	ok(pos.key_size == key_len && memcmp(key_buf, pos.key, key_len) == 0,
	   "Keys must match");

	/** Invalidate position and try to unpack. */
	ptrdiff_t offset = pos.key - pos_buf;
	mp_encode_map(pos_buf + offset, size);
	rc = position_unpack(pos_buf, &pos);
	ok(rc != 0, "Invalid position must not be unpacked");
	mp_encode_strl(pos_buf + offset, size);
	ok(rc != 0, "Invalid position must not be unpacked");

	footer();
}

static void
unpack_invalid_check()
{
	header();

	uint32_t size = rand() % 8 + 2;
	uint32_t key_len = pack_random_key(size);
	struct position pos;
	struct position ret;
	int rc = 0;

	pos.key = NULL;
	pos.key_size = 0;
	pos_buf[0] = 0;
	position_pack(&pos, pos_buf);
	ok(pos_buf[0] == 0, "Empty position must not be packed");

	pos.key = key_buf;
	pos.key_size = key_len;

	position_pack(&pos, pos_buf);
	mp_encode_strl(pos_buf, pos.key_size - 2);
	rc = position_unpack(pos_buf, &ret);
	ok(rc != 0, "Position which is not MP_BIN must not be unpacked");

	position_pack(&pos, pos_buf);
	mp_encode_array(pos_buf + 2, 1);
	rc = position_unpack(pos_buf, &ret);
	ok(rc != 0, "Position which is not MP_MAP must not be unpacked");

	position_pack(&pos, pos_buf);
	pos_buf[3] = 1;
	rc = position_unpack(pos_buf, &ret);
	ok(rc != 0, "Position with invalid map key must not be unpacked");

	position_pack(&pos, pos_buf);
	mp_encode_map(pos_buf + 4, size);
	rc = position_unpack(pos_buf, &ret);
	ok(rc != 0, "Position with key that isn't array must not be unpacked");

	footer();
}

int
main(void)
{
	plan(9);
	srand(time(NULL));
	simple_check();
	unpack_invalid_check();
	check_plan();
}
