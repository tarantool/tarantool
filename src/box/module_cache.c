/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <lua.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "assoc.h"
#include "diag.h"
#include "fiber.h"
#include "errinj.h"
#include "module_cache.h"

#include "box/error.h"
#include "box/port.h"

#include "lua/utils.h"
#include "libeio/eio.h"

static struct mh_strnptr_t *module_cache = NULL;

/**
 * Helpers for cache manipulations.
 */
static struct module *
cache_find(const char *str, size_t len)
{
	mh_int_t e = mh_strnptr_find_inp(module_cache, str, len);
	if (e == mh_end(module_cache))
		return NULL;
	return mh_strnptr_node(module_cache, e)->val;
}

static void
cache_update(struct module *m)
{
	const char *str = m->package;
	size_t len = m->package_len;

	mh_int_t e = mh_strnptr_find_inp(module_cache, str, len);
	if (e == mh_end(module_cache))
		panic("module: failed to update cache: %s", str);

	mh_strnptr_node(module_cache, e)->str = m->package;
	mh_strnptr_node(module_cache, e)->val = m;
}

static int
cache_put(struct module *m)
{
	const struct mh_strnptr_node_t nd = {
		.str	= m->package,
		.len	= m->package_len,
		.hash	= mh_strn_hash(m->package, m->package_len),
		.val	= m,
	};

	struct mh_strnptr_node_t prev;
	struct mh_strnptr_node_t *prev_ptr = &prev;

	mh_int_t e = mh_strnptr_put(module_cache, &nd, &prev_ptr, NULL);
	if (e == mh_end(module_cache)) {
		diag_set(OutOfMemory, sizeof(nd), "malloc",
			 "module_cache node");
		return -1;
	}

	/*
	 * Just to make sure we haven't replaced something, the
	 * entries must be explicitly deleted.
	 */
	assert(prev_ptr == NULL);
	return 0;
}

static void
cache_del(struct module *m)
{
	const char *str = m->package;
	size_t len = m->package_len;

	mh_int_t e = mh_strnptr_find_inp(module_cache, str, len);
	if (e != mh_end(module_cache)) {
		struct module *v = mh_strnptr_node(module_cache, e)->val;
		if (v == m) {
			/*
			 * The module in cache might be updated
			 * via force load and old instance is kept
			 * by a reference only.
			 */
			mh_strnptr_del(module_cache, e, NULL);
		}
	}
}

/** Arguments for lpackage_search. */
struct find_ctx {
	const char *package;
	size_t package_len;
	char *path;
	size_t path_len;
};

/** A helper for find_package(). */
static int
lpackage_search(lua_State *L)
{
	struct find_ctx *ctx = (void *)lua_topointer(L, 1);

	lua_getglobal(L, "package");
	lua_getfield(L, -1, "search");
	lua_pushlstring(L, ctx->package, ctx->package_len);

	lua_call(L, 1, 1);
	if (lua_isnil(L, -1))
		return luaL_error(L, "module not found");

	char resolved[PATH_MAX];
	if (realpath(lua_tostring(L, -1), resolved) == NULL) {
		diag_set(SystemError, "realpath");
		return luaT_error(L);
	}

	/*
	 * No need for result being trimmed test, it
	 * is guaranteed by realpath call.
	 */
	snprintf(ctx->path, ctx->path_len, "%s", resolved);
	return 0;
}

/** Find package in Lua's "package.search". */
static int
find_package(const char *package, size_t package_len,
	     char *path, size_t path_len)
{
	struct find_ctx ctx = {
		.package	= package,
		.package_len	= package_len,
		.path		= path,
		.path_len	= path_len,
	};

	struct lua_State *L = tarantool_L;
	int top = lua_gettop(L);
	if (luaT_cpcall(L, lpackage_search, &ctx) != 0) {
		diag_set(ClientError, ER_LOAD_MODULE, ctx.package_len,
			 ctx.package, lua_tostring(L, -1));
		lua_settop(L, top);
		return -1;
	}
	assert(top == lua_gettop(L));
	return 0;
}

void
module_ref(struct module *m)
{
	assert(m->refs >= 0);
	++m->refs;
}

