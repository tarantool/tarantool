/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <unistd.h>
#include <string.h>
#include <lua.h>

#include "box/error.h"
#include "box/port.h"
#include "box/module_cache.h"

#include "assoc.h"
#include "lib.h"
#include "diag.h"

#include "lua/utils.h"

/**
 * Function descriptor.
 */
struct box_module_func {
	/** C function to call. */
	struct module_func base;
	/** Number of references. */
	int64_t refs;
	/** Length of functon name in @a key. */
	size_t sym_len;
	/** Length of @a key. */
	size_t len;
	/** Function hash key. */
	char key[0];
};

/** Function name to box_module_func hash. */
static struct mh_strnptr_t *func_hash = NULL;

/** A type to find a module from an object. */
static const char *uname_lib = "tt_uname_box_lib";

/** A type to find a function from an object. */
static const char *uname_func = "tt_uname_box_lib_func";

/** Get data associated with an object. */
static void *
get_udata(struct lua_State *L, const char *uname)
{
	void **pptr = luaL_testudata(L, 1, uname);
	return pptr != NULL ? *pptr : NULL;
}

/**
 * Get pointer associated with an object and clear it
 * returning previously associated data.
 */
static void *
clear_udata(struct lua_State *L, const char *uname)
{
	void **pptr = luaL_testudata(L, 1, uname);
	if (pptr != NULL) {
		void *ptr = *pptr;
		*pptr = NULL;
		return ptr;
	}
	return NULL;
}

/** Setup a new data and associate it with an object. */
static void
new_udata(struct lua_State *L, const char *uname, void *ptr)
{
	*(void **)lua_newuserdata(L, sizeof(void *)) = ptr;
	luaL_getmetatable(L, uname);
	lua_setmetatable(L, -2);
}

/**
 * Helpers for function cache.
 */
static void *
cache_find(const char *str, size_t len)
{
	mh_int_t e = mh_strnptr_find_inp(func_hash, str, len);
	if (e == mh_end(func_hash))
		return NULL;
	return mh_strnptr_node(func_hash, e)->val;
}

static int
cache_put(struct box_module_func *cf)
{
	const struct mh_strnptr_node_t nd = {
		.str	= cf->key,
		.len	= cf->len,
		.hash	= mh_strn_hash(cf->key, cf->len),
		.val	= cf,
	};

	struct mh_strnptr_node_t prev;
	struct mh_strnptr_node_t *prev_ptr = &prev;

	mh_int_t e = mh_strnptr_put(func_hash, &nd, &prev_ptr, NULL);
	if (e == mh_end(func_hash)) {
		diag_set(OutOfMemory, sizeof(nd), "malloc",
			 "box.lib: hash node");
		return -1;
	}

	/*
	 * Just to make sure we haven't replaced something,
	 * the entries must be explicitly deleted.
	 */
	assert(prev_ptr == NULL);
	return 0;
}

static void
cache_del(struct box_module_func *cf)
{
	mh_int_t e = mh_strnptr_find_inp(func_hash, cf->key, cf->len);
	if (e != mh_end(func_hash))
		mh_strnptr_del(func_hash, e, NULL);
}

/**
 * Load a module.
 *
 * This function takes a module path from the caller
 * stack @a L and returns cached module instance or
 * creates a new module object.
 *
 * Possible errors:
 *
 * - IllegalParams: module path is either not supplied
 *   or not a string.
 * - SystemError: unable to open a module due to a system error.
 * - ClientError: a module does not exist.
 * - OutOfMemory: unable to allocate a module.
 *
 * @returns module object on success or throws an error.
 */
static int
lbox_module_load(struct lua_State *L)
{
	const char *msg_noname = "Expects box.lib.load(\'name\') "
		"but no name passed";

	if (lua_gettop(L) != 1 || !lua_isstring(L, 1)) {
		diag_set(IllegalParams, msg_noname);
		return luaT_error(L);
	}

	size_t name_len;
	const char *name = lua_tolstring(L, 1, &name_len);

	if (name_len < 1) {
		diag_set(IllegalParams, msg_noname);
		return luaT_error(L);
	}

	struct module *m = module_load(name, name_len);
	if (m == NULL)
		return luaT_error(L);

	new_udata(L, uname_lib, m);
	return 1;
}

