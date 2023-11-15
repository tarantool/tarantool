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
#include "user.h"
#include "assoc.h"
#include "schema.h"
#include "space.h"
#include "func.h"
#include "index.h"
#include "bit/bit.h"
#include "fiber.h"
#include "salad/grp_alloc.h"
#include "scoped_guard.h"
#include "sequence.h"
#include "trivia/util.h"
#include "tt_static.h"

struct universe universe;
static struct user users[BOX_USER_MAX];
struct user *guest_user = users;
struct user *admin_user = users + 1;

static struct user_map user_map_nil;

struct mh_i32ptr_t *user_registry;

enum {
	USER_ACCESS_FULL = (user_access_t)~0,
};

/* {{{ user_map */

static inline int
user_map_calc_idx(uint8_t auth_token, uint8_t *bit_no)
{
	*bit_no = auth_token & (UMAP_INT_BITS - 1);
	return auth_token / UMAP_INT_BITS;
}


/** Set a bit in the user map - add a user. */
static inline void
user_map_set(struct user_map *map, uint8_t auth_token)
{
	uint8_t bit_no;
	int idx = user_map_calc_idx(auth_token, &bit_no);
	map->m[idx] |= ((umap_int_t) 1) << bit_no;
}

/** Clear a bit in the user map - remove a user. */
static inline void
user_map_clear(struct user_map *map, uint8_t auth_token)
{
	uint8_t bit_no;
	int idx = user_map_calc_idx(auth_token, &bit_no);
	map->m[idx] &= ~(((umap_int_t) 1) << bit_no);
}

/* Check if a bit is set in the user map. */
static inline bool
user_map_is_set(struct user_map *map, uint8_t auth_token)
{
	uint8_t bit_no;
	int idx = user_map_calc_idx(auth_token, &bit_no);
	return map->m[idx] & (((umap_int_t) 1) << bit_no);
}

/**
 * Merge two sets of users: add all users from right argument
 * to the left one.
 */
static void
user_map_union(struct user_map *lhs, struct user_map *rhs)
{
	for (int i = 0; i < USER_MAP_SIZE; i++)
		lhs->m[i] |= rhs->m[i];
}

/**
 * Remove all users present in rhs from lhs
 */
static void
user_map_minus(struct user_map *lhs, struct user_map *rhs)
{
	for (int i = 0; i < USER_MAP_SIZE; i++)
		lhs->m[i] &= ~rhs->m[i];
}

/** Iterate over users in the set of users. */
struct user_map_iterator
{
	struct bit_iterator it;
};

static void
user_map_iterator_init(struct user_map_iterator *it, struct user_map *map)
{
	bit_iterator_init(&it->it, map->m,
			  USER_MAP_SIZE * sizeof(umap_int_t), true);
}

static struct user *
user_map_iterator_next(struct user_map_iterator *it)
{
	size_t auth_token = bit_iterator_next(&it->it);
	if (auth_token != SIZE_MAX)
		return users + auth_token;
	return NULL;
}

/* }}} */

/* {{{ privset_t - set of effective privileges of a user */

extern "C" {

static int
priv_def_compare(const struct priv_def *lhs, const struct priv_def *rhs)
{
	if (lhs->object_type != rhs->object_type)
		return lhs->object_type > rhs->object_type ? 1 : -1;
	if (lhs->object_id != rhs->object_id)
		return lhs->object_id > rhs->object_id ? 1 : -1;
	int cmp = memcmp(lhs->object_name, rhs->object_name,
			 MIN(lhs->object_name_len, rhs->object_name_len));
	if (cmp != 0)
		return cmp;
	if (lhs->object_name_len != rhs->object_name_len)
		return lhs->object_name_len > rhs->object_name_len ? 1 : -1;
	return 0;
}

} /* extern "C" */

rb_gen(, privset_, privset_t, struct priv_def, link, priv_def_compare);

/* }}}  */

/** {{{ user */

static void
user_create(struct user *user, uint8_t auth_token)
{
	assert(user->auth_token == 0);
	user->auth_token = auth_token;
	privset_new(&user->privs);
	region_create(&user->pool, &cord()->slabc);
	rlist_create(&user->credentials_list);
}

static void
user_destroy(struct user *user)
{
	while (!rlist_empty(&user->credentials_list))
		rlist_shift(&user->credentials_list);
	/*
	 * Sic: we don't have to remove a deleted
	 * user from users set of roles, since
	 * to drop a user, one has to revoke
	 * all privileges from them first.
	 */
	region_destroy(&user->pool);
	user_def_delete(user->def);
	memset(user, 0, sizeof(*user));
}