void
module_unref(struct module *m)
{
	assert(m->refs > 0);
	if (--m->refs == 0) {
		struct errinj *e = errinj(ERRINJ_DYN_MODULE_COUNT, ERRINJ_INT);
		if (e != NULL)
			--e->iparam;
		cache_del(m);
		dlclose(m->handle);
		TRASH(m);
		free(m);
	}
}

int
module_func_load(struct module *m, const char *func_name,
		 struct module_func *mf)
{
	void *sym = dlsym(m->handle, func_name);
	if (sym == NULL) {
		diag_set(ClientError, ER_LOAD_FUNCTION,
			 func_name, dlerror());
		return -1;
	}

	mf->func = sym;
	mf->module = m;
	module_ref(m);

	return 0;
}

void
module_func_unload(struct module_func *mf)
{
	module_unref(mf->module);
	/*
	 * Strictly speaking there is no need
	 * for implicit creation, it is up to
	 * the caller to clear the module function,
	 * but since it is cheap, lets prevent from
	 * even potential use after free.
	 */
	module_func_create(mf);
}

int
module_func_call(struct module_func *mf, struct port *args,
		 struct port *ret)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	uint32_t data_sz;
	const char *data = port_get_msgpack(args, &data_sz);
	if (data == NULL)
		return -1;

	port_c_create(ret);
	box_function_ctx_t ctx = {
		.port = ret,
	};

	/*
	 * We don't know what exactly the callee
	 * gonna do during the execution, it may
	 * even try to unload itself, thus we make
	 * sure the dso won't be unloaded until
	 * execution is complete.
	 *
	 * Moreover the callee might release the memory
	 * associated with the module_func pointer itself
	 * so keep the address of the module locally.
	 */
	struct module *m = mf->module;
	module_ref(m);
	int rc = mf->func(&ctx, data, data + data_sz);
	module_unref(m);

	region_truncate(region, region_svp);

	if (rc != 0) {
		if (diag_last_error(&fiber()->diag) == NULL)
			diag_set(ClientError, ER_PROC_C, "unknown error");
		port_destroy(ret);
		return -1;
	}

	return 0;
}

/** Fill attributes from stat. */
static void
module_attr_fill(struct module_attr *attr, struct stat *st)
{
	memset(attr, 0, sizeof(*attr));

	attr->st_dev	= (uint64_t)st->st_dev;
	attr->st_ino	= (uint64_t)st->st_ino;
	attr->st_size	= (uint64_t)st->st_size;
#ifdef TARGET_OS_DARWIN
	attr->tv_sec	= (uint64_t)st->st_mtimespec.tv_sec;
	attr->tv_nsec	= (uint64_t)st->st_mtimespec.tv_nsec;
#else
	attr->tv_sec	= (uint64_t)st->st_mtim.tv_sec;
	attr->tv_nsec	= (uint64_t)st->st_mtim.tv_nsec;
#endif
}

/**
 * Copy shared library to temp directory and load from there,
 * then remove it from this temp place leaving in memory. This
 * is because there was a bug in libc which screw file updates
 * detection properly such that next dlopen call simply return
 * a cached version instead of rereading a library from the disk.
 *
 * We keep own copy of file attributes and reload the library
 * on demand.
 */