/**
 * Unload a module.
 *
 * Take a module object from the caller stack @a L and unload it.
 *
 * Possible errors:
 *
 * - IllegalParams: module is not supplied.
 * - IllegalParams: the module is unloaded.
 *
 * @returns true on success or throwns an error.
 */
static int
lbox_module_unload(struct lua_State *L)
{
	if (lua_gettop(L) != 1) {
		diag_set(IllegalParams, "Expects module:unload()");
		return luaT_error(L);
	}

	struct module *m = clear_udata(L, uname_lib);
	if (m == NULL) {
		diag_set(IllegalParams, "The module is unloaded");
		return luaT_error(L);
	}
	module_unload(m);

	lua_pushboolean(L, true);
	return 1;
}

/** Handle __index request for a module object. */
static int
lbox_module_index(struct lua_State *L)
{
	/*
	 * Process metamethods such as "module:load" first.
	 */
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1))
		return 1;

	struct module *m = get_udata(L, uname_lib);
	if (m == NULL) {
		lua_pushnil(L);
		return 1;
	}

	const char *key = lua_tostring(L, 2);
	if (key == NULL || !lua_isstring(L, 2)) {
		diag_set(IllegalParams,
			 "Bad params, use __index(<key>)");
		return luaT_error(L);
	}

	if (strcmp(key, "path") == 0) {
		lua_pushstring(L, m->package);
		return 1;
	}

	/*
	 * Internal keys for debug only, not API.
	 */
	if (strcmp(key, "debug_refs") == 0) {
		lua_pushnumber(L, m->refs);
		return 1;
	} else if (strcmp(key, "debug_ptr") == 0) {
		char s[64];
		snprintf(s, sizeof(s), "%p", m);
		lua_pushstring(L, s);
		return 1;
	}
	return 0;
}

/** Module representation for REPL (console). */
static int
lbox_module_serialize(struct lua_State *L)
{
	struct module *m = get_udata(L, uname_lib);
	if (m == NULL) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, 0, 1);
	lua_pushstring(L, m->package);
	lua_setfield(L, -2, "path");
	return 1;
}

/** Collect a module. */
static int
lbox_module_gc(struct lua_State *L)
{
	struct module *m = clear_udata(L, uname_lib);
	if (m != NULL)
		module_unload(m);
	return 0;
}

/** Increase reference to a function. */
static void
box_module_func_ref(struct box_module_func *cf)
{
	assert(cf->refs >= 0);
	++cf->refs;
}

/** Free function memory. */
static void
box_module_func_delete(struct box_module_func *cf)
{
	assert(module_func_is_empty(&cf->base));
	TRASH(cf);
	free(cf);
}

/** Unreference a function and free if last one. */
static void
box_module_func_unref(struct box_module_func *cf)
{
	assert(cf->refs > 0);
	if (--cf->refs == 0) {
		module_func_unload(&cf->base);
		cache_del(cf);
		box_module_func_delete(cf);
	}
}

/** Function name from a hash key. */
static char *
box_module_func_name(struct box_module_func *cf)
{
	return &cf->key[cf->len - cf->sym_len];
}

/**
 * Allocate a new function instance and resolve its address.
 *
 * @param m a module the function should be loaded from.
 * @param key function hash key, ie "addr.module.foo".
 * @param len length of @a key.
 * @param sym_len function symbol name length, ie 3 for "foo".
 *
 * @returns function instance on success, NULL otherwise (diag is set).
 */
