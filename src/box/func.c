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
#include "trivia/config.h"
#include "assoc.h"
#include "lua/utils.h"
#include "error.h"
#include "errinj.h"
#include "diag.h"
#include "user.h"
#include "libeio/eio.h"
#include <fcntl.h>
#include <dlfcn.h>

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

	lua_getfield(L, -1, "search");

	/* Argument of search: name */
	lua_pushlstring(L, ctx->package, ctx->package_end - ctx->package);

	lua_call(L, 1, 1);
	if (lua_isnil(L, -1))
		return luaL_error(L, "module not found");
	/* Convert path to absolute */
	char resolved[PATH_MAX];
	if (realpath(lua_tostring(L, -1), resolved) == NULL) {
		diag_set(SystemError, "realpath");
		return luaT_error(L);
	}

	snprintf(ctx->path, ctx->path_len, "%s", resolved);
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

static struct mh_strnptr_t *modules = NULL;

static void
module_gc(struct module *module);

int
module_init(void)
{
	modules = mh_strnptr_new();
	if (modules == NULL) {
		diag_set(OutOfMemory, sizeof(*modules), "malloc",
			  "modules hash table");
		return -1;
	}
	return 0;
}

void
module_free(void)
{
	while (mh_size(modules) > 0) {
		mh_int_t i = mh_first(modules);
		struct module *module =
			(struct module *) mh_strnptr_node(modules, i)->val;
		/* Can't delete modules if they have active calls */
		module_gc(module);
	}
	mh_strnptr_delete(modules);
}

/**
 * Look up a module in the modules cache.
 */
static struct module *
module_cache_find(const char *name, const char *name_end)
{
	mh_int_t i = mh_strnptr_find_inp(modules, name, name_end - name);
	if (i == mh_end(modules))
		return NULL;
	return (struct module *)mh_strnptr_node(modules, i)->val;
}

/**
 * Save module to the module cache.
 */
static inline int
module_cache_put(struct module *module)
{
	size_t package_len = strlen(module->package);
	uint32_t name_hash = mh_strn_hash(module->package, package_len);
	const struct mh_strnptr_node_t strnode = {
		module->package, package_len, name_hash, module};

	if (mh_strnptr_put(modules, &strnode, NULL, NULL) == mh_end(modules)) {
		diag_set(OutOfMemory, sizeof(strnode), "malloc", "modules");
		return -1;
	}
	return 0;
}

/**
 * Delete a module from the module cache
 */
static void
module_cache_del(const char *name, const char *name_end)
{
	mh_int_t i = mh_strnptr_find_inp(modules, name, name_end - name);
	if (i == mh_end(modules))
		return;
	mh_strnptr_del(modules, i, NULL);
}

/*
 * Load a dso.
 * Create a new symlink based on temporary directory and try to
 * load via this symink to load a dso twice for cases of a function
 * reload.
 */
static struct module *
module_load(const char *package, const char *package_end)
{
	char path[PATH_MAX];
	if (module_find(package, package_end, path, sizeof(path)) != 0)
		return NULL;

	int package_len = package_end - package;
	struct module *module = (struct module *)
		malloc(sizeof(*module) + package_len + 1);
	if (module == NULL) {
		diag_set(OutOfMemory, sizeof(struct module) + package_len + 1,
			 "malloc", "struct module");
		return NULL;
	}
	memcpy(module->package, package, package_len);
	module->package[package_len] = 0;
	rlist_create(&module->funcs);
	module->calls = 0;

	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";
	char dir_name[PATH_MAX];
	int rc = snprintf(dir_name, sizeof(dir_name), "%s/tntXXXXXX", tmpdir);
	if (rc < 0 || (size_t) rc >= sizeof(dir_name)) {
		diag_set(SystemError, "failed to generate path to tmp dir");
		goto error;
	}

	if (mkdtemp(dir_name) == NULL) {
		diag_set(SystemError, "failed to create unique dir name: %s",
			 dir_name);
		goto error;
	}
	char load_name[PATH_MAX];
	rc = snprintf(load_name, sizeof(load_name), "%s/%.*s." TARANTOOL_LIBEXT,
		      dir_name, package_len, package);
	if (rc < 0 || (size_t) rc >= sizeof(dir_name)) {
		diag_set(SystemError, "failed to generate path to DSO");
		goto error;
	}

	struct stat st;
	if (stat(path, &st) < 0) {
		diag_set(SystemError, "failed to stat() module %s", path);
		goto error;
	}

	int source_fd = open(path, O_RDONLY);
	if (source_fd < 0) {
		diag_set(SystemError, "failed to open module %s file for" \
			 " reading", path);
		goto error;
	}
	int dest_fd = open(load_name, O_WRONLY|O_CREAT|O_TRUNC,
			   st.st_mode & 0777);
	if (dest_fd < 0) {
		diag_set(SystemError, "failed to open file %s for writing ",
			 load_name);
		close(source_fd);
		goto error;
	}

	off_t ret = eio_sendfile_sync(dest_fd, source_fd, 0, st.st_size);
	close(source_fd);
	close(dest_fd);
	if (ret != st.st_size) {
		diag_set(SystemError, "failed to copy DSO %s to %s",
			 path, load_name);
		goto error;
	}

	module->handle = dlopen(load_name, RTLD_NOW | RTLD_LOCAL);
	if (unlink(load_name) != 0)
		say_warn("failed to unlink dso link %s", load_name);
	if (rmdir(dir_name) != 0)
		say_warn("failed to delete temporary dir %s", dir_name);
	if (module->handle == NULL) {
		diag_set(ClientError, ER_LOAD_MODULE, package_len,
			  package, dlerror());
		goto error;
	}
	struct errinj *e = errinj(ERRINJ_DYN_MODULE_COUNT, ERRINJ_INT);
	if (e != NULL)
		++e->iparam;
	return module;
error:
	free(module);
	return NULL;
}

