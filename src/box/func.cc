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

struct func *
func_new(struct func_def *def)
{
	struct func *func = (struct func *) malloc(sizeof(struct func));
	if (func == NULL)
		return NULL;
	func->def = *def;
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

void
func_load(struct func *func)
{
	func_unload(func);

	struct lua_State *L = tarantool_L;
	int n = lua_gettop(L);

	auto l_guard = make_scoped_guard([=]{
		lua_settop(L, n);
	});
	/*
	 * Call package.searchpath(name, package.cpath) and use
	 * the path to the function in dlopen().
	 */
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "searchpath");

	/*
	 * Extract package name from function name.
	 * E.g. name = foo.bar.baz, function = baz, package = foo.bar
	 */
	const char *sym;
	const char *package = func->def.name;
	const char *package_end = strrchr(package, '.');
	if (package_end != NULL) {
		/* module.submodule.function => module.submodule, function */
		sym = package_end + 1;
	} else {
		/* package == function => function, function */
		sym = package;
		package_end = package + strlen(package);
	}

	/* First argument of searchpath: name */
	lua_pushlstring(L, package, package_end - package);
	/* Fetch  cpath from 'package' as the second argument */
	lua_getfield(L, -3, "cpath");

	if (lua_pcall(L, 2, 1, 0)) {
		tnt_raise(ClientError, ER_LOAD_FUNCTION, func->def.name,
			  lua_tostring(L, -1));
	}
	if (lua_isnil(L, -1)) {
		tnt_raise(ClientError, ER_LOAD_FUNCTION, func->def.name,
			  "shared library not found in the search path");
	}
	func->dlhandle = dlopen(lua_tostring(L, -1), RTLD_NOW | RTLD_LOCAL);
	if (func->dlhandle == NULL) {
		tnt_raise(LoggedError, ER_LOAD_FUNCTION, func->def.name,
			  dlerror());
	}
	func->func = (box_function_f) dlsym(func->dlhandle, sym);
	if (func->func == NULL) {
		tnt_raise(LoggedError, ER_LOAD_FUNCTION, func->def.name,
			  dlerror());
	}
}

void
func_update(struct func *func, struct func_def *def)
{
	func_unload(func);
	func->def = *def;
}

void
func_delete(struct func *func)
{
	func_unload(func);
	free(func);
}
