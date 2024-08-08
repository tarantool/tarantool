#include "rt_lua_call_access.h"
#include "diag.h"
#include "assoc.h"
#include "user.h"
#include "session.h"
#include "../lua/init.h" /* tarantool_lua_is_builtin_global */

static struct mh_access_t *user_rt_access_cache = NULL;
static struct mh_strnptr_t *user_rt_universe_access_cache = NULL;

const char *
user_name_by_id(uint32_t uid, uint32_t *name_len)
{
	struct user *user = user_find(uid);
	const char *name = user->def->name;
	*name_len = strlen(name);
	return name;
}

void
rt_lua_call_access_init(void)
{
	user_rt_access_cache = mh_access_new();
	user_rt_universe_access_cache = mh_strnptr_new();
}

void
rt_lua_call_access_destroy(void)
{
	mh_access_delete(user_rt_access_cache);
	mh_strnptr_delete(user_rt_universe_access_cache);
}

void
rt_lua_call_access_reset(void)
{
	mh_access_clear(user_rt_access_cache);
	mh_strnptr_clear(user_rt_universe_access_cache);
}

void
grant_rt_access(const char *uname, uint32_t uname_len,
		const char *fname, uint32_t fname_len)
{
	/* Grant universe access. */
	if (is_universe(fname, fname_len)) {
		uint32_t hash = mh_strn_hash(uname, uname_len);
		const struct mh_strnptr_node_t uid_node =
			{ uname, uname_len, hash, NULL };
		struct mh_strnptr_node_t repl_uid_node = { NULL, 0, 0, NULL };
		struct mh_strnptr_node_t *prepl_uid_node = &repl_uid_node;
		mh_strnptr_put(user_rt_universe_access_cache, &uid_node,
			       &prepl_uid_node, NULL);
		return;
	}

	/* Grant access to lua_call function. */
	uint32_t hash = mh_access_hash(uname, uname_len, fname, fname_len);
	const struct mh_access_node_t access_node =
		{ uname, uname_len, fname, fname_len, hash, RT_ACCESS };
	struct mh_access_node_t repl_access_node = { NULL, 0, NULL, 0, 0, 0 };
	struct mh_access_node_t *prepl_access_node = &repl_access_node;
	mh_access_put(user_rt_access_cache, &access_node, &prepl_access_node,
		      NULL);
}

void
revoke_rt_access(const char *uname, uint32_t uname_len,
		 const char *fname, uint32_t fname_len)
{
	if (is_universe(fname, fname_len)) {
		/* Revoke unvirse access. */
		mh_int_t pos = mh_strnptr_find_str(
			user_rt_universe_access_cache, uname, uname_len);
		if (pos != mh_end(user_rt_universe_access_cache))
			mh_strnptr_del(user_rt_universe_access_cache, pos,
				       NULL);
	} else {
		/* Revoke access to lua_call function. */
		uint32_t hash = mh_access_hash(uname, uname_len, fname,
					       fname_len);
		struct mh_access_key_t key =
			{ uname, uname_len, fname, fname_len, hash };
		mh_int_t pos = mh_access_find(user_rt_access_cache, &key, NULL);
		if (pos != mh_end(user_rt_access_cache))
			mh_access_del(user_rt_access_cache, pos, NULL);
	}
}

int
check_rt_access(const char *uname, uint32_t uname_len,
		const char *fname, uint32_t fname_len)
{
	mh_int_t pos = mh_strnptr_find_str(user_rt_universe_access_cache, uname,
					   uname_len);
	/* Check for universe access. Don't allow to call built-ins. */
	if (pos != mh_end(user_rt_universe_access_cache) &&
	    !tarantool_lua_is_builtin_global(fname, fname_len))
		return RT_ACCESS;

	uint32_t hash = mh_access_hash(uname, uname_len, fname, fname_len);
	struct mh_access_key_t key =
		{ uname, uname_len, fname, fname_len, hash };
	pos = mh_access_find(user_rt_access_cache, &key, NULL);
	if (pos == mh_end(user_rt_access_cache))
		return RT_NO_ACCESS;
	return RT_ACCESS;
}