/**
 * Add a privilege definition to the list
 * of effective privileges of a user.
 */
void
user_grant_priv(struct user *user, const struct priv_def *def)
{
	struct priv_def *old = privset_search(&user->privs, def);
	if (old == NULL) {
		old = xregion_alloc_object(&user->pool, typeof(*old));
		*old = *def;
		if (def->object_name != NULL) {
			char *object_name = (char *)xregion_alloc(
					&user->pool, def->object_name_len);
			memcpy(object_name, def->object_name,
			       def->object_name_len);
			old->object_name = object_name;
		}
		privset_insert(&user->privs, old);
	} else {
		old->access |= def->access;
	}
}

/**
 * Stores cached runtime access information for global Lua functions.
 * Nodes are created on demand and removed if empty.
 */
static mh_strnptr_t *access_lua_call_registry;

/** Node in access_lua_call_registry. */
struct access_lua_call_node {
	/** Name of the global Lua function this node is for. */
	const char *name;
	/** Length of the name string. */
	uint32_t name_len;
	/** Cached runtime access information. */
	struct access access[BOX_USER_MAX];
};

struct access *
access_lua_call_find(const char *name, uint32_t name_len)
{
	struct mh_strnptr_t *h = access_lua_call_registry;
	mh_int_t i = mh_strnptr_find_str(h, name, name_len);
	if (i != mh_end(h)) {
		struct access_lua_call_node *node =
			(typeof(node))mh_strnptr_node(h, i)->val;
		return node->access;
	}
	return NULL;
}

/**
 * Returns cached runtime access information for the given Lua function name.
 * Creates one if it doesn't exist.
 */
static struct access *
access_lua_call_find_or_create(const char *name, uint32_t name_len)
{
	uint32_t name_hash = mh_strn_hash(name, name_len);
	struct mh_strnptr_t *h = access_lua_call_registry;
	struct mh_strnptr_key_t k = {name, name_len, name_hash};
	mh_int_t i = mh_strnptr_find(h, &k, NULL);
	if (i != mh_end(h)) {
		struct access_lua_call_node *node =
			(typeof(node))mh_strnptr_node(h, i)->val;
		return node->access;
	}
	struct access_lua_call_node *node;
	grp_alloc all = grp_alloc_initializer();
	grp_alloc_reserve_data(&all, sizeof(*node));
	grp_alloc_reserve_str(&all, name_len);
	grp_alloc_use(&all, xmalloc(grp_alloc_size(&all)));
	node = (typeof(node))grp_alloc_create_data(&all, sizeof(*node));
	node->name = grp_alloc_create_str(&all, name, name_len);
	node->name_len = name_len;
	memset(node->access, 0, sizeof(node->access));
	assert(grp_alloc_size(&all) == 0);
	mh_strnptr_node_t n = {node->name, name_len, name_hash, node};
	mh_strnptr_put(h, &n, NULL, NULL);
	return node->access;
}

/**
 * Deletes cached runtime access information for a Lua function if it's empty
 * (i.e. grants no access to any user).
 */
static void
access_lua_call_delete_if_empty(struct access *object)
{
	for (int i = 0; i < BOX_USER_MAX; ++i) {
		struct access *access = &object[i];
		if (access->granted != 0 || access->effective != 0)
			return;
	}
	struct mh_strnptr_t *h = access_lua_call_registry;
	struct access_lua_call_node *node = (typeof(node))(
		(char *)object - offsetof(typeof(*node), access));
	mh_int_t i = mh_strnptr_find_str(h, node->name, node->name_len);
	assert(i != mh_end(h));
	assert(mh_strnptr_node(h, i)->val == node);
	mh_strnptr_del(h, i, NULL);
	free(node);
}

/**
 * Find the corresponding access structure for the given privilege.
 * Must be released with access_put() after use.
 */
