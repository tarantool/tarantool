#ifndef INCLUDES_TARANTOOL_BOX_ACCESS_H
#define INCLUDES_TARANTOOL_BOX_ACCESS_H
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
#include "iproto_constants.h"
#include "key_def.h"
#include "scramble.h"
#include "fiber.h"
#include "session.h"

enum {
	/* SELECT */
	PRIV_R = 1,
	/* INSERT, UPDATE, DELETE, REPLACE */
	PRIV_W = 2,
	/* CALL */
	PRIV_X = 4,
};

/* Privilege name for error messages */
const char *
priv_name(uint8_t access);

/**
 * A cache entry for an existing user. Entries for all existing
 * users are always present in the cache. The entry is maintained
 * in sync with _user and _priv system spaces by system space
 * triggers.
 * @sa alter.cc
 */
struct user {
	/** User id. */
	uint32_t uid;
	/** Creator of the user */
	uint32_t owner;
	/** User password - hash2 */
	char hash2[SCRAMBLE_SIZE];
	/** User name - for error messages and debugging */
	char name[BOX_NAME_MAX + 1];
	/** Global privileges this user has on the universe. */
	uint8_t universal_access;
	/** An id in users[] array to quickly find user */
	uint8_t auth_token;
};

/**
 * For best performance, all users are maintained in this array.
 * Position in the array is store in user->auth_token and also
 * in session->auth_token. This way it's easy to quickly find
 * the current user of the session.
 * An auth token, instead of a direct pointer, is stored in the
 * session because it make dropping of a signed in user safe.
 * The same auth token (index in an array)
 * is also used to find out user privileges when accessing stored
 * objects, such as spaces and functions.
 */
extern struct user users[];

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
void
user_cache_replace(struct user *user);

/**
 * Find a user by id and delete it from the
 * users cache.
 */
void
user_cache_delete(uint32_t uid);

/** Find user by id. */
struct user *
user_cache_find(uint32_t uid);

/* Find a user by name. Used by authentication. */
struct user *
user_by_name(const char *name, uint32_t len);

/**
 * Return the current user.
 *
 * @todo: this doesn't account for the case when a user
 * was dropped, its slot in users array was reused
 * for a new user, and some sessions exist which still
 * use the old auth_token. In this case, already
 * authenticated sessions use grants of the new user,
 * not the old one.
 *
 * This can be easily fixed by checking that uid of the
 * user found by means of auth_token matches the uid
 * stored in the session, and invalidating the session
 * auth_token when it doesn't.
 *
 * Alternatively, one could invalidate the session
 * auth_token whenever sc_version changes. Alternatively,
 * one could invalidate auth_token in all sessions whenever
 * any tuple in _user or _priv spaces is modified.
 *
 * None of these 3 solutions seems to be worth the while
 * at the moment.
 */
#define user()							\
({								\
	struct session *s = fiber()->session;			\
	uint8_t auth_token = s ? s->auth_token : (int) ADMIN;	\
	struct user *u = &users[auth_token];			\
	assert(u->auth_token == auth_token);			\
	u;							\
})

/** Initialize the user cache and access control subsystem. */
void
user_cache_init();

/** Cleanup the user cache and access control subsystem */
void
user_cache_free();

#endif /* INCLUDES_TARANTOOL_BOX_ACCESS_H */
