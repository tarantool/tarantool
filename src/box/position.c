#include "position.h"
#include "msgpuck.h"
#include "trivia/util.h"

/*
 * Although the structure in C is very simple, it has a more
 * complex format in MsgPack:
 * +--------+--------+--------------+========================+
 * | MP_BIN | MP_MAP | POSITION_KEY | KEY IN MP_ARRAY FORMAT |
 * +--------+--------+--------------+========================+
 * MP_BIN - needed to make the object opaque to users working
 * directly with IPROTO.
 * MP_MAP - needed for extensibility (at least, we have an idea
 * to generate a digital signature to make sure that user did
 * not modify the object).
 * All the keys of map should be unsigned integer values to
 * minimize the size of the object.
 */

/** Keys for position map. All the keys must be uint. */
enum {
	/** There must be MP_ARRAY after this key, UB otherwise. */
	POSITION_KEY,
	POSITION_MAX,
};

uint32_t
position_pack_size(struct position *pos)
{
	assert(pos != NULL);
	if (pos->key == NULL) {
		assert(pos->key_size == 0);
		return 0;
	}

	assert(pos->key_size != 0);
	uint32_t total = pos->key_size;
	total += mp_sizeof_uint(POSITION_KEY);
	total += mp_sizeof_map(POSITION_MAX);
	total += mp_sizeof_binl(total);
	return total;
}

void
position_pack(struct position *pos, char *buffer)
{
	assert(pos != NULL);
	assert(buffer != NULL);
	if (pos->key == NULL) {
		assert(pos->key_size == 0);
		return;
	}
	assert(mp_typeof(pos->key[0]) == MP_ARRAY);

	uint32_t map_len = pos->key_size;
	map_len += mp_sizeof_uint(POSITION_KEY);
	map_len += mp_sizeof_map(POSITION_MAX);
	buffer = mp_encode_binl(buffer, map_len);
	buffer = mp_encode_map(buffer, POSITION_MAX);
	buffer = mp_encode_uint(buffer, POSITION_KEY);
	memcpy(buffer, pos->key, pos->key_size);
}

int
position_unpack(const char *ptr, struct position *pos)
{
	assert(ptr != NULL);
	assert(pos != NULL);

	pos->key = NULL;
	pos->key_size = 0;
	const char **cptr = &ptr;
	if (unlikely(mp_typeof(*ptr) != MP_BIN))
		return -1;
	mp_decode_binl(cptr);
	if (unlikely(mp_typeof(*ptr) != MP_MAP))
		return -1;
	uint32_t map_len = mp_decode_map(cptr);
	if (unlikely(map_len > 1))
		return -1;
	for (uint32_t i = 0; i < map_len; ++i) {
		if (unlikely(mp_typeof(*ptr) != MP_UINT))
			return -1;
		uint32_t key = mp_decode_uint(cptr);
		switch (key) {
		case POSITION_KEY:
			if (unlikely(mp_typeof(*ptr) != MP_ARRAY))
				return -1;
			pos->key = ptr;
			mp_next(cptr);
			pos->key_size = ptr - pos->key;
			break;
		default:
			return -1;
		}
	}
	return 0;
}
