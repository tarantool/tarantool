#ifndef INCLUDES_TARANTOOL_BOX_USER_CACHE_H
#define INCLUDES_TARANTOOL_BOX_USER_CACHE_H
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

struct user_def;
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
extern struct user_def users[];

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
user_cache_replace(struct user_def *user);

/**
 * Find a user by id and delete it from the
 * users cache.
 */
void
user_cache_delete(uint32_t uid);

/** Find user by id. */
struct user_def *
user_by_id(uint32_t uid);

#define user_by_token(token) (users + token)

/* Find a user by name. Used by authentication. */
struct user_def *
user_cache_find(uint32_t uid);

struct user_def *
user_cache_find_by_name(const char *name, uint32_t len);

/**
 * Return the current user.
 */
#define user()							\
({								\
	struct session *s = session();				\
	struct user_def *u = user_by_token(s->auth_token);	\
	if (u->auth_token != s->auth_token ||			\
	    u->uid != s->uid) {					\
		tnt_raise(ClientError, ER_NO_SUCH_USER,		\
			  int2str(s->uid));			\
	}							\
	u;							\
})

/** Initialize the user cache and access control subsystem. */
void
user_cache_init();

/** Cleanup the user cache and access control subsystem */
void
user_cache_free();

#endif /* INCLUDES_TARANTOOL_BOX_USER_CACHE_H */
