/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "func_cache.h"

#include <assert.h>
#include "assoc.h"

/** ID -> func dictionary. */
static struct mh_i32ptr_t *funcs;
/** Name -> func dictionary. */
static struct mh_strnptr_t *funcs_by_name;

const char *func_cache_holder_type_strs[FUNC_HOLDER_MAX] = {
	[FUNC_HOLDER_CONSTRAINT] = "constraint",
	[FUNC_HOLDER_SPACE_UPGRADE] = "space upgrade",
	[FUNC_HOLDER_FIELD_DEFAULT] = "field default value",
};

void
func_cache_init(void)
{
	funcs = mh_i32ptr_new();
	funcs_by_name = mh_strnptr_new();
}

void
func_cache_destroy(void)
{
	while (mh_size(funcs) > 0) {
		mh_int_t i = mh_first(funcs);

		struct func *func = ((struct func *)
			mh_i32ptr_node(funcs, i)->val);
		func_cache_delete(func->def->fid);
		func_delete(func);
	}
	mh_i32ptr_delete(funcs);
	mh_strnptr_delete(funcs_by_name);
}

void
func_cache_insert(struct func *func)
{
	assert(func_by_id(func->def->fid) == NULL);
	assert(func_by_name(func->def->name, strlen(func->def->name)) == NULL);
	const struct mh_i32ptr_node_t node = { func->def->fid, func };
	mh_i32ptr_put(funcs, &node, NULL, NULL);
	size_t def_name_len = strlen(func->def->name);
	uint32_t name_hash = mh_strn_hash(func->def->name, def_name_len);
	const struct mh_strnptr_node_t strnode = {
		func->def->name, def_name_len, name_hash, func };
	mh_strnptr_put(funcs_by_name, &strnode, NULL, NULL);
}

void
func_cache_delete(uint32_t fid)
{
	mh_int_t k = mh_i32ptr_find(funcs, fid, NULL);
	if (k == mh_end(funcs))
		return;
	struct func *func = (struct func *)mh_i32ptr_node(funcs, k)->val;
	assert(rlist_empty(&func->func_cache_pin_list));
	mh_i32ptr_del(funcs, k, NULL);
	k = mh_strnptr_find_str(funcs_by_name, func->def->name,
				strlen(func->def->name));
	if (k != mh_end(funcs))
		mh_strnptr_del(funcs_by_name, k, NULL);
}

struct func *
func_by_id(uint32_t fid)
{
	mh_int_t func = mh_i32ptr_find(funcs, fid, NULL);
	if (func == mh_end(funcs))
		return NULL;
	return (struct func *)mh_i32ptr_node(funcs, func)->val;
}

struct func *
func_by_name(const char *name, uint32_t name_len)
{
	mh_int_t func = mh_strnptr_find_str(funcs_by_name, name, name_len);
	if (func == mh_end(funcs_by_name))
		return NULL;
	return (struct func *)mh_strnptr_node(funcs_by_name, func)->val;
}

void
func_pin(struct func *func, struct func_cache_holder *holder,
	 enum func_holder_type type)
{
	assert(func_by_id(func->def->fid) != NULL);
	holder->func = func;
	holder->type = type;
	rlist_add_tail(&func->func_cache_pin_list, &holder->link);
}

void
func_unpin(struct func_cache_holder *holder)
{
	assert(func_by_id(holder->func->def->fid) != NULL);
#ifndef NDEBUG
	/* Paranoid check that the func is pinned by holder. */
	bool is_in_list = false;
	struct rlist *tmp;
	rlist_foreach(tmp, &holder->func->func_cache_pin_list)
		is_in_list = is_in_list || tmp == &holder->link;
	assert(is_in_list);
#endif
	rlist_del(&holder->link);
	holder->func = NULL;
}

bool
func_is_pinned(struct func *func, enum func_holder_type *type)
{
	assert(func_by_id(func->def->fid) != NULL);
	if (rlist_empty(&func->func_cache_pin_list))
		return false;
	struct func_cache_holder *h =
		rlist_first_entry(&func->func_cache_pin_list,
				  struct func_cache_holder, link);
	*type = h->type;
	return true;
}