static struct access *
access_get(const struct priv_def *priv)
{
	switch (priv->object_type) {
	case SC_UNIVERSE:
		return universe.access;
	case SC_LUA_CALL:
		if (priv->is_entity_access)
			return universe.access_lua_call;
		/*
		 * lua_call objects aren't persisted in the database so
		 * we create an access struct on demand and delete it in
		 * access_put() if it's empty.
		 */
		return access_lua_call_find_or_create(priv->object_name,
						      priv->object_name_len);
	case SC_LUA_EVAL:
		return universe.access_lua_eval;
	case SC_SQL:
		return universe.access_sql;
	case SC_SPACE:
	{
		if (priv->is_entity_access)
			return entity_access.space;
		struct space *space = space_by_id(priv->object_id);
		return space != NULL ? space->access : NULL;
	}
	case SC_FUNCTION:
	{
		if (priv->is_entity_access)
			return entity_access.function;
		struct func *func = func_by_id(priv->object_id);
		return func != NULL ? func->access : NULL;
	}
	case SC_USER:
	{
		if (priv->is_entity_access)
			return entity_access.user;
		struct user *user = user_by_id(priv->object_id);
		return user != NULL ? user->access : NULL;
	}
	case SC_ROLE:
	{
		if (priv->is_entity_access)
			return entity_access.role;
		struct user *role = user_by_id(priv->object_id);
		return role != NULL ? role->access : NULL;
	}
	case SC_SEQUENCE:
	{
		if (priv->is_entity_access)
			return entity_access.sequence;
		struct sequence *seq = sequence_by_id(priv->object_id);
		return seq != NULL ? seq->access : NULL;
	}
	default:
		return NULL;
	}
}

/** Releases an object returned by access_get(). */
static void
access_put(const struct priv_def *priv, struct access *object)
{
	switch (priv->object_type) {
	case SC_LUA_CALL:
		/*
		 * lua_call objects aren't persisted in the database so
		 * we don't need to keep an access struct if it's empty.
		 */
		if (!priv->is_entity_access)
			access_lua_call_delete_if_empty(object);
		break;
	default:
		break;
	}
}

/**
 * Reset effective access of the user in the
 * corresponding objects.
 */
static void
user_set_effective_access(struct user *user)
{
	struct privset_iterator it;
	privset_ifirst(&user->privs, &it);
	struct priv_def *priv;
	while ((priv = privset_inext(&it)) != NULL) {
		struct access *object = access_get(priv);
		 /* Protect against a concurrent drop. */
		if (object == NULL)
			continue;
		struct access *access = &object[user->auth_token];
		access->effective = access->granted | priv->access;
		access_put(priv, object);
	}
}

/**
 * Reload user privileges and re-grant them.
 */
static int
user_reload_privs(struct user *user)
{
	if (user->is_dirty == false)
		return 0;
	struct priv_def *priv;
	/**
	 * Reset effective access of the user in the
	 * corresponding objects to have
	 * only the stuff that it's granted directly.
	 */
	struct privset_iterator it;
	privset_ifirst(&user->privs, &it);
	while ((priv = privset_inext(&it)) != NULL) {
		priv->access = 0;
	}
	user_set_effective_access(user);
	region_free(&user->pool);
	privset_new(&user->privs);
	/* Load granted privs from _priv space. */
	{
		struct space *space = space_cache_find(BOX_PRIV_ID);
		if (space == NULL)
			return -1;
		char key[6];
		/** Primary key - by user id */
		if (!space_is_memtx(space)) {
			diag_set(ClientError, ER_UNSUPPORTED,
			          space->engine->name, "system data");
			return -1;
		}
		struct index *index = index_find(space, 0);
		if (index == NULL)
			return -1;
		mp_encode_uint(key, user->def->uid);

		struct iterator *it = index_create_iterator(index, ITER_EQ,
							       key, 1);
		if (it == NULL)
			return -1;
		IteratorGuard iter_guard(it);

		struct tuple *tuple;
		if (iterator_next(it, &tuple) != 0)
			return -1;
		while (tuple != NULL) {
			struct priv_def priv;
			if (priv_def_create_from_tuple(&priv, tuple) != 0)
				return -1;
			/**
			 * Skip role grants, we're only
			 * interested in real objects.
			 */
			if (priv.object_type != SC_ROLE || !(priv.access & PRIV_X))
				user_grant_priv(user, &priv);
			if (iterator_next(it, &tuple) != 0)
				return -1;
		}
	}
	{
		/* Take into account privs granted through roles. */
		struct user_map_iterator it;
		user_map_iterator_init(&it, &user->roles);
		struct user *role;
		while ((role = user_map_iterator_next(&it))) {
			struct privset_iterator it;
			privset_ifirst(&role->privs, &it);
			struct priv_def *def;
			while ((def = privset_inext(&it)))
				user_grant_priv(user, def);
		}
	}
	user_set_effective_access(user);
	user->is_dirty = false;
	struct credentials *creds;
	user_access_t new_access = universe.access[user->auth_token].effective;
	rlist_foreach_entry(creds, &user->credentials_list, in_user)
		creds->universal_access = new_access;
	return 0;
}

