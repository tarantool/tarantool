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
#include "session.h"
#include "scoped_guard.h"
#include "sequence.h"

struct universe universe;
static struct user users[BOX_USER_MAX];
struct user *guest_user = users;
struct user *admin_user = users + 1;

static struct user_map user_map_nil;

struct mh_i32ptr_t *user_registry;

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
}

static void
user_destroy(struct user *user)
{
	/*
	 * Sic: we don't have to remove a deleted
	 * user from users set of roles, since
	 * to drop a user, one has to revoke
	 * all privileges from them first.
	 */
	region_destroy(&user->pool);
	free(user->def);
	memset(user, 0, sizeof(*user));
}

/**
 * Add a privilege definition to the list
 * of effective privileges of a user.
 */
void
user_grant_priv(struct user *user, struct priv_def *def)
{
	struct priv_def *old = privset_search(&user->privs, def);
	if (old == NULL) {
		old = (struct priv_def *)
			region_alloc_xc(&user->pool, sizeof(struct priv_def));
		*old = *def;
		privset_insert(&user->privs, old);
	} else {
		old->access |= def->access;
	}
}

/**
 * Find the corresponding access structure
 * given object type and object id.
 */
struct access *
access_find(struct priv_def *priv)
{
	struct access *access = NULL;
	switch (priv->object_type) {
	case SC_UNIVERSE:
	{
		access = universe.access;
		break;
	}
	case SC_SPACE:
	{
		if (priv->object_id == 0) {
			access = entity_access.space;
			break;
		}
		struct space *space = space_by_id(priv->object_id);
		if (space)
			access = space->access;
		break;
	}
	case SC_FUNCTION:
	{
		if (priv->object_id == 0) {
			access = entity_access.function;
			break;
		}
		struct func *func = func_by_id(priv->object_id);
		if (func)
			access = func->access;
		break;
	}
	case SC_SEQUENCE:
	{
		if (priv->object_id == 0) {
			access = entity_access.sequence;
			break;
		}
		struct sequence *seq = sequence_by_id(priv->object_id);
		if (seq)
			access = seq->access;
		break;
	}
	default:
		break;
	}
	return access;
}


/**
 * Reset effective access of the user in the
 * corresponding objects.
 */
static void
user_set_effective_access(struct user *user)
{
	struct credentials *cr = effective_user();
	struct privset_iterator it;
	privset_ifirst(&user->privs, &it);
	struct priv_def *priv;
	while ((priv = privset_inext(&it)) != NULL) {
		struct access *object = access_find(priv);
		 /* Protect against a concurrent drop. */
		if (object == NULL)
			continue;
		struct access *access = &object[user->auth_token];
		access->effective = access->granted | priv->access;
		/** Update global access in the current session. */
		if (priv->object_type == SC_UNIVERSE &&
		    user->def->uid == cr->uid) {
			cr->universal_access = access->effective;
		}
	}
}

/**
 * Reload user privileges and re-grant them.
 */
static void
user_reload_privs(struct user *user)
{
	if (user->is_dirty == false)
		return;
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
		struct space *space = space_cache_find_xc(BOX_PRIV_ID);
		char key[6];
		/** Primary key - by user id */
		struct index *index = index_find_system_xc(space, 0);
		mp_encode_uint(key, user->def->uid);

		struct iterator *it = index_create_iterator_xc(index, ITER_EQ,
							       key, 1);
		IteratorGuard iter_guard(it);

		struct tuple *tuple;
		while ((tuple = iterator_next_xc(it)) != NULL) {
			struct priv_def priv;
			priv_def_create_from_tuple(&priv, tuple);
			/**
			 * Skip role grants, we're only
			 * interested in real objects.
			 */
			if (priv.object_type != SC_ROLE)
				user_grant_priv(user, &priv);
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
			while ((def = privset_inext(&it))) {
				user_grant_priv(user, def);
			}
		}
	}
	user_set_effective_access(user);
	user->is_dirty = false;
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
auth_token_get()
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
		free(user->def);
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

