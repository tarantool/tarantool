#ifndef INCLUDES_TARANTOOL_BOX_USER_H
#define INCLUDES_TARANTOOL_BOX_USER_H
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
#include <stdint.h>
#include "user_def.h"

/** Global grants. */
struct universe {
	/** Global privileges this user has on the universe. */
	struct access access[BOX_USER_MAX];
};

/** A single instance of the universe. */
extern struct universe universe;

/** Bitmap type for used/unused authentication token map. */
typedef unsigned int umap_int_t;
enum {
	UMAP_INT_BITS = CHAR_BIT * sizeof(umap_int_t),
	USER_MAP_SIZE = (BOX_USER_MAX + UMAP_INT_BITS - 1)/UMAP_INT_BITS
};

struct user_map {
	umap_int_t m[USER_MAP_SIZE];
};

struct user: public user_def
{
	/**
	 * An id in privileges array to quickly find a
	 * respective privilege.
	 */
	uint8_t auth_token;
	/** List of users or roles this role has been granted to */
	struct user_map users;
};

/**
 * For best performance, all users are maintained in this array.
 * Position in the array is store in user->auth_token and also
 * in session->auth_token. This way it's easy to quickly find
 * the current user of the session.
 * An auth token, instead of a direct pointer, is stored in the
 * session because it makes dropping of a signed in user safe.
 * The same auth token (index in an array)
 * is also used to find out user privileges when accessing stored
 * objects, such as spaces and functions.
 */
extern struct user *guest_user, *admin_user;

/*
 * Insert or update user object (a cache entry
 * for user).
 * This is called from a trigger on _user table
 * and from trigger on _priv table, (in the latter
 * case, only when making a grant on the universe).
 *
 * If a user already exists, update it, otherwise
 * find space in users[] array and store the new
 * user in it. Update user->auth_token
 * with an index in the users[] array.
 */
struct user *
user_cache_replace(struct user_def *user);

/**
 * Find a user by id and delete it from the
 * users cache.
 */
void
user_cache_delete(uint32_t uid);

/** Find user by id. */
struct user *
user_by_id(uint32_t uid);

/* Find a user by name. Used by authentication. */
struct user *
user_cache_find(uint32_t uid);

struct user *
user_cache_find_by_name(const char *name, uint32_t len);

/** Initialize the user cache and access control subsystem. */
void
user_cache_init();

/** Cleanup the user cache and access control subsystem */
void
user_cache_free();

/* {{{ Roles */

/**
 * Check, mainly, that users & roles form an acyclic graph,
 * and no loop in the graph will occur when grantee gets
 * a given role.
 */
void
role_check(struct user *role, struct user *grantee);

/**
 * Grant a role to a user or another role.
 */
void
role_grant(struct user *grantee, struct user *role);

/**
 * Revoke a role from a user or another role.
 */
void
role_revoke(struct user *grantee, struct user *role);

/**
 * Re-evaluate effective access of a user based on this role
 * that is granted to him/her.
 * The role has its effective access already re-evaluated.
 */
void
user_set_effective_access(struct user *grantee, struct user *role);

/* }}} */

#endif /* INCLUDES_TARANTOOL_BOX_USER_H */