/** }}} */

/* {{{ authentication tokens */

/** A map to quickly look up free slots in users[] array. */
static umap_int_t tokens[USER_MAP_SIZE];
/**
 * Index of the minimal element of the tokens array which
 * has an unused token.
 */
static int min_token_idx = 0;

/**
 * Find and return a spare authentication token.
 * Raise an exception when the maximal number of users
 * is reached (and we're out of tokens).
 */
uint8_t
auth_token_get(void)
{
	uint8_t bit_no = 0;
	while (min_token_idx < USER_MAP_SIZE) {
                bit_no = __builtin_ffs(tokens[min_token_idx]);
		if (bit_no)
			break;
		min_token_idx++;
        }
	if (bit_no == 0 || bit_no > BOX_USER_MAX) {
		/* A cap on the number of users was reached.
		 * Check for BOX_USER_MAX to cover case when
		 * USER_MAP_BITS > BOX_USER_MAX.
		 */
		tnt_raise(LoggedError, ER_USER_MAX, BOX_USER_MAX);
	}
        /*
         * find-first-set returns bit index starting from 1,
         * or 0 if no bit is set. Rebase the index to offset 0.
         */
	bit_no--;
	tokens[min_token_idx] ^= ((umap_int_t) 1) << bit_no;
	int auth_token = min_token_idx * UMAP_INT_BITS + bit_no;
	assert(auth_token < UINT8_MAX);
	return auth_token;
}

/**
 * Return an authentication token to the set of unused
 * tokens.
 */
void
auth_token_put(uint8_t auth_token)
{
	uint8_t bit_no;
	int idx = user_map_calc_idx(auth_token, &bit_no);
	tokens[idx] |= ((umap_int_t) 1) << bit_no;
	if (idx < min_token_idx)
		min_token_idx = idx;
}

/* }}} */

/* {{{ user cache */

struct user *
user_cache_replace(struct user_def *def)
{
	struct user *user = user_by_id(def->uid);
	if (user == NULL) {
		uint8_t auth_token = auth_token_get();
		user = users + auth_token;
		user_create(user, auth_token);
		struct mh_i32ptr_node_t node = { def->uid, user };
		mh_i32ptr_put(user_registry, &node, NULL, NULL);
	} else {
		user_def_delete(user->def);
	}
	user->def = def;
	return user;
}

void
user_cache_delete(uint32_t uid)
{
	mh_int_t k = mh_i32ptr_find(user_registry, uid, NULL);
	if (k != mh_end(user_registry)) {
		struct user *user = (struct user *)
			mh_i32ptr_node(user_registry, k)->val;
		assert(user->auth_token > ADMIN);
		auth_token_put(user->auth_token);
		assert(user_map_is_empty(&user->roles));
		assert(user_map_is_empty(&user->users));
		user_destroy(user);
		/*
		 * Sic: we don't have to remove a deleted
		 * user from users hash of roles, since
		 * to drop a user, one has to revoke
		 * all privileges from them first.
		 */
		mh_i32ptr_del(user_registry, k, NULL);
	}
}

/** Find user by id. */
struct user *
user_by_id(uint32_t uid)
{
	mh_int_t k = mh_i32ptr_find(user_registry, uid, NULL);
	if (k == mh_end(user_registry))
		return NULL;
	return (struct user *) mh_i32ptr_node(user_registry, k)->val;
}

struct user *
user_find(uint32_t uid)
{
	struct user *user = user_by_id(uid);
	if (user == NULL)
		diag_set(ClientError, ER_NO_SUCH_USER, int2str(uid));
	return user;
}

/* Find a user by authentication token. */
struct user *
user_find_by_token(uint8_t auth_token)
{
    return &users[auth_token];
}

/** Find user by name. */
struct user *
user_find_by_name(const char *name, uint32_t len)
{
	uint32_t uid;
	if (schema_find_id(BOX_USER_ID, 2, name, len, &uid) != 0)
		return NULL;
	if (uid != BOX_ID_NIL) {
		struct user *user = user_by_id(uid);
		if (user != NULL && user->def->type == SC_USER)
			return user;
	}
	diag_set(ClientError, ER_NO_SUCH_USER,
		 tt_cstr(name, MIN((uint32_t) BOX_INVALID_NAME_MAX, len)));
	return NULL;
}