static struct box_module_func *
box_module_func_new(struct module *m, const char *key, size_t len, size_t sym_len)
{
	size_t size = sizeof(struct box_module_func) + len + 1;
	struct box_module_func *cf = malloc(size);
	if (cf == NULL) {
		diag_set(OutOfMemory, size, "malloc", "cf");
		return NULL;
	}

	cf->len = len;
	cf->sym_len = sym_len;
	cf->refs = 0;

	module_func_create(&cf->base);
	memcpy(cf->key, key, len);
	cf->key[len] = '\0';

	if (module_func_load(m, box_module_func_name(cf), &cf->base) != 0) {
		box_module_func_delete(cf);
		return NULL;
	}

	if (cache_put(cf) != 0) {
		module_func_unload(&cf->base);
		box_module_func_delete(cf);
		return NULL;
	}

	/*
	 * Each new function depends on module presence.
	 * Module will reside even if been unload
	 * explicitly after the function creation.
	 */
	box_module_func_ref(cf);
	return cf;
}

/**
 * Load a function.
 *
 * This function takes a function name from the caller
 * stack @a L and either returns a cached function or
 * creates a new function object.
 *
 * Possible errors:
 *
 * - IllegalParams: function name is either not supplied
 *   or not a string.
 * - SystemError: unable to open a module due to a system error.
 * - ClientError: a module does not exist.
 * - OutOfMemory: unable to allocate a module.
 *
 * @returns module object on success or throws an error.
 */
static int
lbox_module_load_func(struct lua_State *L)
{
	const char *method = "function = module:load";
	const char *fmt_noname = "Expects %s(\'name\') but no name passed";

	if (lua_gettop(L) != 2 || !lua_isstring(L, 2)) {
		diag_set(IllegalParams, fmt_noname, method);
		return luaT_error(L);
	}

	struct module *m = get_udata(L, uname_lib);
	if (m == NULL) {
		const char *fmt =
			"Expects %s(\'name\') but not module object passed";
		diag_set(IllegalParams, fmt, method);
		return luaT_error(L);
	}

	size_t sym_len;
	const char *sym = lua_tolstring(L, 2, &sym_len);

	/*
	 * C standard requires at least 63 significant
	 * initial characters, though it advises to not
	 * impose limits. Lets make the max identifier
	 * big enough to keep longest id, which is hardly
	 * be bigger than 256 symbols.
	 */
	const size_t max_sym_len = 256;

	if (sym_len < 1) {
		diag_set(IllegalParams, fmt_noname, method);
		return luaT_error(L);
	} else if (sym_len > max_sym_len) {
		diag_set(IllegalParams, "Symbol \'%s\' is too long (max %zd)",
			 sym, max_sym_len);
		return luaT_error(L);
	}

	/*
	 * Functions are bound to a module symbols, thus since the hash is
	 * global it should be unique per module.
	 * Make sure there is enough space for key, and formatting.
	 */
	char key[max_sym_len + 32];
	size_t len = (size_t)snprintf(key, sizeof(key), "%p.%s", m, sym);
	assert(len > 1 && len < sizeof(key));

	struct box_module_func *cf = cache_find(key, len);
	if (cf == NULL) {
		cf = box_module_func_new(m, key, len, sym_len);
		if (cf == NULL)
			return luaT_error(L);
	} else {
		box_module_func_ref(cf);
	}

	new_udata(L, uname_func, cf);
	return 1;
}

/**
 * Unload a function.
 *
 * Take a function object from the caller stack @a L and unload it.
 *
 * Possible errors:
 *
 * - IllegalParams: the function is not supplied.
 * - IllegalParams: the function already unloaded.
 *
 * @returns true on success or throwns an error.
 */
static int
lbox_func_unload(struct lua_State *L)
{
	if (lua_gettop(L) != 1) {
		diag_set(IllegalParams, "Expects function:unload()");
		return luaT_error(L);
	}

	struct box_module_func *cf = clear_udata(L, uname_func);
	if (cf == NULL) {
		diag_set(IllegalParams, "The function is unloaded");
		return luaT_error(L);
	}
	box_module_func_unref(cf);

	lua_pushboolean(L, true);
	return 1;
}

