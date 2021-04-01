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
#include "assoc.h"
#include "lua/call.h"
#include "diag.h"
#include "port.h"
#include "schema.h"
#include "session.h"

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

/**
 * Schema module (box.schema) instance.
 */
struct schema_module {
	/** Low level module instance. */
	struct module *base;
	/** List of imported functions. */
	struct rlist funcs;
	/** Reference counter. */
	int64_t refs;
};


struct func_c {
	/** Function object base class. */
	struct func base;
	/**
	 * Anchor for module membership.
	 */
	struct rlist item;
	/**
	 * C function to call.
	 */
	struct module_func mf;
	/**
	 * A schema module the function belongs to.
	 */
	struct schema_module *module;
};

static void
schema_module_ref(struct schema_module *module);

static void
schema_module_unref(struct schema_module *module);

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

/** Schema modules hash. */
static struct mh_strnptr_t *modules = NULL;

int
schema_module_init(void)
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
schema_module_free(void)
{
	mh_strnptr_delete(modules);
	modules = NULL;
}

/**
 * Look up a module in the modules cache.
 */
static struct schema_module *
cache_find(const char *name, const char *name_end)
{
	mh_int_t i = mh_strnptr_find_inp(modules, name, name_end - name);
	if (i == mh_end(modules))
		return NULL;
	return mh_strnptr_node(modules, i)->val;
}

/**
 * Save a module to the modules cache.
 */
static int
cache_put(struct schema_module *module)
{
	const char *str = module->base->package;
	size_t len = module->base->package_len;

	const struct mh_strnptr_node_t strnode = {
		.str	= str,
		.len	= len,
		.hash	= mh_strn_hash(str, len),
		.val	= module,
	};

	struct mh_strnptr_node_t prev;
	struct mh_strnptr_node_t *prev_ptr = &prev;

	if (mh_strnptr_put(modules, &strnode, &prev_ptr, NULL) == mh_end(modules)) {
		diag_set(OutOfMemory, sizeof(strnode), "malloc", "modules");
		return -1;
	}

	/*
	 * Just to make sure we haven't replaced something, the
	 * entries must be explicitly deleted.
	 */
	assert(prev_ptr == NULL);
	return 0;
}

/**
 * Update a module in the modules cache.
 */
static void
cache_update(struct schema_module *module)
{
	const char *str = module->base->package;
	size_t len = module->base->package_len;

	mh_int_t i = mh_strnptr_find_inp(modules, str, len);
	if (i == mh_end(modules))
		panic("func: failed to update cache: %s", str);

	mh_strnptr_node(modules, i)->str = str;
	mh_strnptr_node(modules, i)->val = module;
}

/**
 * Delete a module from the module cache.
 */
static void
cache_del(struct schema_module *module)
{
	const char *str = module->base->package;
	size_t len = module->base->package_len;

	mh_int_t i = mh_strnptr_find_inp(modules, str, len);
	if (i != mh_end(modules)) {
		struct schema_module *v;
		v = mh_strnptr_node(modules, i)->val;
		/*
		 * The module may be already reloaded so
		 * the cache carries a new entry instead.
		 */
		if (v == module)
			mh_strnptr_del(modules, i, NULL);
	}
}

/** Delete a module. */
static void
schema_module_delete(struct schema_module *module)
{
	module_unload(module->base);
	TRASH(module);
	free(module);
}

/** Increment reference to a module. */
static void
schema_module_ref(struct schema_module *module)
{
	assert(module->refs >= 0);
	++module->refs;
}

/** Decrement reference to a module and delete it if last one. */
static void
schema_module_unref(struct schema_module *module)
{
	assert(module->refs > 0);
	if (--module->refs == 0) {
		cache_del(module);
		schema_module_delete(module);
	}
}

static struct schema_module *
schema_do_module_load(const char *name, size_t len, bool force)
{
	struct schema_module *module = malloc(sizeof(*module));
	if (module != NULL) {
		module->base = NULL;
		module->refs = 0;
		rlist_create(&module->funcs);
	} else {
		diag_set(OutOfMemory, sizeof(*module),
			 "malloc", "struct schema_module");
		return NULL;
	}

	if (force)
		module->base = module_load_force(name, len);
	else
		module->base = module_load(name, len);

	if (module->base == NULL) {
		free(module);
		return NULL;
	}

	schema_module_ref(module);
	return module;
}

/**
 * Load a new module.
 */
