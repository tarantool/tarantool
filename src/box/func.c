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
#include "fiber.h"
#include "trivia/config.h"
#include "assoc.h"
#include "lua/utils.h"
#include "lua/call.h"
#include "error.h"
#include "errinj.h"
#include "diag.h"
#include "port.h"
#include "schema.h"
#include "session.h"
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

struct func_c {
	/** Function object base class. */
	struct func base;
	/**
	 * Anchor for module membership.
	 */
	struct rlist item;
	/**
	 * For C functions, the body of the function.
	 */
	box_function_f func;
	/**
	 * Each stored function keeps a handle to the
	 * dynamic library for the C callback.
	 */
	struct module *module;
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
	char dir_name[] = "/tmp/tntXXXXXX";
	if (mkdtemp(dir_name) == NULL) {
		diag_set(SystemError, "failed to create unique dir name");
		goto error;
	}
	char load_name[PATH_MAX + 1];
	snprintf(load_name, sizeof(load_name), "%s/%.*s." TARANTOOL_LIBEXT,
		 dir_name, package_len, package);
	if (symlink(path, load_name) < 0) {
		diag_set(SystemError, "failed to create dso link");
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

	struct func_c *func, *tmp_func;
	rlist_foreach_entry_safe(func, &old_module->funcs, item, tmp_func) {
		struct func_name name;
		func_split_name(func->base.def->name, &name);
		func->func = module_sym(new_module, name.sym);
		if (func->func == NULL)
			goto restore;
		func->module = new_module;
		rlist_move(&new_module->funcs, &func->item);
	}
	module_cache_del(package, package_end);
	if (module_cache_put(new_module) != 0)
		goto restore;
	module_gc(old_module);
	*module = new_module;
	return 0;
restore:
	/*
	 * Some old-dso func can't be load from new module, restore old
	 * functions.
	 */
	do {
		struct func_name name;
		func_split_name(func->base.def->name, &name);
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
	} while (func != rlist_first_entry(&old_module->funcs,
					   struct func_c, item));
	assert(rlist_empty(&new_module->funcs));
	module_delete(new_module);
	return -1;
}

static struct func *
func_c_new(struct func_def *def);

/** Construct a SQL builtin function object. */
extern struct func *
func_sql_builtin_new(struct func_def *def);

struct func *
func_new(struct func_def *def)
{
	struct func *func;
	switch (def->language) {
	case FUNC_LANGUAGE_C:
		func = func_c_new(def);
		break;
	case FUNC_LANGUAGE_LUA:
		func = func_lua_new(def);
		break;
	case FUNC_LANGUAGE_SQL_BUILTIN:
		func = func_sql_builtin_new(def);
		break;
	default:
		unreachable();
	}
	if (func == NULL)
		return NULL;
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
	return func;
}

static struct func_vtab func_c_vtab;

static struct func *
func_c_new(MAYBE_UNUSED struct func_def *def)
{
	assert(def->language == FUNC_LANGUAGE_C);
	assert(def->body == NULL && !def->is_sandboxed);
	struct func_c *func = (struct func_c *) malloc(sizeof(struct func_c));
	if (func == NULL) {
		diag_set(OutOfMemory, sizeof(*func), "malloc", "func");
		return NULL;
	}
	func->base.vtab = &func_c_vtab;
	func->func = NULL;
	func->module = NULL;
	return &func->base;
}

static void
func_c_unload(struct func_c *func)
{
	if (func->module) {
		rlist_del(&func->item);
		if (rlist_empty(&func->module->funcs)) {
			struct func_name name;
			func_split_name(func->base.def->name, &name);
			module_cache_del(name.package, name.package_end);
		}
		module_gc(func->module);
	}
	func->module = NULL;
	func->func = NULL;
}

static void
func_c_destroy(struct func *base)
{
	assert(base->vtab == &func_c_vtab);
	assert(base != NULL && base->def->language == FUNC_LANGUAGE_C);
	struct func_c *func = (struct func_c *) base;
	func_c_unload(func);
	TRASH(base);
	free(func);
}

/**
 * Resolve func->func (find the respective DLL and fetch the
 * symbol from it).
 */
static int
func_c_load(struct func_c *func)
{
	assert(func->func == NULL);

	struct func_name name;
	func_split_name(func->base.def->name, &name);

	struct module *module = module_cache_find(name.package,
						  name.package_end);
	if (module == NULL) {
		/* Try to find loaded module in the cache */
		module = module_load(name.package, name.package_end);
		if (module == NULL)
			return -1;
		if (module_cache_put(module)) {
			module_delete(module);
			return -1;
		}
	}

	func->func = module_sym(module, name.sym);
	if (func->func == NULL)
		return -1;
	func->module = module;
	rlist_add(&module->funcs, &func->item);
	return 0;
}

int
func_c_call(struct func *base, struct port *args, struct port *ret)
{
	assert(base->vtab == &func_c_vtab);
	assert(base != NULL && base->def->language == FUNC_LANGUAGE_C);
	struct func_c *func = (struct func_c *) base;
	if (func->func == NULL) {
		if (func_c_load(func) != 0)
			return -1;
	}

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	uint32_t data_sz;
	const char *data = port_get_msgpack(args, &data_sz);
	if (data == NULL)
		return -1;

	port_c_create(ret);
	box_function_ctx_t ctx = { ret };

	/* Module can be changed after function reload. */
	struct module *module = func->module;
	assert(module != NULL);
	++module->calls;
	int rc = func->func(&ctx, data, data + data_sz);
	--module->calls;
	module_gc(module);
	region_truncate(region, region_svp);
	if (rc != 0) {
		if (diag_last_error(&fiber()->diag) == NULL) {
			/* Stored procedure forget to set diag  */
			diag_set(ClientError, ER_PROC_C, "unknown error");
		}
		port_destroy(ret);
		return -1;
	}
	return rc;
}

static struct func_vtab func_c_vtab = {
	.call = func_c_call,
	.destroy = func_c_destroy,
};

void
func_delete(struct func *func)
{
	struct func_def *def = func->def;
	credentials_destroy(&func->owner_credentials);
	func->vtab->destroy(func);
	free(def);
}

/** Check "EXECUTE" permissions for a given function. */
static int
func_access_check(struct func *func)
{
	struct credentials *credentials = effective_user();
	/*
	 * If the user has universal access, don't bother with
	 * checks. No special check for ADMIN user is necessary
	 * since ADMIN has universal access.
	 */
	if ((credentials->universal_access & (PRIV_X | PRIV_U)) ==
	    (PRIV_X | PRIV_U))
		return 0;
	user_access_t access = PRIV_X | PRIV_U;
	/* Check access for all functions. */
	access &= ~entity_access_get(SC_FUNCTION)[credentials->auth_token].effective;
	user_access_t func_access = access & ~credentials->universal_access;
	if ((func_access & PRIV_U) != 0 ||
	    (func->def->uid != credentials->uid &&
	     func_access & ~func->access[credentials->auth_token].effective)) {
		/* Access violation, report error. */
		struct user *user = user_find(credentials->uid);
		if (user != NULL) {
			diag_set(AccessDeniedError, priv_name(PRIV_X),
				 schema_object_name(SC_FUNCTION),
				 func->def->name, user->def->name);
		}
		return -1;
	}
	return 0;
}

int
func_call(struct func *base, struct port *args, struct port *ret)
{
	if (func_access_check(base) != 0)
		return -1;
	/**
	 * Change the current user id if the function is
	 * a set-definer-uid one. If the function is not
	 * defined, it's obviously not a setuid one.
	 */
	struct credentials *orig_credentials = NULL;
	if (base->def->setuid) {
		orig_credentials = effective_user();
		/* Remember and change the current user id. */
		if (credentials_is_empty(&base->owner_credentials)) {
			/*
			 * Fill the cache upon first access, since
			 * when func is created, no user may
			 * be around to fill it (recovery of
			 * system spaces from a snapshot).
			 */
			struct user *owner = user_find(base->def->uid);
			if (owner == NULL)
				return -1;
			credentials_reset(&base->owner_credentials, owner);
		}
		fiber_set_user(fiber(), &base->owner_credentials);
	}
	int rc = base->vtab->call(base, args, ret);
	/* Restore the original user */
	if (orig_credentials)
		fiber_set_user(fiber(), orig_credentials);
	return rc;
}
