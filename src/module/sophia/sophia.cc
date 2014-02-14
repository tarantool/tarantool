
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

#include <sophia.h>

extern "C" {
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
}
#include <lua/init.h>
#include <coeio.h>

static const char lsophia_name[] = "box.sophia";

struct lsophia {
	void *env;
	void *db;
};

static inline struct lsophia*
lsophia_check(struct lua_State *L, int narg)
{
	if (lua_gettop(L) < narg)
		luaL_error(L, "%s: incorrect method call", lsophia_name);
	return (struct lsophia*)luaL_checkudata(L, narg, lsophia_name);
}

static int
lsophia_create(struct lua_State *L)
{
	struct lsophia *s = (struct lsophia*)
		lua_newuserdata(L, sizeof(struct lsophia));
	luaL_getmetatable(L, lsophia_name);
	lua_setmetatable(L, -2);
	s->db  = NULL;
	s->env = sp_env();
	if (s->env == NULL)
		luaL_error(L, "%s.create: failed to create env", lsophia_name);
	return 1;
}

static int
lsophia_error(struct lua_State *L)
{
	struct lsophia *s = lsophia_check(L, -1);
	lua_pushstring(L, sp_error(s->env));
	return 0;
}

static ssize_t
lsophia_openfunc(va_list ap)
{
	struct lsophia *s = va_arg(ap, struct lsophia*);
	s->db = sp_open(s->env);
	return (s->db) ? 0 : -1;
}

static int
lsophia_open(struct lua_State *L)
{
	struct lsophia *s = lsophia_check(L, -1);
	int rc = coeio_custom(lsophia_openfunc, TIMEOUT_INFINITY, s);
	lua_pushinteger(L, rc);
	return 1;
}

static ssize_t
lsophia_closedbfunc(va_list ap)
{
	struct lsophia *s = va_arg(ap, struct lsophia*);
	return sp_destroy(s->db);
}

static int
lsophia_close(struct lua_State *L)
{
	int rcret = 0;
	int rc;
	struct lsophia *s = lsophia_check(L, -1);
	if (s->db) {
		rc = coeio_custom(lsophia_closedbfunc, TIMEOUT_INFINITY, s);
		if (rc == -1)
			rcret = -1;
		s->db = NULL;
	}
	if (s->env) {
		rc = sp_destroy(s->env);
		if (rc == -1)
			rcret = -1;
		s->env = NULL;
	}
	lua_pushinteger(L, rcret);
	return 1;
}

static int
lsophia_ctl(struct lua_State *L)
{
	struct lsophia *s = lsophia_check(L, 1);
	spopt opt = (spopt)luaL_checkint(L, 2);
	int rc = 0;
	switch (opt) {
	case SPDIR:
		rc = sp_ctl(s->env, opt, luaL_checkinteger(L, 3),
		            luaL_checkstring(L, 4));
		break;
	case SPPAGE:
	case SPGC:
	case SPMERGE:
	case SPMERGEWM:
		rc = sp_ctl(s->env, opt, luaL_checkinteger(L, 3));
		break;
	case SPGCF:
		rc = sp_ctl(s->env, opt, luaL_checknumber(L, 3));
	case SPMERGEFORCE:
		if (s->db == NULL)
			luaL_error(L, "%s:ctl: db must be open", lsophia_name);
		rc = sp_ctl(s->db, opt);
		break;
	case SPALLOC:
	case SPCMP:
	case SPGROW:
	case SPVERSION:
		break;
	default:
		luaL_error(L, "%s:ctl: bad ctl argument", lsophia_name);
		break;
	}
	lua_pushinteger(L, rc);
	return 1;
}

static ssize_t
lsophia_setfunc(va_list ap)
{
	struct lsophia *s = va_arg(ap, struct lsophia*);
	const char *key = va_arg(ap, const char*);
	size_t keysize = va_arg(ap, size_t);
	const char *value = va_arg(ap, const char*);
	size_t valuesize = va_arg(ap, size_t);

	return sp_set(s->db, key, keysize, value, valuesize);
}

static int
lsophia_set(struct lua_State *L)
{
	struct lsophia *s = lsophia_check(L, 1);
	if (s->db == NULL)
		luaL_error(L, "%s:set: db must be open", lsophia_name);
	size_t keysize = 0;
	size_t valuesize = 0;
	const char *key = luaL_checklstring(L, 2, &keysize);
	const char *value =
		luaL_checklstring(L, 3, &valuesize);
	int rc = coeio_custom(lsophia_setfunc, TIMEOUT_INFINITY, s,
	                      key, keysize,
	                      value, valuesize);
	lua_pushinteger(L, rc);
	return 1;
}