static struct schema_module *
schema_module_load(const char *name, const char *name_end)
{
	return schema_do_module_load(name, name_end - name, false);
}

/**
 * Force load a new module.
 */
static struct schema_module *
schema_module_load_force(const char *name, const char *name_end)
{
	return schema_do_module_load(name, name_end - name, true);
}

static struct func_vtab func_c_vtab;

/** Create a new C function. */
static void
func_c_create(struct func_c *func_c)
{
	func_c->module = NULL;
	func_c->base.vtab = &func_c_vtab;
	rlist_create(&func_c->item);
	module_func_create(&func_c->mf);
}

static int
func_c_load_from(struct func_c *func, struct schema_module *module,
		 const char *func_name)
{
	assert(module_func_is_empty(&func->mf));

	if (module_func_load(module->base, func_name, &func->mf) != 0)
		return -1;

	func->module = module;
	rlist_move(&module->funcs, &func->item);
	schema_module_ref(module);
	return 0;
}

static void
func_c_unload(struct func_c *func)
{
	if (!module_func_is_empty(&func->mf)) {
		rlist_del(&func->item);
		schema_module_unref(func->module);
		module_func_unload(&func->mf);
		func_c_create(func);
	}
}

int
schema_module_reload(const char *package, const char *package_end)
{
	struct schema_module *old = cache_find(package, package_end);
	if (old == NULL) {
		/* Module wasn't loaded - do nothing. */
		diag_set(ClientError, ER_NO_SUCH_MODULE, package);
		return -1;
	}

	struct schema_module *new = schema_module_load_force(package, package_end);
	if (new == NULL)
		return -1;

	/*
	 * Keep an extra reference to the old module
	 * so it won't be freed until reload is complete,
	 * otherwise we might free old module then fail
	 * on some function loading and in result won't
	 * be able to restore old symbols.
	 */
	schema_module_ref(old);
	struct func_c *func, *tmp_func;
	rlist_foreach_entry_safe(func, &old->funcs, item, tmp_func) {
		struct func_name name;
		func_split_name(func->base.def->name, &name);
		func_c_unload(func);
		if (func_c_load_from(func, new, name.sym) != 0) {
			/*
			 * We can restore failing function immediately
			 * and then all previously migrated ones.
			 */
			if (func_c_load_from(func, old, name.sym) != 0)
				goto restore_fail;
			else
				goto restore;
		}
	}
	cache_update(new);
	schema_module_unref(old);
	schema_module_unref(new);
	return 0;
restore:
	/*
	 * Some old functions are not found in the new module,
	 * thus restore all migrated functions back to the original.
	 */
	rlist_foreach_entry_safe(func, &new->funcs, item, tmp_func) {
		struct func_name name;
		func_split_name(func->base.def->name, &name);
		func_c_unload(func);
		if (func_c_load_from(func, old, name.sym) != 0)
			goto restore_fail;
	}
	schema_module_unref(old);
	schema_module_unref(new);
	return -1;

restore_fail:
	/*
	 * Something strange was happen, an early loaden
	 * function was not found in an old dso.
	 */
	panic("Can't restore module function, "
	      "server state is inconsistent");
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
	func_c_create(func);
	return &func->base;
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
	struct func_name name;
	func_split_name(func->base.def->name, &name);

	struct schema_module *cached, *module;
	cached = cache_find(name.package, name.package_end);
	if (cached == NULL) {
		module = schema_module_load(name.package, name.package_end);
		if (module == NULL)
			return -1;
		if (cache_put(module)) {
			schema_module_unref(module);
			return -1;
		}
	} else {
		module = cached;
		schema_module_ref(module);
	}

	int rc = func_c_load_from(func, module, name.sym);
	/*
	 * There is no explicit module loading in this
	 * interface so each function carries a reference
	 * by their own.
	 */
	schema_module_unref(module);
	return rc;
}

int
func_c_call(struct func *base, struct port *args, struct port *ret)
{
	assert(base->vtab == &func_c_vtab);
	assert(base != NULL && base->def->language == FUNC_LANGUAGE_C);
	struct func_c *func = (struct func_c *) base;
	if (module_func_is_empty(&func->mf)) {
		if (func_c_load(func) != 0)
			return -1;
	}
	/*
	 * Note that we don't take a reference to the
	 * module, it is handled by low level instance,
	 * thus while been inside the call the associated
	 * schema_module can be unreferenced and freed.
	 */
	return module_func_call(&func->mf, args, ret);
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
