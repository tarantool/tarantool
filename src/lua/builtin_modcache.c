/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "builtin_modcache.h"
#include "assoc.h"

static struct mh_strnptr_t *builtin_modules = NULL;

void
builtin_modcache_init(void)
{
	if (builtin_modules == NULL)
		builtin_modules = mh_strnptr_new();
}

void
builtin_modcache_free(void)
{
	if (builtin_modules == NULL)
		return;
	mh_strnptr_delete(builtin_modules);
	builtin_modules = NULL;
}

void
builtin_modcache_put(const char *modname, const char *code)
{
	assert(code != NULL && *code);
	assert(modname != NULL);
	uint32_t len = strlen(modname);
	assert(len > 0);

	uint32_t hash = mh_strn_hash(modname, len);
	const struct mh_strnptr_node_t strnode = {
		modname, len, hash, (void *)code
	};
	mh_strnptr_put(builtin_modules, &strnode, NULL, NULL);
}

const char*
builtin_modcache_find(const char *modname)
{
	uint32_t len = strlen(modname);
	mh_int_t k = mh_strnptr_find_str(builtin_modules, modname, len);
	if (k == mh_end(builtin_modules))
		return NULL;
	return (const char *)mh_strnptr_node(builtin_modules, k)->val;
}
