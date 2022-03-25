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

#include <string.h>
#include <lua/digest.h>
#include <sha1.h>
#include <openssl/evp.h>
#include <coio_task.h>
#include <lua.h>
#include <lauxlib.h>
#include "utils.h"
#include "crc32.h"

#define PBKDF2_MAX_DIGEST_SIZE 128

unsigned char *
SHA1internal(const unsigned char *d, size_t n, unsigned char *md)
{
	static __thread unsigned char result[20];
	SHA1_CTX ctx;
	SHA1Init(&ctx);
	SHA1Update(&ctx, d, n);
	SHA1Final(result, &ctx);

	if (md)
		memcpy(md, result, 20);
	return result;
}

static ssize_t
digest_pbkdf2_f(va_list ap)
{
	char *password = va_arg(ap, char *);
	size_t password_size = va_arg(ap, size_t);
	const unsigned char *salt = va_arg(ap, unsigned char *);
	size_t salt_size = va_arg(ap, size_t);
	unsigned char *digest = va_arg(ap, unsigned char *);
	int num_iterations = va_arg(ap, int);
	int digest_len = va_arg(ap, int);
	if (PKCS5_PBKDF2_HMAC(password, password_size, salt, salt_size,
						  num_iterations, EVP_sha256(),
						  digest_len, digest) == 0) {
		return -1;
	}
	return 0;
}

int
lua_pbkdf2(lua_State *L)
{
	const char *password = lua_tostring(L, 1);
	const char *salt = lua_tostring(L, 2);
	int num_iterations = lua_tointeger(L, 3);
	int digest_len = lua_tointeger(L, 4);
	unsigned char digest[PBKDF2_MAX_DIGEST_SIZE];

	if (coio_call(digest_pbkdf2_f, password, strlen(password), salt,
				  strlen(salt), digest, num_iterations, digest_len) < 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, (char *) digest, digest_len);
	return 1;
}

/* CRC32 internal {{{ */

int
crc32_methods_update(lua_State *L)
{
	size_t strl;
	/* Get <string>. */
	const char *str = lua_tolstring(L, 2, &strl);
	if (str == NULL)
		luaL_error(L, "Usage crc32:update(string)");
	/* Get <self.value>. */
	lua_getfield(L, 1, "value");
	uint32_t crc32_begin = lua_tointeger(L, -1);
	uint32_t crc32_result = crc32_calc(crc32_begin, str,
					   (unsigned int)strl);
	/* Push the result. */
	lua_pushinteger(L, crc32_result);
	/* Set <self.value>. */
	lua_setfield(L, 1, "value");
	return 0;
}

int
crc32___call(lua_State *L)
{
	size_t strl;
	/* Get <string>. */
	const char *str = lua_tolstring(L, 2, &strl);
	if (str == NULL)
		luaL_error(L, "Usage digest.crc32(string)");
	/* Get <CRC32> upvalue. */
	lua_pushvalue(L, lua_upvalueindex(1));
	/* Get <CRC32.crc_begin>. */
	lua_getfield(L, -1, "crc_begin");
	uint32_t crc32_begin = lua_tointeger(L, -1);
	uint32_t crc32_result = crc32_calc(crc32_begin, str,
					   (unsigned int)strl);
	/* Push the result. */
	lua_pushinteger(L, crc32_result);
	return 1;
}

int
crc32_internal_init(lua_State *L)
{
	/* Create the table with internal methods */
	lua_createtable(L, 0, 2);
	/* Copy <CRC32> on the top of the stack. */
	lua_pushvalue(L, 1);
	/* Create function <__call>. */
	lua_pushcclosure(L, crc32___call, 1);
	/* Store it to the table with internal methods. */
	lua_setfield(L, -2, "__call");
	/* Create function <update>. */
	lua_pushcfunction(L, crc32_methods_update);
	/* Store it to the table with internal methods. */
	lua_setfield(L, -2, "update");
	/* Copy the table with internal methods on the top of the stack. */
	lua_pushvalue(L, -1);
	/* Get the <crc32.internal> field from <package.loaded>. */
	lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
	/* Set the table with internal methods in <package.loaded>. */
	lua_setfield(L, -2, "crc32.internal");
	/* Return the table with internal methods */
	return 1;
}

LUA_API int
luaopen_crc32_internal(lua_State *L)
{
	lua_pushcfunction(L, crc32_internal_init);
	return 1;
}

/* }}} */

void
tarantool_lua_digest_init(struct lua_State *L)
{
	static const struct luaL_Reg lua_digest_methods [] = {
		{"pbkdf2", lua_pbkdf2},
		{NULL, NULL}
	};
	lua_getfield(L, LUA_REGISTRYINDEX, "_PRELOAD");
	lua_pushcfunction(L, luaopen_crc32_internal);
	lua_setfield(L, -2, "crc32.internal");
	lua_pop(L, 1);
	luaL_register_module(L, "digest", lua_digest_methods);
	lua_pop(L, 1);
};