/** Handle __index request for a function object. */
static int
lbox_func_index(struct lua_State *L)
{
	/*
	 * Process metamethods such as "func:unload" first.
	 */
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1))
		return 1;

	struct box_module_func *cf = get_udata(L, uname_func);
	if (cf == NULL) {
		lua_pushnil(L);
		return 1;
	}

	const char *key = lua_tostring(L, 2);
	if (key == NULL || !lua_isstring(L, 2)) {
		diag_set(IllegalParams,
			 "Bad params, use __index(<key>)");
		return luaT_error(L);
	}

	if (strcmp(key, "name") == 0) {
		lua_pushstring(L, box_module_func_name(cf));
		return 1;
	}

	/*
	 * Internal keys for debug only, not API.
	 */
	if (strcmp(key, "debug_refs") == 0) {
		lua_pushnumber(L, cf->refs);
		return 1;
	} else if (strcmp(key, "debug_key") == 0) {
		lua_pushstring(L, cf->key);
		return 1;
	} else if (strcmp(key, "debug_module_ptr") == 0) {
		char s[64];
		snprintf(s, sizeof(s), "%p", cf->base.module);
		lua_pushstring(L, s);
		return 1;
	} else if (strcmp(key, "debug_module_refs") == 0) {
		lua_pushnumber(L, cf->base.module->refs);
		return 1;
	}
	return 0;
}

/** Function representation for REPL (console). */
static int
lbox_func_serialize(struct lua_State *L)
{
	struct box_module_func *cf = get_udata(L, uname_func);
	if (cf == NULL) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, 0, 1);
	lua_pushstring(L, box_module_func_name(cf));
	lua_setfield(L, -2, "name");
	return 1;
}

/** Collect a function. */
static int
lbox_func_gc(struct lua_State *L)
{
	struct box_module_func *cf = clear_udata(L, uname_func);
	if (cf != NULL)
		box_module_func_unref(cf);
	return 0;
}


/** Call a function by its name from the Lua code. */
static int
lbox_func_call(struct lua_State *L)
{
	struct box_module_func *cf = get_udata(L, uname_func);
	if (cf == NULL) {
		diag_set(IllegalParams, "The function is unloaded");
		return luaT_error(L);
	}

	lua_State *args_L = luaT_newthread(tarantool_L);
	if (args_L == NULL)
		return luaT_error(L);

	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	lua_xmove(L, args_L, lua_gettop(L) - 1);

	struct port args;
	port_lua_create(&args, args_L);
	((struct port_lua *)&args)->ref = coro_ref;

	struct port ret;

	if (module_func_call(&cf->base, &args, &ret) != 0) {
		port_destroy(&args);
		return luaT_error(L);
	}

	int top = lua_gettop(L);
	port_dump_lua(&ret, L, true);
	int cnt = lua_gettop(L) - top;

	port_destroy(&ret);
	port_destroy(&args);

	return cnt;
}

void
box_lua_lib_init(struct lua_State *L)
{
	func_hash = mh_strnptr_new();
	if (func_hash == NULL)
		panic("box.lib: Can't allocate func hash table");

	static const struct luaL_Reg top_methods[] = {
		{ "load",		lbox_module_load	},
		{ NULL, NULL },
	};
	luaL_register(L, "box.lib", top_methods);
	lua_pop(L, 1);

	static const struct luaL_Reg lbox_module_methods[] = {
		{ "unload",		lbox_module_unload	},
		{ "load",		lbox_module_load_func	},
		{ "__index",		lbox_module_index	},
		{ "__serialize",	lbox_module_serialize	},
		{ "__gc",		lbox_module_gc		},
		{ NULL, NULL },
	};
	luaL_register_type(L, uname_lib, lbox_module_methods);

	static const struct luaL_Reg lbox_func_methods[] = {
		{ "unload",		lbox_func_unload	},
		{ "__index",		lbox_func_index		},
		{ "__serialize",	lbox_func_serialize	},
		{ "__gc",		lbox_func_gc		},
		{ "__call",		lbox_func_call		},
		{ NULL, NULL },
	};
	luaL_register_type(L, uname_func, lbox_func_methods);
}
