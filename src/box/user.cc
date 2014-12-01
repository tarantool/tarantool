/*
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
#include "user_def.h"
#include "assoc.h"
#include "schema.h"
#include "bit/bit.h"

struct universe universe;
static struct user users[BOX_USER_MAX];
struct user *guest_user = users;
struct user *admin_user = users + 1;

struct mh_i32ptr_t *user_registry;

/* {{{ user_map */

/* Initialize an empty user map. */
void
user_map_init(struct user_map *map)
{
	memset(map, 0, sizeof(*map));
}

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

static inline bool
user_map_is_empty(struct user_map *map)
{
	for (int i = 0; i < USER_MAP_SIZE; i++)
		if (map->m[i])
			return false;
	return true;
}

/**
 * Merge two sets of users: add all users from right argument
 * to the left one.
 */
void
user_map_union(struct user_map *lhs, struct user_map *rhs)
{
	for (int i = 0; i < USER_MAP_SIZE; i++)
		lhs->m[i] |= rhs->m[i];
}

/** Iterate over users in the set of users. */
struct user_map_iterator
{
	struct bit_iterator it;
};

void
user_map_iterator_init(struct user_map_iterator *it, struct user_map *map)
{
	bit_iterator_init(&it->it, map->m,
			  USER_MAP_SIZE * sizeof(umap_int_t), true);
}

struct user *
user_map_iterator_next(struct user_map_iterator *it)
{
	size_t auth_token = bit_iterator_next(&it->it);
	if (auth_token != SIZE_MAX)
		return users + auth_token;
	return NULL;
}

/* }}} */

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
		assert(user->auth_token == 0);
		user->auth_token = auth_token;
		struct mh_i32ptr_node_t node = { def->uid, user };
		mh_i32ptr_put(user_registry, &node, NULL, NULL);
	}
	*(struct user_def *) user = *def;
	return user;
}

void
user_cache_delete(uint32_t uid)
{
	struct user *user = user_by_id(uid);
	if (user) {
		assert(user->auth_token > ADMIN);
		auth_token_put(user->auth_token);
		/*
		 * Sic: we don't have to remove a deleted
		 * user from users hash of roles, since
		 * to drop a user, one has to revoke
		 * all privileges from them first.
		 */
		memset(user, 0, sizeof(*user));
		mh_i32ptr_del(user_registry, uid, NULL);
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
user_cache_find(uint32_t uid)
{
	struct user *user = user_by_id(uid);
	if (user)
		return user;
	tnt_raise(ClientError, ER_NO_SUCH_USER, int2str(uid));
}

/** Find user by name. */
struct user *
user_cache_find_by_name(const char *name, uint32_t len)
{
	uint32_t uid = schema_find_id(SC_USER_ID, 2, name, len);
	struct user *user = user_by_id(uid);
	if (user == NULL || user->type != SC_USER) {
		char name_buf[BOX_NAME_MAX + 1];
		/* \0 - to correctly print user name the error message. */
		snprintf(name_buf, sizeof(name_buf), "%.*s", len, name);
		tnt_raise(ClientError, ER_NO_SUCH_USER, name_buf);
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
	struct user_def def;
	memset(&def, 0, sizeof(def));
	snprintf(def.name, sizeof(def.name), "guest");
	def.owner = ADMIN;
	def.type = SC_USER;
	struct user *user = user_cache_replace(&def);
	/* 0 is the auth token and user id by default. */
	assert(user->uid == GUEST && user->auth_token == GUEST);

	memset(&def, 0, sizeof(def));
	snprintf(def.name, sizeof(def.name), "admin");
	def.uid = def.owner = ADMIN;
	def.type = SC_USER;
	user = user_cache_replace(&def);
	/* ADMIN is both the auth token and user id for 'admin' user. */
	assert(user->uid == ADMIN && user->auth_token == ADMIN);
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
	struct user_map transitive_closure;
	user_map_init(&transitive_closure);
	user_map_set(&transitive_closure, grantee->auth_token);
	struct user_map current_layer = transitive_closure;
	while (! user_map_is_empty(&current_layer)) {
		/*
		 * As long as we're traversing a directed
		 * acyclic graph, we're bound to end at some
		 * point in a layer with no incoming edges.
		 */
		struct user_map next_layer;
		user_map_init(&next_layer);
		struct user_map_iterator it;
		user_map_iterator_init(&it, &current_layer);
		struct user *user;
		while ((user = user_map_iterator_next(&it)))
			user_map_union(&next_layer, &user->users);
		user_map_union(&transitive_closure, &next_layer);
		current_layer = next_layer;
	}

	if (user_map_is_set(&transitive_closure,
			    role->auth_token)) {
		tnt_raise(ClientError, ER_ROLE_LOOP,
			  role->name, grantee->name);
	}
}

void
role_grant(struct user *grantee, struct user *role)
{
	user_map_set(&role->users, grantee->auth_token);
	/**
	 * Todo: grant all effective privileges of
	 * the role to whoever this role was granted
	 * to.
	 */
}

void
role_revoke(struct user *grantee, struct user *role)
{
	user_map_clear(&role->users, grantee->auth_token);
	/**
	 * Todo: rebuild effective privileges of grantee,
	 * for all effective privileges which he/she
	 * might have inherited through the revoked role.
	 */
}

void
privilege_grant(struct user *user,
		struct access *object, uint8_t access)
{
	bool grant = access > object[user->auth_token].granted;
	object[user->auth_token].granted = access;
	if (grant) {
		/*
		 * Grant the privilege to this user or
		 * role and all users to which this
		 * role has been granted, if this is
		 * a role.
		 */
		struct user_map current_layer;
		user_map_init(&current_layer);
		user_map_set(&current_layer, user->auth_token);
		while (!user_map_is_empty(&current_layer)) {
			struct user_map next_layer;
			user_map_init(&next_layer);
			struct user_map_iterator it;
			user_map_iterator_init(&it, &current_layer);
			while ((user = user_map_iterator_next(&it))) {
				object[user->auth_token].effective |= access;
				user_map_union(&next_layer, &user->users);
			}
			current_layer = next_layer;
		}
	} else {
		/**
		 * @fixme: this only works for users and
		 * non-recursive roles
		 */
		object[user->auth_token].effective =
			object[user->auth_token].granted;
	}
}

/** }}} */