/** Find user by name. */
struct user *
user_find_by_name(const char *name, uint32_t len)
{
	uint32_t uid = schema_find_id(BOX_USER_ID, 2, name, len);
	struct user *user = user_by_id(uid);
	if (user == NULL || user->def->type != SC_USER) {
		diag_set(ClientError, ER_NO_SUCH_USER, tt_cstr(name, len));
		return NULL;
	}
	return user;
}

void
user_cache_init()
{
	/** Mark all tokens as unused. */
	memset(tokens, 0xFF, sizeof(tokens));
	user_registry = mh_i32ptr_new();
	/*
	 * Solve a chicken-egg problem:
	 * we need a functional user cache entry for superuser to
	 * perform recovery, but the superuser credentials are
	 * stored in the snapshot. So, pre-create cache entries
	 * for 'guest' and 'admin' users here, they will be
	 * updated with snapshot contents during recovery.
	 */
	size_t name_len = strlen("guest");
	size_t sz = user_def_sizeof(name_len);
	struct user_def *def = (struct user_def *) calloc(1, sz);
	if (def == NULL)
		tnt_raise(OutOfMemory, sz, "malloc", "def");
	/* Free def in a case of exception. */
	auto guest_def_guard = make_scoped_guard([=] { free(def); });
	memcpy(def->name, "guest", name_len);
	def->owner = ADMIN;
	def->type = SC_USER;
	struct user *user = user_cache_replace(def);
	/* Now the user cache owns the def. */
	guest_def_guard.is_active = false;
	/* 0 is the auth token and user id by default. */
	assert(user->def->uid == GUEST && user->auth_token == GUEST);
	(void) user;

	name_len = strlen("admin");
	sz = user_def_sizeof(name_len);
	def = (struct user_def *) calloc(1, sz);
	if (def == NULL)
		tnt_raise(OutOfMemory, sz, "malloc", "def");
	auto admin_def_guard = make_scoped_guard([=] { free(def); });
	memcpy(def->name, "admin", name_len);
	def->uid = def->owner = ADMIN;
	def->type = SC_USER;
	user = user_cache_replace(def);
	admin_def_guard.is_active = false;
	/* ADMIN is both the auth token and user id for 'admin' user. */
	assert(user->def->uid == ADMIN && user->auth_token == ADMIN);
}

void
user_cache_free()
{
	if (user_registry)
		mh_i32ptr_delete(user_registry);
}

/* }}} user cache */

/** {{{ roles */

void
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
		tnt_raise(ClientError, ER_ROLE_LOOP,
			  role->def->name, grantee->def->name);
	}
}

/**
 * Re-calculate effective grants of the linked subgraph
 * this user/role is a part of.
 */
void
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
				user_reload_privs(user);
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
}


/**
 * Update verges in the graph of dependencies.
 * Grant all effective privileges of the role to whoever
 * this role was granted to.
 */
void
role_grant(struct user *grantee, struct user *role)
{
	user_map_set(&role->users, grantee->auth_token);
	user_map_set(&grantee->roles, role->auth_token);
	rebuild_effective_grants(grantee);
}

/**
 * Update the role dependencies graph.
 * Rebuild effective privileges of the grantee.
 */
void
role_revoke(struct user *grantee, struct user *role)
{
	user_map_clear(&role->users, grantee->auth_token);
	user_map_clear(&grantee->roles, role->auth_token);
	rebuild_effective_grants(grantee);
}

void
priv_grant(struct user *grantee, struct priv_def *priv)
{
	struct access *object = access_find(priv);
	if (object == NULL)
		return;
	struct access *access = &object[grantee->auth_token];
	assert(privset_search(&grantee->privs, priv) || access->granted == 0);
	access->granted = priv->access;
	rebuild_effective_grants(grantee);
}

/** }}} */