void
user_cache_init(void)
{
	memset(users, 0, sizeof(users));
	/** Mark all tokens as unused. */
	memset(tokens, 0xFF, sizeof(tokens));
	user_registry = mh_i32ptr_new();
	access_lua_call_registry = mh_strnptr_new();
	/*
	 * Solve a chicken-egg problem:
	 * we need a functional user cache entry for superuser to
	 * perform recovery, but the superuser credentials are
	 * stored in the snapshot. So, pre-create cache entries
	 * for 'guest' and 'admin' users here, they will be
	 * updated with snapshot contents during recovery.
	 */
	const char *name = "guest";
	struct user_def *def = user_def_new(GUEST, ADMIN, SC_USER,
					    name, strlen(name));
	/* Free def in a case of exception. */
	auto guest_def_guard = make_scoped_guard([=] { user_def_delete(def); });
	struct user *user = user_cache_replace(def);
	/* Now the user cache owns the def. */
	guest_def_guard.is_active = false;
	/* 0 is the auth token and user id by default. */
	assert(user->def->uid == GUEST && user->auth_token == GUEST);
	(void) user;

	name = "admin";
	def = user_def_new(ADMIN, ADMIN, SC_USER, name, strlen(name));
	auto admin_def_guard = make_scoped_guard([=] { user_def_delete(def); });
	user = user_cache_replace(def);
	admin_def_guard.is_active = false;
	/*
	 * For performance reasons, we do not always explicitly
	 * look at user id in access checks, while still need to
	 * ensure 'admin' user has full access to all objects in
	 * the universe.
	 *
	 * This is why  _priv table contains a record with grants
	 * of full access to universe to 'admin' user.
	 *
	 * Making a record in _priv table is, however,
	 * insufficient, since some checks are done at bootstrap,
	 * before _priv table is read (e.g. when we're
	 * bootstrapping a replica in applier fiber).
	 *
	 * When user_cache_init() is called, admin user access is
	 * not loaded yet (is 0), force global access.
	 */
	universe.access[ADMIN].effective = USER_ACCESS_FULL;
	/* ADMIN is both the auth token and user id for 'admin' user. */
	assert(user->def->uid == ADMIN && user->auth_token == ADMIN);
}

void
user_cache_free(void)
{
	if (user_registry != NULL) {
		mh_i32ptr_delete(user_registry);
		user_registry = NULL;
	}
	if (access_lua_call_registry != NULL) {
		struct mh_strnptr_t *h = access_lua_call_registry;
		mh_int_t i;
		mh_foreach(h, i)
			free(mh_strnptr_node(h, i)->val);
		mh_strnptr_delete(h);
		access_lua_call_registry = NULL;
	}
}

/* }}} user cache */

/** {{{ roles */

int
role_check(struct user *grantee, struct user *role)
{
	/*
	 * Check that there is no loop from grantee to role:
	 * if grantee is a role, build up a closure of all
	 * immediate and indirect users of grantee, and ensure
	 * the granted role is not in this set.
	 */
	struct user_map transitive_closure = user_map_nil;
	user_map_set(&transitive_closure, grantee->auth_token);
	struct user_map current_layer = transitive_closure;
	while (! user_map_is_empty(&current_layer)) {
		/*
		 * As long as we're traversing a directed
		 * acyclic graph, we're bound to end at some
		 * point in a layer with no incoming edges.
		 */
		struct user_map next_layer = user_map_nil;
		struct user_map_iterator it;
		user_map_iterator_init(&it, &current_layer);
		struct user *user;
		while ((user = user_map_iterator_next(&it)))
			user_map_union(&next_layer, &user->users);
		user_map_union(&transitive_closure, &next_layer);
		current_layer = next_layer;
	}
	/*
	 * Check if the role is in the list of roles to which the
	 * grantee is granted.
	 */
	if (user_map_is_set(&transitive_closure,
			    role->auth_token)) {
		diag_set(ClientError, ER_ROLE_LOOP,
			  role->def->name, grantee->def->name);
		return -1;
	}
	return 0;
}

/**
 * Re-calculate effective grants of the linked subgraph
 * this user/role is a part of.
 */
