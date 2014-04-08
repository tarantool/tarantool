/*
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
#include "cfg.h"
#include "lua/utils.h"

enum { MAX_OPT_NAME_LEN = 256, MAX_OPT_VAL_LEN = 256, MAX_STR_OPTS = 8 };

static void
cfg_get(const char *param)
{
	char buf[MAX_OPT_NAME_LEN];
	snprintf(buf, sizeof(buf), "return box.cfg.%s", param);
	luaL_dostring(tarantool_L, buf);
}

int
cfg_geti(const char *param)
{
	cfg_get(param);
	int val = lua_tointeger(tarantool_L, -1);
	lua_pop(tarantool_L, 1);
	return val;
}

const char *
cfg_gets(const char *param)
{
	/* Support simultaneous cfg_gets("str1") and cfg_gets("str2") */
	static char __thread values[MAX_STR_OPTS][MAX_OPT_VAL_LEN];
	static int __thread i = 0;
	struct lua_State *L = tarantool_L;
	char *val;
	cfg_get(param);
	if (lua_isnil(L, -1))
		val = NULL;
	else {
		val = values[i++ % MAX_STR_OPTS];
		snprintf(val, MAX_OPT_VAL_LEN, "%s", lua_tostring(L, -1));
	}
	lua_pop(L, 1);
	return val;
}

double
cfg_getd(const char *param)
{
	cfg_get(param);
	double val = lua_tonumber(tarantool_L, -1);
	lua_pop(tarantool_L, 1);
	return val;
}

