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

static struct user users[BOX_USER_MAX];
struct user *guest_user = users;
struct user *admin_user = users + 1;

/** Bitmap type for used/unused authentication token map. */
typedef unsigned long user_map_t;

/** A map to quickly look up free slots in users[] array. */
user_map_t user_map[BOX_USER_MAX/(CHAR_BIT*sizeof(user_map_t)) + 1];

int user_map_idx = 0;
struct mh_i32ptr_t *user_registry;

uint8_t
user_map_get_slot()
{
        uint32_t idx = __builtin_ffsl(user_map[user_map_idx]);
        while (idx == 0) {
		if (user_map_idx == sizeof(user_map)/sizeof(*user_map))
			panic("Out of slots for new users");

		user_map_idx++;
                idx = __builtin_ffsl(user_map[user_map_idx]);
        }
        /*
         * find-first-set returns bit index starting from 1,
         * or 0 if no bit is set. Rebase the index to offset 0.
         */
        idx--;
	if (idx == BOX_USER_MAX) {
		/* A cap on the number of users was reached. */
		tnt_raise(LoggedError, ER_USER_MAX, BOX_USER_MAX);
	}
	user_map[user_map_idx] ^= ((user_map_t) 1) << idx;
	idx += user_map_idx * sizeof(*user_map) * CHAR_BIT;
	assert(idx < UINT8_MAX);
	return idx;
}

void
user_map_put_slot(uint8_t auth_token)
{
	memset(users + auth_token, 0, sizeof(struct user_def));
	uint32_t bit_no = auth_token & (sizeof(user_map_t) * CHAR_BIT - 1);
	auth_token /= sizeof(user_map_t) * CHAR_BIT;
	user_map[auth_token] |= ((user_map_t) 1) << bit_no;
	if (auth_token > user_map_idx)
		user_map_idx = auth_token;
}

struct user *
user_cache_replace(struct user_def *def)
{
	struct user *user = user_by_id(def->uid);
	if (user == NULL) {
		uint8_t auth_token = user_map_get_slot();
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
	struct user *old = user_by_id(uid);
	if (old) {
		assert(old->auth_token > ADMIN);
		user_map_put_slot(old->auth_token);
		memset(old, 0, sizeof(*old));
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
	memset(user_map, 0xFF, sizeof(user_map));
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