int
rebuild_effective_grants(struct user *grantee)
{
	/*
	 * Recurse over all roles to which grantee is granted
	 * and mark them as dirty - in need for rebuild.
	 */
	struct user_map_iterator it;
	struct user *user;
	struct user_map current_layer = user_map_nil;
	user_map_set(&current_layer, grantee->auth_token);
	while (!user_map_is_empty(&current_layer)) {
		struct user_map next_layer = user_map_nil;
		user_map_iterator_init(&it, &current_layer);
		while ((user = user_map_iterator_next(&it))) {
			user->is_dirty = true;
			user_map_union(&next_layer, &user->users);
		}
		/*
		 * Switch to the nodes which are not in the set
		 * yet.
		 */
		current_layer = next_layer;
	}
	/*
	 * First, construct a subset of the transitive
	 * closure consisting from the nodes with no
	 * incoming edges (roles which have no granted
	 * roles). Build their list of effective grants
	 * from their actual grants.
	 *
	 * Propagate the effective grants through the
	 * outgoing edges of the nodes, avoiding the nodes
	 * with incoming edges from not-yet-evaluated nodes.
	 * Eventually this process will end with a set of
	 * nodes with no outgoing edges.
	 */
	struct user_map transitive_closure = user_map_nil;
	current_layer = user_map_nil;
	user_map_set(&current_layer, grantee->auth_token);
	/*
	 * Propagate effective privileges from the nodes
	 * with no incoming edges to the remaining nodes.
	 */
	while (! user_map_is_empty(&current_layer)) {
		struct user_map postponed = user_map_nil;
		struct user_map next_layer = user_map_nil;
		user_map_iterator_init(&it, &current_layer);
		while ((user = user_map_iterator_next(&it))) {
			struct user_map indirect_edges = user->roles;
			user_map_minus(&indirect_edges, &transitive_closure);
			if (user_map_is_empty(&indirect_edges)) {
				if (user_reload_privs(user) != 0)
					return -1;
				user_map_union(&next_layer, &user->users);
			} else {
				/*
				 * The user has roles whose
				 * effective grants have not been
				 * calculated yet. Postpone
				 * evaluation of effective grants
				 * of this user till these roles'
				 * effective grants have been
				 * built.
				 */
				user_map_union(&next_layer, &indirect_edges);
				user_map_set(&postponed, user->auth_token);
				user_map_set(&next_layer, user->auth_token);
			}
		}
		user_map_minus(&current_layer, &postponed);
		user_map_union(&transitive_closure, &current_layer);
		current_layer = next_layer;
	}
	return 0;
}


/**
 * Update verges in the graph of dependencies.
 * Grant all effective privileges of the role to whoever
 * this role was granted to.
 */
int
role_grant(struct user *grantee, struct user *role)
{
	user_map_set(&role->users, grantee->auth_token);
	user_map_set(&grantee->roles, role->auth_token);
	if (rebuild_effective_grants(grantee) != 0)
		return -1;
	return 0;
}

/**
 * Update the role dependencies graph.
 * Rebuild effective privileges of the grantee.
 */
int
role_revoke(struct user *grantee, struct user *role)
{
	user_map_clear(&role->users, grantee->auth_token);
	user_map_clear(&grantee->roles, role->auth_token);
	if (rebuild_effective_grants(grantee) != 0)
		return -1;
	return 0;
}

int
priv_grant(struct user *grantee, struct priv_def *priv)
{
	struct access *object = access_get(priv);
	if (object == NULL)
		return 0;
	if (grantee->auth_token == ADMIN && priv->object_type == SC_UNIVERSE &&
	    priv->access != USER_ACCESS_FULL) {
		diag_set(ClientError, ER_GRANT,
			 "can't revoke universe from the admin user");
		access_put(priv, object);
		return -1;
	}
	struct access *access = &object[grantee->auth_token];
	access->granted = priv->access;
	access_put(priv, object);
	if (rebuild_effective_grants(grantee) != 0)
		return -1;
	return 0;
}

/** }}} */

void
credentials_create(struct credentials *cr, struct user *user)
{
	cr->auth_token = user->auth_token;
	cr->universal_access = universe.access[user->auth_token].effective;
	cr->uid = user->def->uid;
	rlist_add_entry(&user->credentials_list, cr, in_user);
}

void
credentials_create_empty(struct credentials *cr)
{
	cr->auth_token = BOX_USER_MAX;
	cr->universal_access = 0;
	cr->uid = BOX_USER_MAX;
	rlist_create(&cr->in_user);
}

void
credentials_destroy(struct credentials *cr)
{
	rlist_del_entry(cr, in_user);
}