static struct module *
module_new(const char *package, size_t package_len,
	   const char *source_path)
{
	size_t size = sizeof(struct module) + package_len + 1;
	struct module *m = malloc(size);
	if (m == NULL) {
		diag_set(OutOfMemory, size, "malloc", "module");
		return NULL;
	}

	m->package_len = package_len;
	m->refs = 0;

	memcpy(m->package, package, package_len);
	m->package[package_len] = 0;

	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";

	char dir_name[PATH_MAX];
	int rc = snprintf(dir_name, sizeof(dir_name),
			  "%s/tntXXXXXX", tmpdir);
	if (rc < 0 || (size_t)rc >= sizeof(dir_name)) {
		diag_set(SystemError, "failed to generate path to tmp dir");
		goto error;
	}

	if (mkdtemp(dir_name) == NULL) {
		diag_set(SystemError, "failed to create unique dir name: %s",
			 dir_name);
		goto error;
	}

	char load_name[PATH_MAX];
	rc = snprintf(load_name, sizeof(load_name),
		      "%s/%.*s." TARANTOOL_LIBEXT,
		      dir_name, (int)package_len, package);
	if (rc < 0 || (size_t)rc >= sizeof(dir_name)) {
		diag_set(SystemError, "failed to generate path to dso");
		goto error;
	}

	struct stat st;
	if (stat(source_path, &st) < 0) {
		diag_set(SystemError, "failed to stat() module: %s",
			 source_path);
		goto error;
	}
	module_attr_fill(&m->attr, &st);

	int source_fd = open(source_path, O_RDONLY);
	if (source_fd < 0) {
		diag_set(SystemError, "failed to open module %s "
			 "file for reading", source_path);
		goto error;
	}
	int dest_fd = open(load_name, O_WRONLY | O_CREAT | O_TRUNC,
			   st.st_mode & 0777);
	if (dest_fd < 0) {
		diag_set(SystemError, "failed to open file %s "
			 "for writing ", load_name);
		close(source_fd);
		goto error;
	}

	off_t ret = eio_sendfile_sync(dest_fd, source_fd, 0, st.st_size);
	close(source_fd);
	close(dest_fd);
	if (ret != st.st_size) {
		diag_set(SystemError, "failed to copy dso %s to %s",
			 source_path, load_name);
		goto error;
	}

	m->handle = dlopen(load_name, RTLD_NOW | RTLD_LOCAL);
	if (unlink(load_name) != 0)
		say_warn("failed to unlink dso link: %s", load_name);
	if (rmdir(dir_name) != 0)
		say_warn("failed to delete temporary dir: %s", dir_name);
	if (m->handle == NULL) {
		diag_set(ClientError, ER_LOAD_MODULE, package_len,
			  package, dlerror());
		goto error;
	}

	struct errinj *e = errinj(ERRINJ_DYN_MODULE_COUNT, ERRINJ_INT);
	if (e != NULL)
		++e->iparam;

	module_ref(m);
	return m;

error:
	free(m);
	return NULL;
}

struct module *
module_load_force(const char *package, size_t package_len)
{
	char path[PATH_MAX];
	size_t size = sizeof(path);

	if (find_package(package, package_len, path, size) != 0)
		return NULL;

	struct module *m = module_new(package, package_len, path);
	if (m == NULL)
		return NULL;

	struct module *c = cache_find(package, package_len);
	if (c != NULL) {
		cache_update(m);
	} else {
		if (cache_put(m) != 0) {
			module_unload(m);
			return NULL;
		}
	}

	return m;
}

struct module *
module_load(const char *package, size_t package_len)
{
	char path[PATH_MAX];

	if (find_package(package, package_len, path, sizeof(path)) != 0)
		return NULL;

	struct module *m = cache_find(package, package_len);
	if (m != NULL) {
		struct module_attr attr;
		struct stat st;
		if (stat(path, &st) != 0) {
			diag_set(SystemError, "failed to stat() %s", path);
			return NULL;
		}

		/*
		 * In case of cache hit we may reuse existing
		 * module which speedup load procedure.
		 */
		module_attr_fill(&attr, &st);
		if (memcmp(&attr, &m->attr, sizeof(attr)) == 0) {
			module_ref(m);
			return m;
		}

		/*
		 * Module has been updated on a storage device,
		 * so load a new instance and update the cache,
		 * old entry get evicted but continue residing
		 * in memory, fully functional, until last
		 * function is unloaded.
		 */
		m = module_new(package, package_len, path);
		if (m != NULL)
			cache_update(m);
	} else {
		m = module_new(package, package_len, path);
		if (m != NULL && cache_put(m) != 0) {
			module_unload(m);
			return NULL;
		}
	}

	return m;
}

void
module_unload(struct module *m)
{
	module_unref(m);
}

void
module_free(void)
{
	mh_strnptr_delete(module_cache);
	module_cache = NULL;
}

int
module_init(void)
{
	module_cache = mh_strnptr_new();
	if (module_cache == NULL) {
		diag_set(OutOfMemory, sizeof(*module_cache),
			 "malloc", "module_cache");
		return -1;
	}
	return 0;
}