static ssize_t
lsophia_deletefunc(va_list ap)
{
	struct lsophia *s = va_arg(ap, struct lsophia*);
	const char *key = va_arg(ap, const char*);
	size_t keysize = va_arg(ap, size_t);

	return sp_delete(s->db, key, keysize);
}

static int
lsophia_delete(struct lua_State *L)
{
	struct lsophia *s = lsophia_check(L, 1);
	if (s->db == NULL)
		luaL_error(L, "%s:delete: db must be open", lsophia_name);
	size_t keysize = 0;
	const char *key = luaL_checklstring(L, 2, &keysize);
	int rc = coeio_custom(lsophia_deletefunc, TIMEOUT_INFINITY, s,
	                      key, keysize);
	lua_pushinteger(L, rc);
	return 1;
}

static ssize_t
lsophia_getfunc(va_list ap)
{
	struct lsophia *s = va_arg(ap, struct lsophia*);
	const char *key = va_arg(ap, const char*);
	size_t keysize = va_arg(ap, size_t);
	void **value = va_arg(ap, void**);
	size_t *valuesize = va_arg(ap, size_t*);

	return sp_get(s->db, key, keysize, value, valuesize);
}

static int
lsophia_get(struct lua_State *L)
{
	struct lsophia *s = lsophia_check(L, 1);
	if (s->db == NULL)
		luaL_error(L, "%s:get: db must be open", lsophia_name);
	size_t keysize = 0;
	const char *key = luaL_checklstring(L, 2, &keysize);
	size_t valuesize = 0;
	void *value = NULL;
	int rc = coeio_custom(lsophia_getfunc, TIMEOUT_INFINITY, s,
	                      key, keysize,
	                      &value, &valuesize);
	if (rc <= 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, (char*)value, valuesize);
	free(value);
	return 1;
}

static void
lsophia_initconst(lua_State *L)
{
	lua_pushstring(L, "SPDIR");
	lua_pushnumber(L, SPDIR);
	lua_settable(L, -3);

	lua_pushstring(L, "SPPAGE");
	lua_pushnumber(L, SPPAGE);
	lua_settable(L, -3);

	lua_pushstring(L, "SPGC");
	lua_pushnumber(L, SPGC);
	lua_settable(L, -3);

	lua_pushstring(L, "SPGCF");
	lua_pushnumber(L, SPGCF);
	lua_settable(L, -3);

	lua_pushstring(L, "SPMERGE");
	lua_pushnumber(L, SPMERGE);
	lua_settable(L, -3);

	lua_pushstring(L, "SPMERGEWM");
	lua_pushnumber(L, SPMERGEWM);
	lua_settable(L, -3);

	lua_pushstring(L, "SPMERGEFORCE");
	lua_pushnumber(L, SPMERGEFORCE);
	lua_settable(L, -3);

	lua_pushstring(L, "SPMERGEFORCE");
	lua_pushnumber(L, SPMERGEFORCE);
	lua_settable(L, -3);

	lua_pushstring(L, "SPO_RDONLY");
	lua_pushnumber(L, SPO_RDONLY);
	lua_settable(L, -3);

	lua_pushstring(L, "SPO_RDWR");
	lua_pushnumber(L, SPO_RDWR);
	lua_settable(L, -3);

	lua_pushstring(L, "SPO_CREAT");
	lua_pushnumber(L, SPO_CREAT);
	lua_settable(L, -3);
}

extern "C" {
	int LUA_API luaopen_box_sophia(lua_State*);
}

int LUA_API luaopen_box_sophia(lua_State *L)
{
	static const struct luaL_reg lsophia_meta[] =
	{
		{ "__gc",   lsophia_close  },
		{ "error",  lsophia_error  },
		{ "open",   lsophia_open   },
		{ "close",  lsophia_close  },
		{ "ctl",    lsophia_ctl    },
		{ "set",    lsophia_set    },
		{ "delete", lsophia_delete },
		{ "get",    lsophia_get    },
		{ NULL,     NULL           }
	};
	tarantool_lua_register_type(L, lsophia_name, lsophia_meta);
	struct luaL_reg driver[] = {
		{"create", lsophia_create},
		{NULL, NULL}
	};
	luaL_openlib(L, "box.sophia", driver, 0);

	lsophia_initconst(L);
	return 1;
}
