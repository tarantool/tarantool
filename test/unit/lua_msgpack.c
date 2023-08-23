#include <lualib.h>

#include "core/assoc.h"
#include "core/cord_buf.h"
#include "core/fiber.h"
#include "core/memory.h"

#include "lua/msgpack.h"
#include "lua/serializer.h"

#include "mpstream/mpstream.h"

#include "small/ibuf.h"
#include "tt_uuid.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "lua_test_utils.h"

static void
mpstream_error_mock(void *ctx)
{
	(void)ctx;
}

/**
 * Checks encoding to `MP_EXT`.
 */
static void
test_encode_ext(lua_State *L)
{
	plan(2);
	header();

	struct mh_strnu32_t *translation = mh_strnu32_new();
	const char *alias = "x";
	struct mh_strnu32_node_t node = {
		.str = alias,
		.len = strlen(alias),
		.hash = lua_hash(alias, strlen(alias)),
		.val = 0
	};
	mh_strnu32_put(translation, &node, NULL, NULL);

	struct ibuf *ibuf = cord_ibuf_take();
	struct mpstream stream;
	mpstream_init(&stream, ibuf, ibuf_reserve_cb, ibuf_alloc_cb,
		      mpstream_error_mock, L);

	enum mp_type type;
	struct tt_uuid uuid;
	memset(&uuid, 0, sizeof(uuid));
	luaT_pushuuid(L, &uuid);
	luamp_encode_with_translation(L, luaL_msgpack_default, &stream, 1,
				      translation, &type);
	lua_pop(L, 1);
	mpstream_flush(&stream);
	const char *mp_expected = "\xd8\x02\x00\x00\x00\x00\x00\x00\x00"
				  "\x00\x00\x00\x00\x00\x00\x00\x00\x00";
	ok(strncmp(ibuf->buf, mp_expected, ibuf_used(ibuf)) == 0,
	   "UUID is correctly encoded as MP_EXT");
	ok(type == MP_EXT, "type of UUID is MP_EXT");
	ibuf_reset(ibuf);
	mpstream_reset(&stream);

	cord_ibuf_drop(ibuf);
	mh_strnu32_delete(translation);

	footer();
	check_plan();
}

/**
 * Checks that translation of first-level `MP_MAP` keys is done correctly.
 */
static void
test_translation_in_encoding(lua_State *L)
{
	plan(4);
	header();

	struct mh_strnu32_t *translation = mh_strnu32_new();
	const char *alias = "x";
	struct mh_strnu32_node_t node = {
		.str = alias,
		.len = strlen(alias),
		.hash = lua_hash(alias, strlen(alias)),
		.val = 0
	};
	mh_strnu32_put(translation, &node, NULL, NULL);

	struct ibuf *ibuf = cord_ibuf_take();
	struct mpstream stream;
	mpstream_init(&stream, ibuf, ibuf_reserve_cb, ibuf_alloc_cb,
		      mpstream_error_mock, L);

	lua_createtable(L, 0, 1);
	lua_pushboolean(L, true);
	lua_setfield(L, 1, alias);
	luamp_encode_with_translation(L, luaL_msgpack_default, &stream, 1,
				      translation, NULL);
	lua_pop(L, 1);
	mpstream_flush(&stream);
	ok(strncmp(ibuf->buf, "\x81\x00\xc3", ibuf_used(ibuf)) == 0,
	   "first-level MP_MAP key is translated");
	ibuf_reset(ibuf);
	mpstream_reset(&stream);

	lua_createtable(L, 0, 1);
	lua_createtable(L, 0, 1);
	lua_pushboolean(L, true);
	lua_setfield(L, -2, alias);
	lua_setfield(L, -2, "k");
	luamp_encode_with_translation(L, luaL_msgpack_default, &stream, 1,
				      translation, NULL);
	lua_pop(L, 1);
	mpstream_flush(&stream);
	ok(strncmp(ibuf->buf, "\x81\xa1k\x81\xa1x\xc3", ibuf_used(ibuf)) == 0,
	   "only first-level MP_MAP key is translated");
	ibuf_reset(ibuf);
	mpstream_reset(&stream);

	lua_createtable(L, 0, 1);
	lua_pushnumber(L, 0);
	lua_pushboolean(L, true);
	lua_settable(L, -3);
	luamp_encode_with_translation(L, luaL_msgpack_default, &stream, 1,
				      translation, NULL);
	mpstream_flush(&stream);
	ok(strncmp(ibuf->buf, "\x81\x00\xc3", ibuf_used(ibuf)) == 0,
	   "only keys with MP_STRING type are translated");
	ibuf_reset(ibuf);
	mpstream_reset(&stream);

	lua_createtable(L, 0, 1);
	lua_pushboolean(L, true);
	lua_setfield(L, 1, alias);
	lua_pushnumber(L, 0);
	lua_pushboolean(L, false);
	lua_settable(L, -3);
	luamp_encode_with_translation(L, luaL_msgpack_default, &stream, 1,
				      translation, NULL);
	lua_pop(L, 1);
	mpstream_flush(&stream);
	ok(strncmp(ibuf->buf, "\x82\x00\xc2\x00\xc3", ibuf_used(ibuf)) == 0,
	   "first-level MP_MAP key that has translation along with first-level "
	   "MP_MAP key that is the value of the translation are translated "
	   "correctly");
	ibuf_reset(ibuf);
	mpstream_reset(&stream);

	cord_ibuf_drop(ibuf);
	mh_strnu32_delete(translation);

	footer();
	check_plan();
}

/**
 * Checks that MsgPack object with dictionaries work correctly.
 */
static void
test_translation_in_indexation(struct lua_State *L)
{
	plan(1);
	header();

	struct mh_strnu32_t *translation = mh_strnu32_new();
	const char *alias = "alias";
	uint32_t key = 0;
	struct mh_strnu32_node_t node = {
		.str = alias,
		.len = strlen(alias),
		.hash = lua_hash(alias, strlen(alias)),
		.val = key,
	};
	mh_strnu32_put(translation, &node, NULL, NULL);

	char buf[64];

	char *w = mp_encode_map(buf, 1);

	w = mp_encode_uint(w, key);
	w = mp_encode_bool(w, true);
	luamp_push_with_translation(L, buf, w, translation);
	lua_getfield(L, -1, alias);
	ok(lua_toboolean(L, -1), "string key is aliased");
	lua_pop(L, 2);

	lua_gc(L, LUA_GCCOLLECT, 0);
	mh_strnu32_delete(translation);

	footer();
	check_plan();
}

int
main(void)
{
	plan(3);
	header();

	struct lua_State *L = luaT_newteststate();
	tarantool_L = L;
	memory_init();
	fiber_init(fiber_c_invoke);

	tarantool_lua_error_init(L);
	tarantool_lua_utils_init(L);
	tarantool_lua_serializer_init(L);
	luaopen_msgpack(L);
	lua_pop(L, 1);

	test_encode_ext(L);
	test_translation_in_encoding(L);
	test_translation_in_indexation(L);

	fiber_free();
	memory_free();
	lua_close(L);
	tarantool_L = NULL;

	footer();
	return check_plan();
}
