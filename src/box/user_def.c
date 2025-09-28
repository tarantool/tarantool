/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "user_def.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "authentication.h"
#include "salad/grp_alloc.h"
#include "trivia/util.h"

const char *
priv_name(user_access_t access)
{
	static const char *priv_name_strs[] = {
		"Read",
		"Write",
		"Execute",
		"Session",
		"Usage",
		"Create",
		"Drop",
		"Alter",
		"Reference",
		"Trigger",
		"Insert",
		"Update",
		"Delete",
		"Grant",
		"Revoke",
	};
	int bit_no = __builtin_ffs((int) access);
	if (bit_no > 0 && bit_no <= (int) lengthof(priv_name_strs))
		return priv_name_strs[bit_no - 1];
	return "Any";
}

void
accesses_init(struct accesses *accesses)
{
	memset(accesses->access, 0, sizeof(accesses->access));
}

struct access
accesses_get(const struct accesses *accesses, auth_token_t auth_token)
{
	return accesses->access[auth_token];
}

void
accesses_set(struct accesses *accesses, auth_token_t auth_token,
	     struct access access)
{
	accesses->access[auth_token] = access;
}

struct user_def *
user_def_new(uint32_t uid, uint32_t owner, enum schema_object_type type,
	     const char *name, uint32_t name_len)
{
	struct user_def *def;
	struct grp_alloc all = grp_alloc_initializer();
	grp_alloc_reserve_data(&all, sizeof(*def));
	grp_alloc_reserve_str(&all, name_len);
	grp_alloc_use(&all, xmalloc(grp_alloc_size(&all)));
	def = grp_alloc_create_data(&all, sizeof(*def));
	def->uid = uid;
	def->owner = owner;
	def->type = type;
	def->auth = NULL;
	def->last_modified = 0;
	def->name = grp_alloc_create_str(&all, name, name_len);
	assert(grp_alloc_size(&all) == 0);
	return def;
}

void
user_def_delete(struct user_def *def)
{
	if (def->auth != NULL)
		authenticator_delete(def->auth);
	TRASH(def);
	free(def);
}
