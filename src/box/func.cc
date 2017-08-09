/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "func.h"

#include <dlfcn.h>

#include "lua/utils.h"
#include "scoped_guard.h"

/**
 * Parsed symbol and package names.
 */
struct func_name {
	/** Null-terminated symbol name, e.g. "func" for "mod.submod.func" */
	const char *sym;
	/** Package name, e.g. "mod.submod" for "mod.submod.func" */
	const char *package;
	/** A pointer to the last character in ->package + 1 */
	const char *package_end;
};

/***
 * Split function name to symbol and package names.
 * For example, str = foo.bar.baz => sym = baz, package = foo.bar
 * @param str function name, e.g. "module.submodule.function".
 * @param[out] name parsed symbol and package names.
 */
static void
func_split_name(const char *str, struct func_name *name)
{
	name->package = str;
	name->package_end = strrchr(str, '.');
	if (name->package_end != NULL) {
		/* module.submodule.function => module.submodule, function */
		name->sym = name->package_end + 1; /* skip '.' */
	} else {
		/* package == function => function, function */
		name->sym = name->package;
		name->package_end = str + strlen(str);
	}
}

/**
 * Arguments for luaT_module_find used by lua_cpcall()
 */
struct module_find_ctx {
	const char *package;
	const char *package_end;
	char *path;
	size_t path_len;
};

/**
 * A cpcall() helper for module_find()
 */
static int
luaT_module_find(lua_State *L)
{
	struct module_find_ctx *ctx = (struct module_find_ctx *)
		lua_topointer(L, 1);

	/*
	 * Call package.searchpath(name, package.cpath) and use
	 * the path to the function in dlopen().
	 */
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "searchpath");

	/* First argument of searchpath: name */
	lua_pushlstring(L, ctx->package, ctx->package_end - ctx->package);
	/* Fetch  cpath from 'package' as the second argument */
	lua_getfield(L, -3, "cpath");

	lua_call(L, 2, 1);
	if (lua_isnil(L, -1))
		return luaL_error(L, "module not found in package.cpath");

	snprintf(ctx->path, ctx->path_len, "%s", lua_tostring(L, -1));
	return 0;
}

/**
 * Find path to module using Lua's package.cpath
 * @param package package name
 * @param package_end a pointer to the last byte in @a package + 1
 * @param[out] path path to shared library
 * @param path_len size of @a path buffer
 * @retval 0 on success
 * @retval -1 on error, diag is set
 */
static int
module_find(const char *package, const char *package_end, char *path,
	    size_t path_len)
{
	struct module_find_ctx ctx = { package, package_end, path, path_len };
	lua_State *L = tarantool_L;
	int top = lua_gettop(L);
	if (luaT_cpcall(L, luaT_module_find, &ctx) != 0) {
		int package_len = (int) (package_end - package);
		diag_set(ClientError, ER_LOAD_MODULE, package_len, package,
			 lua_tostring(L, -1));
		lua_settop(L, top);
		return -1;
	}
	assert(top == lua_gettop(L)); /* cpcall discard results */
	return 0;
}

struct func *
func_new(struct func_def *def)
{
	struct func *func = (struct func *) malloc(sizeof(struct func));
	if (func == NULL) {
		diag_set(OutOfMemory, sizeof(*func), "malloc", "func");
		return NULL;
	}
	func->def = def;
	/** Nobody has access to the function but the owner. */
	memset(func->access, 0, sizeof(func->access));
	/*
	 * Do not initialize the privilege cache right away since
	 * when loading up a function definition during recovery,
	 * user cache may not be filled up yet (space _user is
	 * recovered after space _func), so no user cache entry
	 * may exist yet for such user.  The cache will be filled
	 * up on demand upon first access.
	 *
	 * Later on consistency of the cache is ensured by DDL
	 * checks (see user_has_data()).
	 */
	func->owner_credentials.auth_token = BOX_USER_MAX; /* invalid value */
	func->func = NULL;
	func->dlhandle = NULL;
	return func;
}

static void
func_unload(struct func *func)
{
	if (func->dlhandle)
		dlclose(func->dlhandle);
	func->dlhandle = NULL;
	func->func = NULL;
}

/**
 * Resolve func->func (find the respective DLL and fetch the
 * symbol from it).
 */
static int
func_load(struct func *func)
{
	assert(func->func == NULL);

	struct func_name name;
	func_split_name(func->def->name, &name);

	char path[PATH_MAX];
	if (module_find(name.package, name.package_end, path, sizeof(path)))
		return -1;

	func->dlhandle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (func->dlhandle == NULL) {
		int package_len = (int) (name.package_end - name.package_end);
		diag_set(ClientError, ER_LOAD_MODULE, package_len,
			  name.package, dlerror());
		diag_log();
		return -1;
	}
	func->func = (box_function_f) dlsym(func->dlhandle, name.sym);
	if (func->func == NULL) {
		diag_set(ClientError, ER_LOAD_FUNCTION, func->def->name,
			  dlerror());
		diag_log();
		return -1;
	}
	return 0;
}

int
func_call(struct func *func, box_function_ctx_t *ctx, const char *args,
	  const char *args_end)
{
	if (func->func == NULL) {
		if (func_load(func) != 0)
			return -1;
	}

	return func->func(ctx, args, args_end);
}

void
func_update(struct func *func, struct func_def *def)
{
	func_unload(func);
	free(func->def);
	func->def = def;
}

void
func_delete(struct func *func)
{
	func_unload(func);
	free(func->def);
	free(func);
}