static void
module_delete(struct module *module)
{
	struct errinj *e = errinj(ERRINJ_DYN_MODULE_COUNT, ERRINJ_INT);
	if (e != NULL)
		--e->iparam;
	dlclose(module->handle);
	TRASH(module);
	free(module);
}

/*
 * Check if a dso is unused and can be closed.
 */
static void
module_gc(struct module *module)
{
	if (rlist_empty(&module->funcs) && module->calls == 0)
		module_delete(module);
}

/*
 * Import a function from the module.
 */
static box_function_f
module_sym(struct module *module, const char *name)
{
	box_function_f f = (box_function_f)dlsym(module->handle, name);
	if (f == NULL) {
		diag_set(ClientError, ER_LOAD_FUNCTION, name, dlerror());
		return NULL;
	}
	return f;
}

/*
 * Reload a dso.
 */
int
module_reload(const char *package, const char *package_end, struct module **module)
{
	struct module *old_module = module_cache_find(package, package_end);
	if (old_module == NULL) {
		/* Module wasn't loaded - do nothing. */
		*module = NULL;
		return 0;
	}

	struct module *new_module = module_load(package, package_end);
	if (new_module == NULL)
		return -1;

	struct func *func, *tmp_func;
	rlist_foreach_entry_safe(func, &old_module->funcs, item, tmp_func) {
		/* Move immediately for restore sake. */
		rlist_move(&new_module->funcs, &func->item);
		struct func_name name;
		func_split_name(func->def->name, &name);
		func->func = module_sym(new_module, name.sym);
		if (func->func == NULL)
			goto restore;
		func->module = new_module;
	}
	module_cache_del(package, package_end);
	if (module_cache_put(new_module) != 0)
		goto restore;
	module_gc(old_module);
	*module = new_module;
	return 0;
restore:
	/*
	 * Some old functions are not found in the new module,
	 * thus restore all migrated functions back to the original.
	 */
	rlist_foreach_entry_safe(func, &new_module->funcs, item, tmp_func) {
		struct func_name name;
		func_split_name(func->def->name, &name);
		func->func = module_sym(old_module, name.sym);
		if (func->func == NULL) {
			/*
			 * Something strange was happen, an early loaden
			 * function was not found in an old dso.
			 */
			panic("Can't restore module function, "
			      "server state is inconsistent");
		}
		func->module = old_module;
		rlist_move(&old_module->funcs, &func->item);
	}
	module_delete(new_module);
	return -1;
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
	credentials_create_empty(&func->owner_credentials);
	func->func = NULL;
	func->module = NULL;
	return func;
}

static void
func_unload(struct func *func)
{
	if (func->module) {
		rlist_del(&func->item);
		if (rlist_empty(&func->module->funcs)) {
			struct func_name name;
			func_split_name(func->def->name, &name);
			module_cache_del(name.package, name.package_end);
		}
		module_gc(func->module);
	}
	func->module = NULL;
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

	struct module *cached, *module;
	cached = module_cache_find(name.package, name.package_end);
	if (cached == NULL) {
		module = module_load(name.package, name.package_end);
		if (module == NULL)
			return -1;
		if (module_cache_put(module)) {
			module_delete(module);
			return -1;
		}
	} else {
		module = cached;
	}

	func->func = module_sym(module, name.sym);
	if (func->func == NULL) {
		if (cached == NULL) {
			/*
			 * In case if it was a first load we should
			 * clean the cache immediately otherwise
			 * the module continue being referenced even
			 * if there will be no use of it.
			 *
			 * Note the module_sym set an error thus be
			 * careful to not wipe it.
			 */
			module_cache_del(name.package, name.package_end);
			module_delete(module);
		}
		return -1;
	}
	func->module = module;
	rlist_add(&module->funcs, &func->item);
	return 0;
}

int
func_reload(struct func *func)
{
	struct func_name name;
	func_split_name(func->def->name, &name);
	struct module *module = NULL;
	if (module_reload(name.package, name.package_end, &module) != 0) {
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

	/* Module can be changed after function reload. */
	struct module *module = func->module;
	assert(module != NULL);
	++module->calls;
	int rc = func->func(ctx, args, args_end);
	--module->calls;
	module_gc(module);
	return rc;
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
	credentials_destroy(&func->owner_credentials);
	free(func->def);
	TRASH(func);
	free(func);
}
