/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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

#include <sys/types.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <small/region.h>

#include "diag.h"
#include "core/popen.h"
#include "core/fiber.h"
#include "core/exception.h"
#include "tarantool_ev.h"

#include "lua/utils.h"
#include "lua/fiber.h"
#include "lua/popen.h"

/* {{{ Constants */

static const char *popen_handle_uname = "popen_handle";
static const char *popen_handle_closed_uname = "popen_handle_closed";

#define POPEN_LUA_READ_BUF_SIZE        4096
#define POPEN_LUA_WAIT_DELAY           0.1
#define POPEN_LUA_ENV_CAPACITY_DEFAULT 256

/**
 * Helper map for transformation between std* popen.new() options
 * and popen backend engine flags.
 */
static const struct {
	/* Name for error messages. */
	const char *option_name;

	unsigned int mask_devnull;
	unsigned int mask_close;
	unsigned int mask_pipe;
} pfd_map[POPEN_FLAG_FD_STDEND_BIT] = {
	{
		.option_name	= "opts.stdin",
		.mask_devnull	= POPEN_FLAG_FD_STDIN_DEVNULL,
		.mask_close	= POPEN_FLAG_FD_STDIN_CLOSE,
		.mask_pipe	= POPEN_FLAG_FD_STDIN,
	}, {
		.option_name	= "opts.stdout",
		.mask_devnull	= POPEN_FLAG_FD_STDOUT_DEVNULL,
		.mask_close	= POPEN_FLAG_FD_STDOUT_CLOSE,
		.mask_pipe	= POPEN_FLAG_FD_STDOUT,
	}, {
		.option_name	= "opts.stderr",
		.mask_devnull	= POPEN_FLAG_FD_STDERR_DEVNULL,
		.mask_close	= POPEN_FLAG_FD_STDERR_CLOSE,
		.mask_pipe	= POPEN_FLAG_FD_STDERR,
	},
};

/* }}} */

/* {{{ Signals */

static const struct {
	const char *signame;
	int signo;
} popen_lua_signals[] = {
#ifdef SIGHUP
	{"SIGHUP", SIGHUP},
#endif
#ifdef SIGINT
	{"SIGINT", SIGINT},
#endif
#ifdef SIGQUIT
	{"SIGQUIT", SIGQUIT},
#endif
#ifdef SIGILL
	{"SIGILL", SIGILL},
#endif
#ifdef SIGTRAP
	{"SIGTRAP", SIGTRAP},
#endif
#ifdef SIGABRT
	{"SIGABRT", SIGABRT},
#endif
#ifdef SIGIOT
	{"SIGIOT", SIGIOT},
#endif
#ifdef SIGBUS
	{"SIGBUS", SIGBUS},
#endif
#ifdef SIGFPE
	{"SIGFPE", SIGFPE},
#endif
#ifdef SIGKILL
	{"SIGKILL", SIGKILL},
#endif
#ifdef SIGUSR1
	{"SIGUSR1", SIGUSR1},
#endif
#ifdef SIGSEGV
	{"SIGSEGV", SIGSEGV},
#endif
#ifdef SIGUSR2
	{"SIGUSR2", SIGUSR2},
#endif
#ifdef SIGPIPE
	{"SIGPIPE", SIGPIPE},
#endif
#ifdef SIGALRM
	{"SIGALRM", SIGALRM},
#endif
#ifdef SIGTERM
	{"SIGTERM", SIGTERM},
#endif
#ifdef SIGSTKFLT
	{"SIGSTKFLT", SIGSTKFLT},
#endif
#ifdef SIGCHLD
	{"SIGCHLD", SIGCHLD},
#endif
#ifdef SIGCONT
	{"SIGCONT", SIGCONT},
#endif
#ifdef SIGSTOP
	{"SIGSTOP", SIGSTOP},
#endif
#ifdef SIGTSTP
	{"SIGTSTP", SIGTSTP},
#endif
#ifdef SIGTTIN
	{"SIGTTIN", SIGTTIN},
#endif
#ifdef SIGTTOU
	{"SIGTTOU", SIGTTOU},
#endif
#ifdef SIGURG
	{"SIGURG", SIGURG},
#endif
#ifdef SIGXCPU
	{"SIGXCPU", SIGXCPU},
#endif
#ifdef SIGXFSZ
	{"SIGXFSZ", SIGXFSZ},
#endif
#ifdef SIGVTALRM
	{"SIGVTALRM", SIGVTALRM},
#endif
#ifdef SIGPROF
	{"SIGPROF", SIGPROF},
#endif
#ifdef SIGWINCH
	{"SIGWINCH", SIGWINCH},
#endif
#ifdef SIGIO
	{"SIGIO", SIGIO},
#endif
#ifdef SIGPOLL
	{"SIGPOLL", SIGPOLL},
#endif
#ifdef SIGPWR
	{"SIGPWR", SIGPWR},
#endif
#ifdef SIGSYS
	{"SIGSYS", SIGSYS},
#endif
	{NULL, 0},
};

/* }}} */

/* {{{ Stream actions */

#define POPEN_LUA_STREAM_INHERIT	"inherit"
#define POPEN_LUA_STREAM_DEVNULL	"devnull"
#define POPEN_LUA_STREAM_CLOSE		"close"
#define POPEN_LUA_STREAM_PIPE		"pipe"

static const struct {
	const char *name;
	const char *value;
	bool devnull;
	bool close;
	bool pipe;
} popen_lua_actions[] = {
	{
		.name		= "INHERIT",
		.value		= POPEN_LUA_STREAM_INHERIT,
		.devnull	= false,
		.close		= false,
		.pipe		= false,
	},
	{
		.name		= "DEVNULL",
		.value		= POPEN_LUA_STREAM_DEVNULL,
		.devnull	= true,
		.close		= false,
		.pipe		= false,
	},
	{
		.name		= "CLOSE",
		.value		= POPEN_LUA_STREAM_CLOSE,
		.devnull	= false,
		.close		= true,
		.pipe		= false,
	},
	{
		.name		= "PIPE",
		.value		= POPEN_LUA_STREAM_PIPE,
		.devnull	= false,
		.close		= false,
		.pipe		= true,
	},
	{NULL, NULL, false, false, false},
};

/* }}} */

/* {{{ Stream status */

#define POPEN_LUA_STREAM_STATUS_OPEN	"open"
#define POPEN_LUA_STREAM_STATUS_CLOSED	"closed"

static const struct {
	const char *name;
	const char *value;
} popen_lua_stream_status[] = {
	{"OPEN",	POPEN_LUA_STREAM_STATUS_OPEN},
	{"CLOSED",	POPEN_LUA_STREAM_STATUS_CLOSED},
	{NULL, NULL},
};

/* }}} */

/* {{{ Process states */

#define POPEN_LUA_STATE_ALIVE "alive"
#define POPEN_LUA_STATE_EXITED "exited"
#define POPEN_LUA_STATE_SIGNALED "signaled"

static const struct {
	const char *name;
	const char *value;
} popen_lua_states[] = {
	{"ALIVE",	POPEN_LUA_STATE_ALIVE},
	{"EXITED",	POPEN_LUA_STATE_EXITED},
	{"SIGNALED",	POPEN_LUA_STATE_SIGNALED},
	{NULL, NULL},
};

/* }}} */

/* {{{ General-purpose Lua helpers */

/**
 * Extract a string from the Lua stack.
 *
 * Return (const char *) for a string, otherwise return NULL.
 *
 * Unlike luaL_tolstring() it accepts only a string and does not
 * accept a number.
 */
static const char *
luaL_tolstring_strict(struct lua_State *L, int idx, size_t *len_ptr)
{
	if (lua_type(L, idx) != LUA_TSTRING)
		return NULL;

	const char *res = lua_tolstring(L, idx, len_ptr);
	assert(res != NULL);
	return res;
}

/**
 * Extract a timeout value from the Lua stack.
 *
 * Return -1.0 when error occurs.
 */
static ev_tstamp
luaT_check_timeout(struct lua_State *L, int idx)
{
	if (lua_type(L, idx) == LUA_TNUMBER)
		return lua_tonumber(L, idx);
	/* FIXME: Support cdata<int64_t> and cdata<uint64_t>. */
	return -1.0;
}

/**
 * Helper for luaT_push_string_noxc().
 */
static int
luaT_push_string_noxc_wrapper(struct lua_State *L)
{
	char *str = (char *)lua_topointer(L, 1);
	size_t len = lua_tointeger(L, 2);
	lua_pushlstring(L, str, len);
	return 1;
}

/**
 * Push a string to the Lua stack.
 *
 * Return 0 at success, -1 at failure and set a diag.
 *
 * Possible errors:
 *
 * - LuajitError ("not enough memory"): no memory space for the
 *   Lua string.
 */
static int
luaT_push_string_noxc(struct lua_State *L, char *str, size_t len)
{
	lua_pushcfunction(L, luaT_push_string_noxc_wrapper);
	lua_pushlightuserdata(L, str);
	lua_pushinteger(L, len);
	return luaT_call(L, 2, 1);
}

/* }}} */

/* {{{ Popen handle userdata manipulations */

/**
 * Extract popen handle from the Lua stack.
 *
 * Return NULL in case of unexpected type.
 */
static struct popen_handle *
luaT_check_popen_handle(struct lua_State *L, int idx, bool *is_closed_ptr)
{
	struct popen_handle **handle_ptr =
		luaL_testudata(L, idx, popen_handle_uname);
	bool is_closed = false;

	if (handle_ptr == NULL) {
		handle_ptr = luaL_testudata(L, idx, popen_handle_closed_uname);
		is_closed = true;
	}

	if (handle_ptr == NULL)
		return NULL;
	assert(*handle_ptr != NULL);

	if (is_closed_ptr != NULL)
		*is_closed_ptr = is_closed;
	return *handle_ptr;
}

/**
 * Push popen handle into the Lua stack.
 *
 * Return 1 -- amount of pushed values.
 */
static int
luaT_push_popen_handle(struct lua_State *L, struct popen_handle *handle)
{
	*(struct popen_handle **)lua_newuserdata(L, sizeof(handle)) = handle;
	luaL_getmetatable(L, popen_handle_uname);
	lua_setmetatable(L, -2);
	return 1;
}

/**
 * Mark popen handle as closed.
 *
 * Does not perform any checks whether @a idx points
 * to a popen handle.
 *
 * The closed state is needed primarily to protect a
 * handle from double freeing.
 */
static void
luaT_mark_popen_handle_closed(struct lua_State *L, int idx)
{
	luaL_getmetatable(L, popen_handle_closed_uname);
	lua_setmetatable(L, idx);
}

/* }}} */

/* {{{ Push popen handle info to the Lua stack */

/**
 * Convert ...FD_STD* flags to a popen.opts.<...> constant.
 *
 * If flags are invalid, push 'invalid' string.
 *
 * Push the result onto the Lua stack.
 */
static int
luaT_push_popen_stdX_action(struct lua_State *L, int fd, unsigned int flags)
{
	for (size_t i = 0; popen_lua_actions[i].name != NULL; ++i) {
		bool devnull	= (flags & pfd_map[fd].mask_devnull) != 0;
		bool close	= (flags & pfd_map[fd].mask_close) != 0;
		bool pipe	= (flags & pfd_map[fd].mask_pipe) != 0;

		if (devnull == popen_lua_actions[i].devnull &&
		    close   == popen_lua_actions[i].close &&
		    pipe    == popen_lua_actions[i].pipe) {
			lua_pushstring(L, popen_lua_actions[i].value);
			return 1;
		}
	}

	lua_pushliteral(L, "invalid");
	return 1;
}

/**
 * Push a piped stream status (open or closed) to the Lua stack.
 */
static int
luaT_push_popen_stdX_status(struct lua_State *L, struct popen_handle *handle,
			    int idx)
{
	if ((handle->flags & pfd_map[idx].mask_pipe) == 0) {
		/* Stream action: INHERIT, DEVNULL or CLOSE. */
		lua_pushnil(L);
		return 1;
	}

	/* Stream action: PIPE. */
	if (handle->ios[idx].fd < 0)
		lua_pushliteral(L, POPEN_LUA_STREAM_STATUS_CLOSED);
	else
		lua_pushliteral(L, POPEN_LUA_STREAM_STATUS_OPEN);

	return 1;
}

/**
 * Push popen options as a Lua table.
 *
 * Environment variables are not stored in a popen handle and so
 * missed here.
 */
static int
luaT_push_popen_opts(struct lua_State *L, unsigned int flags)
{
	lua_createtable(L, 0, 8);

	/*
	 * FIXME: Loop over a static array of stdX options.
	 *
	 * static const struct {
	 *	const char *option_name;
	 *	int fd;
	 * } popen_lua_stdX_options = {
	 *	{"stdin",	STDIN_FILENO	},
	 *	{"stdout",	STDOUT_FILENO	},
	 *	{"stderr",	STDERR_FILENO	},
	 *	{NULL,		-1		},
	 * };
	 */

	luaT_push_popen_stdX_action(L, STDIN_FILENO, flags);
	lua_setfield(L, -2, "stdin");

	luaT_push_popen_stdX_action(L, STDOUT_FILENO, flags);
	lua_setfield(L, -2, "stdout");

	luaT_push_popen_stdX_action(L, STDERR_FILENO, flags);
	lua_setfield(L, -2, "stderr");

	/* env is skipped */

	/* FIXME: Loop over a static array of boolean options. */

	lua_pushboolean(L, (flags & POPEN_FLAG_SHELL) != 0);
	lua_setfield(L, -2, "shell");

	lua_pushboolean(L, (flags & POPEN_FLAG_SETSID) != 0);
	lua_setfield(L, -2, "setsid");

	lua_pushboolean(L, (flags & POPEN_FLAG_CLOSE_FDS) != 0);
	lua_setfield(L, -2, "close_fds");

	lua_pushboolean(L, (flags & POPEN_FLAG_RESTORE_SIGNALS) != 0);
	lua_setfield(L, -2, "restore_signals");

	lua_pushboolean(L, (flags & POPEN_FLAG_GROUP_SIGNAL) != 0);
	lua_setfield(L, -2, "group_signal");

	lua_pushboolean(L, (flags & POPEN_FLAG_KEEP_CHILD) != 0);
	lua_setfield(L, -2, "keep_child");

	return 1;
}

/**
 * Push a process status to the Lua stack as a table.
 *
 * The format of the resulting table:
 *
 *     {
 *         state = one-of(
 *             popen.state.ALIVE    (== 'alive'),
 *             popen.state.EXITED   (== 'exited'),
 *             popen.state.SIGNALED (== 'signaled'),
 *         )
 *
 *         -- Present when `state` is 'exited'.
 *         exit_code = <number>,
 *
 *         -- Present when `state` is 'signaled'.
 *         signo = <number>,
 *         signame = <string>,
 *     }
 *
 * @param state POPEN_STATE_{ALIVE,EXITED,SIGNALED}
 *
 * @param exit_code is exit code when the process is exited and a
 * signal number when a process is signaled.
 *
 * @see enum popen_states
 * @see popen_state()
 */
static int
luaT_push_popen_process_status(struct lua_State *L, int state, int exit_code)
{
	lua_createtable(L, 0, 3);

	switch (state) {
	case POPEN_STATE_ALIVE:
		lua_pushliteral(L, POPEN_LUA_STATE_ALIVE);
		lua_setfield(L, -2, "state");
		break;
	case POPEN_STATE_EXITED:
		lua_pushliteral(L, POPEN_LUA_STATE_EXITED);
		lua_setfield(L, -2, "state");
		lua_pushinteger(L, exit_code);
		lua_setfield(L, -2, "exit_code");
		break;
	case POPEN_STATE_SIGNALED:
		lua_pushliteral(L, POPEN_LUA_STATE_SIGNALED);
		lua_setfield(L, -2, "state");
		lua_pushinteger(L, exit_code);
		lua_setfield(L, -2, "signo");

		/*
		 * FIXME: Preallocate signo -> signal name
		 * mapping.
		 */
		const char *signame = "unknown";
		for (int i = 0; popen_lua_signals[i].signame != NULL; ++i) {
			if (popen_lua_signals[i].signo == exit_code)
				signame = popen_lua_signals[i].signame;
		}
		lua_pushstring(L, signame);
		lua_setfield(L, -2, "signame");

		break;
	default:
		unreachable();
	}

	return 1;
}

/* }}} */

/* {{{ Errors */

/**
 * Raise IllegalParams error re closed popen handle.
 */
static int
luaT_popen_handle_closed_error(struct lua_State *L)
{
	diag_set(IllegalParams, "popen: attempt to operate on a closed handle");
	return luaT_error(L);
}

/**
 * Raise IllegalParams error re wrong parameter.
 */
static int
luaT_popen_param_value_error(struct lua_State *L, const char *got,
			     const char *func_name, const char *param,
			     const char *exp)
{
	static const char *fmt =
		"%s: wrong parameter \"%s\": expected %s, got %s";
	diag_set(IllegalParams, fmt, func_name, param, exp, got);
	return luaT_error(L);
}

/**
 * Raise IllegalParams error re wrong parameter type.
 */
static int
luaT_popen_param_type_error(struct lua_State *L, int idx, const char *func_name,
			    const char *param, const char *exp)
{
	const char *typename = idx == 0 ?
		"<unknown>" : lua_typename(L, lua_type(L, idx));
	static const char *fmt =
		"%s: wrong parameter \"%s\": expected %s, got %s";
	diag_set(IllegalParams, fmt, func_name, param, exp, typename);
	return luaT_error(L);
}

/**
 * Raise IllegalParams error re wrong parameter type in an array.
 */
static int
luaT_popen_array_elem_type_error(struct lua_State *L, int idx,
				 const char *func_name, const char *param,
				 int num, const char *exp)
{
	const char *typename = idx == 0 ?
		"<unknown>" : lua_typename(L, lua_type(L, idx));
	static const char *fmt =
		"%s: wrong parameter \"%s[%d]\": expected %s, got %s";
	diag_set(IllegalParams, fmt, func_name, param, num, exp, typename);
	return luaT_error(L);
}

/* }}} */

/* {{{ Parameter parsing */

/**
 * Parse popen.new() "opts.{stdin,stdout,stderr}" parameter.
 *
 * Result: @a flags_p is updated.
 *
 * Raise an error in case of the incorrect parameter.
 */
static void
luaT_popen_parse_stdX(struct lua_State *L, int idx, int fd,
		      unsigned int *flags_p)
{
	const char *action;
	size_t action_len;
	if ((action = luaL_tolstring_strict(L, idx, &action_len)) == NULL)
		luaT_popen_param_type_error(L, idx, "popen.new",
					    pfd_map[fd].option_name,
					    "string or nil");

	unsigned int flags = *flags_p;

	/* See popen_lua_actions. */
	if (strncmp(action, POPEN_LUA_STREAM_INHERIT, action_len) == 0) {
		flags &= ~pfd_map[fd].mask_devnull;
		flags &= ~pfd_map[fd].mask_close;
		flags &= ~pfd_map[fd].mask_pipe;
	} else if (strncmp(action, POPEN_LUA_STREAM_DEVNULL, action_len) == 0) {
		flags |= pfd_map[fd].mask_devnull;
		flags &= ~pfd_map[fd].mask_close;
		flags &= ~pfd_map[fd].mask_pipe;
	} else if (strncmp(action, POPEN_LUA_STREAM_CLOSE, action_len) == 0) {
		flags &= ~pfd_map[fd].mask_devnull;
		flags |= pfd_map[fd].mask_close;
		flags &= ~pfd_map[fd].mask_pipe;
	} else if (strncmp(action, POPEN_LUA_STREAM_PIPE, action_len) == 0) {
		flags &= ~pfd_map[fd].mask_devnull;
		flags &= ~pfd_map[fd].mask_close;
		flags |= pfd_map[fd].mask_pipe;
	} else {
		luaT_popen_param_value_error(L, action, "popen.new",
					     pfd_map[fd].option_name,
					     "popen.opts.<...> constant");
		unreachable();
	}

	*flags_p = flags;
}

/**
 * Glue key and value on the Lua stack into "key=value" entry.
 *
 * Raise an error in case of the incorrect parameter.
 *
 * Return NULL in case of an allocation error and set a diag
 * (OutOfMemory).
 */
static char *
luaT_popen_parse_env_entry(struct lua_State *L, int key_idx, int value_idx,
			   struct region *region)
{
	size_t key_len;
	size_t value_len;
	const char *key = luaL_tolstring_strict(L, key_idx, &key_len);
	const char *value = luaL_tolstring_strict(L, value_idx, &value_len);
	if (key == NULL || value == NULL) {
		luaT_popen_param_value_error(L, "a non-string key or value",
					     "popen.new", "opts.env",
					     "{[<string>] = <string>, ...}");
		unreachable();
		return NULL;
	}

	/*
	 * FIXME: Don't sure, but maybe it would be right to
	 * validate key and value here against '=', '\0' and
	 * maybe other symbols.
	 */

	/* entry = "${key}=${value}" */
	size_t entry_size = key_len + value_len + 2;
	char *entry = region_alloc(region, entry_size);
	if (entry == NULL) {
		diag_set(OutOfMemory, entry_size, "region_alloc", "env entry");
		return NULL;
	}
	memcpy(entry, key, key_len);
	size_t pos = key_len;
	entry[pos++] = '=';
	memcpy(entry + pos, value, value_len);
	pos += value_len;
	entry[pos++] = '\0';
	assert(pos == entry_size);

	return entry;
}

/**
 * Parse popen.new() "opts.env" parameter.
 *
 * Return a new array in the `extern char **environ` format
 * (NULL terminated array of "foo=bar" strings).
 *
 * Strings and array of them are allocated on the provided
 * region. A caller should call region_used() before invoking
 * this function and call region_truncate() when the result
 * is not needed anymore. Alternatively a caller may assume
 * that fiber_gc() will collect this memory eventually, but
 * it is recommended to do so only for rare paths.
 *
 * Raise an error in case of the incorrect parameter.
 *
 * Return NULL in case of an allocation error and set a diag
 * (OutOfMemory).
 */
static char **
luaT_popen_parse_env(struct lua_State *L, int idx, struct region *region)
{
	if (lua_type(L, idx) != LUA_TTABLE) {
		luaT_popen_param_type_error(L, idx, "popen.new", "opts.env",
					    "table or nil");
		unreachable();
		return NULL;
	}

	/* Calculate absolute value in the stack. */
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;

	size_t capacity = POPEN_LUA_ENV_CAPACITY_DEFAULT;
	size_t region_svp = region_used(region);
	size_t size;
	char **env = region_alloc_array(region, typeof(env[0]), capacity,
					&size);
	if (env == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "env");
		return NULL;
	}
	size_t nr_env = 0;

	bool only_count = false;

	/*
	 * Traverse over the table and fill `env` array. If
	 * default `env` capacity is not enough, discard
	 * everything, but continue iterating to count entries.
	 */
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		/*
		 * Can we store the next entry and trailing NULL?
		 */
		if (nr_env >= capacity - 1) {
			env = NULL;
			region_truncate(region, region_svp);
			only_count = true;
		}
		if (only_count) {
			++nr_env;
			lua_pop(L, 1);
			continue;
		}
		char *entry = luaT_popen_parse_env_entry(L, -2, -1, region);
		if (entry == NULL) {
			region_truncate(region, region_svp);
			return NULL;
		}
		env[nr_env++] = entry;
		lua_pop(L, 1);
	}

	if (! only_count) {
		assert(nr_env < capacity);
		env[nr_env] = NULL;
		return env;
	}

	/*
	 * Now we know exact amount of elements. Run
	 * the traverse again and fill `env` array.
	 */
	capacity = nr_env + 1;
	env = region_alloc_array(region, typeof(env[0]), capacity, &size);
	if (env == NULL) {
		region_truncate(region, region_svp);
		diag_set(OutOfMemory, size, "region_alloc_array", "env");
		return NULL;
	}
	nr_env = 0;
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		char *entry = luaT_popen_parse_env_entry(L, -2, -1, region);
		if (entry == NULL) {
			region_truncate(region, region_svp);
			return NULL;
		}
		assert(nr_env < capacity - 1);
		env[nr_env++] = entry;
		lua_pop(L, 1);
	}
	assert(nr_env == capacity - 1);
	env[nr_env] = NULL;
	return env;
}

/**
 * Parse popen.new() "opts" parameter.
 *
 * Prerequisite: @a opts should be zero filled.
 *
 * Result: @a opts structure is filled.
 *
 * Raise an error in case of the incorrect parameter.
 *
 * Return 0 at success. Allocates opts->env on @a region if
 * needed. @see luaT_popen_parse_env() for details how to
 * free it.
 *
 * Return -1 in case of an allocation error and set a diag
 * (OutOfMemory).
 */
static int
luaT_popen_parse_opts(struct lua_State *L, int idx, struct popen_opts *opts,
		      struct region *region)
{
	/*
	 * Default flags: inherit std*, close other fds,
	 * restore signals.
	 */
	opts->flags = POPEN_FLAG_NONE		|
		POPEN_FLAG_CLOSE_FDS		|
		POPEN_FLAG_RESTORE_SIGNALS;

	/* Parse options. */
	if (lua_type(L, idx) == LUA_TTABLE) {
		/*
		 * FIXME: Loop over a static array of stdX
		 * options.
		 */

		lua_getfield(L, idx, "stdin");
		if (! lua_isnil(L, -1)) {
			luaT_popen_parse_stdX(L, -1, STDIN_FILENO,
					      &opts->flags);
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "stdout");
		if (! lua_isnil(L, -1))
			luaT_popen_parse_stdX(L, -1, STDOUT_FILENO,
					      &opts->flags);
		lua_pop(L, 1);

		lua_getfield(L, idx, "stderr");
		if (! lua_isnil(L, -1))
			luaT_popen_parse_stdX(L, -1, STDERR_FILENO,
					      &opts->flags);
		lua_pop(L, 1);

		lua_getfield(L, idx, "env");
		if (! lua_isnil(L, -1)) {
			opts->env = luaT_popen_parse_env(L, -1, region);
			if (opts->env == NULL)
				return -1;
		}
		lua_pop(L, 1);

		/*
		 * FIXME: Loop over a static array of boolean
		 * options.
		 */

		lua_getfield(L, idx, "shell");
		if (! lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				luaT_popen_param_type_error(L, -1, "popen.new",
							    "opts.shell",
							    "boolean or nil");
			if (lua_toboolean(L, -1) == 0)
				opts->flags &= ~POPEN_FLAG_SHELL;
			else
				opts->flags |= POPEN_FLAG_SHELL;
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "setsid");
		if (! lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				luaT_popen_param_type_error(L, -1, "popen.new",
							    "opts.setsid",
							    "boolean or nil");
			if (lua_toboolean(L, -1) == 0)
				opts->flags &= ~POPEN_FLAG_SETSID;
			else
				opts->flags |= POPEN_FLAG_SETSID;
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "close_fds");
		if (! lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				luaT_popen_param_type_error(L, -1, "popen.new",
							    "opts.close_fds",
							    "boolean or nil");
			if (lua_toboolean(L, -1) == 0)
				opts->flags &= ~POPEN_FLAG_CLOSE_FDS;
			else
				opts->flags |= POPEN_FLAG_CLOSE_FDS;
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "restore_signals");
		if (! lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				luaT_popen_param_type_error(
					L, -1, "popen.new",
					"opts.restore_signals",
					"boolean or nil");
			if (lua_toboolean(L, -1) == 0)
				opts->flags &= ~POPEN_FLAG_RESTORE_SIGNALS;
			else
				opts->flags |= POPEN_FLAG_RESTORE_SIGNALS;
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "group_signal");
		if (! lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				luaT_popen_param_type_error(L, -1, "popen.new",
							    "opts.group_signal",
							    "boolean or nil");
			if (lua_toboolean(L, -1) == 0)
				opts->flags &= ~POPEN_FLAG_GROUP_SIGNAL;
			else
				opts->flags |= POPEN_FLAG_GROUP_SIGNAL;
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "keep_child");
		if (! lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				luaT_popen_param_type_error(L, -1, "popen.new",
							    "opts.keep_child",
							    "boolean or nil");
			if (lua_toboolean(L, -1) == 0)
				opts->flags &= ~POPEN_FLAG_KEEP_CHILD;
			else
				opts->flags |= POPEN_FLAG_KEEP_CHILD;
		}
		lua_pop(L, 1);
	}

	return 0;
}

/**
 * Parse popen.new() "argv" parameter.
 *
 * Prerequisite: opts->flags & POPEN_FLAG_SHELL should be
 * the same in a call of this function and when paired
 * popen_new() is invoked.
 *
 * Raise an error in case of the incorrect parameter.
 *
 * Return 0 at success. Sets opts->argv and opts->nr_argv.
 * Allocates opts->argv on @a region (@see
 * luaT_popen_parse_env() for details how to free it).
 *
 * Return -1 in case of an allocation error and set a diag
 * (OutOfMemory).
 */
static int
luaT_popen_parse_argv(struct lua_State *L, int idx, struct popen_opts *opts,
		      struct region *region)
{
	size_t region_svp = region_used(region);

	/*
	 * We need to know exact size of the array to allocate
	 * a memory for opts->argv without reallocations.
	 *
	 * lua_objlen() does not guarantee that there are no
	 * holes in the array, but we check it within a loop
	 * later.
	 */
	size_t argv_len = lua_objlen(L, idx);

	/* ["sh", "-c", ]..., NULL. */
	opts->nr_argv = argv_len + 1;
	if (opts->flags & POPEN_FLAG_SHELL)
		opts->nr_argv += 2;

	size_t size;
	opts->argv = region_alloc_array(region, typeof(opts->argv[0]),
					opts->nr_argv, &size);
	if (opts->argv == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "opts->argv");
		return -1;
	}

	/* Keep place for "sh", "-c" as popen_new() expects. */
	const char **to = (const char **)opts->argv;
	if (opts->flags & POPEN_FLAG_SHELL) {
		opts->argv[0] = NULL;
		opts->argv[1] = NULL;
		to += 2;
	}

	for (size_t i = 0; i < argv_len; ++i) {
		lua_rawgeti(L, idx, i + 1);
		const char *arg = luaL_tolstring_strict(L, -1, NULL);
		if (arg == NULL) {
			region_truncate(region, region_svp);
			return luaT_popen_array_elem_type_error(
				L, -1, "popen.new", "argv", i + 1, "string");
		}
		*to++ = arg;
		lua_pop(L, 1);
	}
	*to++ = NULL;
	assert((const char **)opts->argv + opts->nr_argv == to);

	return 0;
}

/**
 * Parse popen.shell() "mode" parameter.
 *
 * Convert "mode" parameter into options table for popen.new().
 * Push the table to the Lua stack.
 *
 * Raise an error in case of the incorrect parameter.
 */
static void
luaT_popen_parse_mode(struct lua_State *L, int idx)
{
	if (lua_type(L, idx) != LUA_TSTRING &&
	    lua_type(L, idx) != LUA_TNONE &&
	    lua_type(L, idx) != LUA_TNIL)
		luaT_popen_param_type_error(L, idx, "popen.shell", "mode",
					    "string or nil");

	/*
	 * Create options table for popen.new().
	 *
	 * Preallocate space for shell, setsid, group_signal and
	 * std{in,out,err} options.
	 */
	lua_createtable(L, 0, 5);

	lua_pushboolean(L, true);
	lua_setfield(L, -2, "shell");

	lua_pushboolean(L, true);
	lua_setfield(L, -2, "setsid");

	lua_pushboolean(L, true);
	lua_setfield(L, -2, "group_signal");

	/*
	 * When mode is nil, left std* params default, which means
	 * to inherit parent's file descriptors in a child
	 * process.
	 */
	if (lua_isnoneornil(L, idx))
		return;

	size_t mode_len;
	const char *mode = lua_tolstring(L, idx, &mode_len);
	for (size_t i = 0; i < mode_len; ++i) {
		switch (mode[i]) {
		case 'r':
			lua_pushstring(L, POPEN_LUA_STREAM_PIPE);
			lua_setfield(L, -2, "stdout");
			break;
		case 'R':
			lua_pushstring(L, POPEN_LUA_STREAM_PIPE);
			lua_setfield(L, -2, "stderr");
			break;
		case 'w':
			lua_pushstring(L, POPEN_LUA_STREAM_PIPE);
			lua_setfield(L, -2, "stdin");
			break;
		default:
			luaT_popen_param_value_error(
				L, mode, "popen.shell", "mode",
				"'r', 'w', 'R' or its combination");
		}
	}
}

/* }}} */

/* {{{ Lua API functions and methods */

/**
 * Execute a child program in a new process.
 *
 * @param argv  an array of a program to run with
 *              command line options, mandatory;
 *              absolute path to the program is required
 *              when @a opts.shell is false (default)
 *
 * @param opts  table of options
 *
 * @param opts.stdin   action on STDIN_FILENO
 * @param opts.stdout  action on STDOUT_FILENO
 * @param opts.stderr  action on STDERR_FILENO
 *
 * File descriptor actions:
 *
 *     popen.opts.INHERIT  (== 'inherit') [default]
 *                         inherit the fd from the parent
 *     popen.opts.DEVNULL  (== 'devnull')
 *                         open /dev/null on the fd
 *     popen.opts.CLOSE    (== 'close')
 *                         close the fd
 *     popen.opts.PIPE     (== 'pipe')
 *                         feed data from/to the fd to parent
 *                         using a pipe
 *
 * @param opts.env  a table of environment variables to
 *                  be used inside a process; key is a
 *                  variable name, value is a variable
 *                  value.
 *                  - when is not set then the current
 *                    environment is inherited;
 *                  - if set to an empty table then the
 *                    environment will be dropped
 *                  - if set then the environment will be
 *                    replaced
 *
 * @param opts.shell            (boolean, default: false)
 *        true                  run a child process via
 *                              'sh -c "${opts.argv}"'
 *        false                 call the executable directly
 *
 * @param opts.setsid           (boolean, default: false)
 *        true                  run the program in a new
 *                              session
 *        false                 run the program in the
 *                              tarantool instance's
 *                              session and process group
 *
 * @param opts.close_fds        (boolean, default: true)
 *        true                  close all inherited fds from a
 *                              parent
 *        false                 don't do that
 *
 * @param opts.restore_signals  (boolean, default: true)
 *        true                  reset all signal actions
 *                              modified in parent's process
 *        false                 inherit changed actions
 *
 * @param opts.group_signal     (boolean, default: false)
 *        true                  send signal to a child process
 *                              group (only when opts.setsid is
 *                              enabled)
 *        false                 send signal to a child process
 *                              only
 *
 * @param opts.keep_child       (boolean, default: false)
 *        true                  don't send SIGKILL to a child
 *                              process at freeing (by :close()
 *                              or Lua GC)
 *        false                 send SIGKILL to a child process
 *                              (or a process group if
 *                              opts.group_signal is enabled) at
 *                              :close() or collecting of the
 *                              handle by Lua GC
 *
 * The returned handle provides :close() method to explicitly
 * release all occupied resources (including the child process
 * itself if @a opts.keep_child is not set). However if the
 * method is not called for a handle during its lifetime, the
 * same freeing actions will be triggered by Lua GC.
 *
 * It is recommended to use opts.setsid + opts.group_signal
 * if a child process may spawn its own childs and they all
 * should be killed together.
 *
 * Note: A signal will not be sent if the child process is
 * already dead: otherwise we might kill another process that
 * occupies the same PID later. This means that if the child
 * process dies before its own childs, the function will not
 * send a signal to the process group even when opts.setsid and
 * opts.group_signal are set.
 *
 * Use os.environ() to pass copy of current environment with
 * several replacements (see example 2 below).
 *
 * Raise an error on incorrect parameters:
 *
 * - IllegalParams: incorrect type or value of a parameter.
 * - IllegalParams: group signal is set, while setsid is not.
 *
 * Return a popen handle on success.
 *
 * Return `nil, err` on a failure. Possible reasons:
 *
 * - SystemError: dup(), fcntl(), pipe(), vfork() or close()
 *                fails in the parent process.
 * - SystemError: (temporary restriction) the parent process
 *                has closed stdin, stdout or stderr.
 * - OutOfMemory: unable to allocate the handle or a temporary
 *                buffer.
 *
 * Example 1:
 *
 *  | local popen = require('popen')
 *  |
 *  | local ph = popen.new({'/bin/date'}, {
 *  |     stdout = popen.opts.PIPE,
 *  | })
 *  | local date = ph:read():rstrip()
 *  | ph:close()
 *  | print(date) -- Thu 16 Apr 2020 01:40:56 AM MSK
 *
 * Execute 'date' command, read the result and close the
 * popen object.
 *
 * Example 2:
 *
 *  | local popen = require('popen')
 *  |
 *  | local env = os.environ()
 *  | env['FOO'] = 'bar'
 *  |
 *  | local ph = popen.new({'echo "${FOO}"'}, {
 *  |     stdout = popen.opts.PIPE,
 *  |     shell = true,
 *  |     env = env,
 *  | })
 *  | local res = ph:read():rstrip()
 *  | ph:close()
 *  | print(res) -- bar
 *
 * It is quite similar to the previous one, but sets the
 * environment variable and uses shell builtin 'echo' to
 * show it.
 *
 * Example 3:
 *
 *  | local popen = require('popen')
 *  |
 *  | local ph = popen.new({'echo hello >&2'}, { -- !!
 *  |     stderr = popen.opts.PIPE,              -- !!
 *  |     shell = true,
 *  | })
 *  | local res = ph:read({stderr = true}):rstrip()
 *  | ph:close()
 *  | print(res) -- hello
 *
 * This example demonstrates how to capture child's stderr.
 *
 * Example 4:
 *
 *  | local function call_jq(input, filter)
 *  |     -- Start jq process, connect to stdin, stdout and stderr.
 *  |     local jq_argv = {'/usr/bin/jq', '-M', '--unbuffered', filter}
 *  |     local ph, err = popen.new(jq_argv, {
 *  |         stdin = popen.opts.PIPE,
 *  |         stdout = popen.opts.PIPE,
 *  |         stderr = popen.opts.PIPE,
 *  |     })
 *  |     if ph == nil then return nil, err end
 *  |
 *  |     -- Write input data to child's stdin and send EOF.
 *  |     local ok, err = ph:write(input)
 *  |     if not ok then return nil, err end
 *  |     ph:shutdown({stdin = true})
 *  |
 *  |     -- Read everything until EOF.
 *  |     local chunks = {}
 *  |     while true do
 *  |         local chunk, err = ph:read()
 *  |         if chunk == nil then
 *  |             ph:close()
 *  |             return nil, err
 *  |         end
 *  |         if chunk == '' then break end -- EOF
 *  |         table.insert(chunks, chunk)
 *  |     end
 *  |
 *  |     -- Read diagnostics from stderr if any.
 *  |     local err = ph:read({stderr = true})
 *  |     if err ~= '' then
 *  |         ph:close()
 *  |         return nil, err
 *  |     end
 *  |
 *  |     -- Glue all chunks, strip trailing newline.
 *  |     return table.concat(chunks):rstrip()
 *  | end
 *
 * Demonstrates how to run a stream program (like `grep`, `sed`
 * and so), write to its stdin and read from its stdout.
 *
 * The example assumes that input data are small enough to fit
 * a pipe buffer (typically 64 KiB, but depends on a platform
 * and its configuration). It will stuck in :write() for large
 * data. How to handle this case: call :read() in a loop in
 * another fiber (start it before a first :write()).
 *
 * If a process writes large text to stderr, it may fill out
 * stderr pipe buffer and stuck in write(2, ...). So we need
 * to read stderr in a separate fiber to handle this case.
 */
static int
lbox_popen_new(struct lua_State *L)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	if (lua_type(L, 1) != LUA_TTABLE)
		return luaT_popen_param_type_error(L, 1, "popen.new", "argv",
						   "table");
	else if (lua_type(L, 2) != LUA_TTABLE &&
		 lua_type(L, 2) != LUA_TNONE &&
		 lua_type(L, 2) != LUA_TNIL)
		return luaT_popen_param_type_error(L, 2, "popen.new", "opts",
						   "table or nil");

	/* Parse opts and argv. */
	struct popen_opts opts = { .argv = NULL, };
	int rc = luaT_popen_parse_opts(L, 2, &opts, region);
	if (rc != 0)
		goto err;
	rc = luaT_popen_parse_argv(L, 1, &opts, region);
	if (rc != 0)
		goto err;

	struct popen_handle *handle = popen_new(&opts);

	if (handle == NULL)
		goto err;

	region_truncate(region, region_svp);
	luaT_push_popen_handle(L, handle);
	return 1;

err:
	region_truncate(region, region_svp);
	struct error *e = diag_last_error(diag_get());
	if (e->type == &type_IllegalParams)
		return luaT_error(L);
	return luaT_push_nil_and_error(L);
}

/**
 * Execute a shell command.
 *
 * @param command  a command to run, mandatory
 * @param mode     communication mode, optional
 *                 'w'    to use ph:write()
 *                 'r'    to use ph:read()
 *                 'R'    to use ph:read({stderr = true})
 *                 nil    inherit parent's std* file descriptors
 *
 * Several mode characters can be set together: 'rw', 'rRw', etc.
 *
 * This function is just shortcut for popen.new({command}, opts)
 * with opts.{shell,setsid,group_signal} set to `true` and
 * and opts.{stdin,stdout,stderr} set based on `mode` parameter.
 *
 * All std* streams are inherited from parent by default if it is
 * not changed using mode: 'r' for stdout, 'R' for stderr, 'w' for
 * stdin.
 *
 * Raise an error on incorrect parameters:
 *
 * - IllegalParams: incorrect type or value of a parameter.
 *
 * Return a popen handle on success.
 *
 * Return `nil, err` on a failure.
 * @see lbox_popen_new() for possible reasons.
 *
 * Example:
 *
 *  | local popen = require('popen')
 *  |
 *  | -- Run the program and save its handle.
 *  | local ph = popen.shell('date', 'r')
 *  |
 *  | -- Read program's output, strip trailing newline.
 *  | local date = ph:read():rstrip()
 *  |
 *  | -- Free resources. The process is killed (but 'date'
 *  | -- exits itself anyway).
 *  | ph:close()
 *  |
 *  | print(date)
 *
 * Execute 'sh -c date' command, read the output and close the
 * popen object.
 *
 * Unix defines a text file as a sequence of lines, each ends
 * with the newline symbol. The same convention is usually
 * applied for a text output of a command (so when it is
 * redirected to a file, the file will be correct).
 *
 * However internally an application usually operates on
 * strings, which are NOT newline terminated (e.g. literals
 * for error messages). The newline is usually added right
 * before a string is written to the outside world (stdout,
 * console or log). :rstrip() in the example above is shown
 * for this sake.
 */
static int
lbox_popen_shell(struct lua_State *L)
{
	if (lua_type(L, 1) != LUA_TSTRING)
		return luaT_popen_param_type_error(L, 1, "popen.shell",
						   "command", "string");

	/*
	 * Ensure that at least two stack slots are occupied.
	 *
	 * Otherwise we can pass `top` as `idx` to lua_replace().
	 * lua_replace() on `top` index copies a value to itself
	 * first and then pops it from the stack.
	 */
	if (lua_gettop(L) == 1)
		lua_pushnil(L);

	/* Create argv table for popen.new(). */
	lua_createtable(L, 1, 0);
	/* argv[1] = command */
	lua_pushvalue(L, 1);
	lua_rawseti(L, -2, 1);
	/* {...}[1] == argv */
	lua_replace(L, 1);

	/* opts = parse_mode(mode) */
	luaT_popen_parse_mode(L, 2);
	/* {...}[2] == opts */
	lua_replace(L, 2);

	return lbox_popen_new(L);
}

/**
 * Send signal to a child process.
 *
 * @param handle  a handle carries child process to be signaled
 * @param signo   signal number to send
 *
 * When opts.setsid and opts.group_signal are set on the handle
 * the signal is sent to the process group rather than to the
 * process. @see lbox_popen_new() for details about group
 * signaling.
 *
 * Note: The module offers popen.signal.SIG* constants, because
 * some signals have different numbers on different platforms.
 *
 * Note: Mac OS may don't deliver a signal to a process in a
 * group when opts.setsid and opts.group_signal are set. It
 * seems there is a race here: when a process is just forked it
 * may be not signaled.
 *
 * Raise an error on incorrect parameters:
 *
 * - IllegalParams:    an incorrect handle parameter.
 * - IllegalParams:    called on a closed handle.
 *
 * Return `true` if signal is sent.
 *
 * Return `nil, err` on a failure. Possible reasons:
 *
 * - SystemError: a process does not exists anymore
 *
 *                Aside of a non-exist process it is also
 *                returned for a zombie process or when all
 *                processes in a group are zombies (but
 *                see note re Mac OS below).
 *
 * - SystemError: invalid signal number
 *
 * - SystemError: no permission to send a signal to
 *                a process or a process group
 *
 *                It is returned on Mac OS when a signal is
 *                sent to a process group, where a group leader
 *                is zombie (or when all processes in it
 *                are zombies, don't sure).
 *
 *                Whether it may appear due to other
 *                reasons is unclear.
 */
static int
lbox_popen_signal(struct lua_State *L)
{
	/*
	 * FIXME: Extracting a handle and raising an error when
	 * it is closed is repeating pattern within the file. It
	 * worth to extract it to a function.
	 */
	struct popen_handle *handle;
	bool is_closed;
	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL ||
	    !lua_isnumber(L, 2)) {
		diag_set(IllegalParams, "Bad params, use: ph:signal(signo)");
		return luaT_error(L);
	}
	if (is_closed)
		return luaT_popen_handle_closed_error(L);

	int signo = lua_tonumber(L, 2);

	if (popen_send_signal(handle, signo) != 0)
		return luaT_push_nil_and_error(L);

	lua_pushboolean(L, true);
	return 1;
}

/**
 * Send SIGTERM signal to a child process.
 *
 * @param handle  a handle carries child process to terminate
 *
 * The function only sends SIGTERM signal and does NOT
 * free any resources (popen handle memory and file
 * descriptors).
 *
 * @see lbox_popen_signal() for errors and return values.
 */
static int
lbox_popen_terminate(struct lua_State *L)
{
	struct popen_handle *handle;
	bool is_closed;
	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL) {
		diag_set(IllegalParams, "Bad params, use: ph:terminate()");
		return luaT_error(L);
	}
	if (is_closed)
		return luaT_popen_handle_closed_error(L);

	int signo = SIGTERM;

	if (popen_send_signal(handle, signo) != 0)
		return luaT_push_nil_and_error(L);

	lua_pushboolean(L, true);
	return 1;
}

/**
 * Send SIGKILL signal to a child process.
 *
 * @param handle  a handle carries child process to kill
 *
 * The function only sends SIGKILL signal and does NOT
 * free any resources (popen handle memory and file
 * descriptors).
 *
 * @see lbox_popen_signal() for errors and return values.
 */
static int
lbox_popen_kill(struct lua_State *L)
{
	struct popen_handle *handle;
	bool is_closed;
	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL) {
		diag_set(IllegalParams, "Bad params, use: ph:kill()");
		return luaT_error(L);
	}
	if (is_closed)
		return luaT_popen_handle_closed_error(L);

	int signo = SIGKILL;

	if (popen_send_signal(handle, signo) != 0)
		return luaT_push_nil_and_error(L);

	lua_pushboolean(L, true);
	return 1;
}

/**
 * Wait until a child process get exited or signaled.
 *
 * @param handle  a handle of process to wait
 *
 * Raise an error on incorrect parameters or when the fiber is
 * cancelled:
 *
 * - IllegalParams:    an incorrect handle parameter.
 * - IllegalParams:    called on a closed handle.
 * - FiberIsCancelled: cancelled by an outside code.
 *
 * Return a process status table (the same as ph.status and
 * ph.info().status). @see lbox_popen_info() for the format
 * of the table.
 */
static int
lbox_popen_wait(struct lua_State *L)
{
	/*
	 * FIXME: Use trigger or fiber conds to sleep and wake up.
	 * FIXME: Add timeout option: ph:wait({timeout = <...>})
	 */
	struct popen_handle *handle;
	bool is_closed;
	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL) {
		diag_set(IllegalParams, "Bad params, use: ph:wait()");
		return luaT_error(L);
	}
	if (is_closed)
		return luaT_popen_handle_closed_error(L);

	int state;
	int exit_code;

	while (true) {
		popen_state(handle, &state, &exit_code);
		assert(state < POPEN_STATE_MAX);
		if (state != POPEN_STATE_ALIVE)
			break;
		fiber_sleep(POPEN_LUA_WAIT_DELAY);
		luaL_testcancel(L);
	}

	return luaT_push_popen_process_status(L, state, exit_code);
}

/**
 * Read data from a child peer.
 *
 * @param handle        handle of a child process
 * @param opts          an options table
 * @param opts.stdout   whether to read from stdout, boolean
 *                      (default: true)
 * @param opts.stderr   whether to read from stderr, boolean
 *                      (default: false)
 * @param opts.timeout  time quota in seconds
 *                      (default: 100 years)
 *
 * Read data from stdout or stderr streams with @a timeout.
 * By default it reads from stdout. Set @a opts.stderr to
 * `true` to read from stderr.
 *
 * It is not possible to read from stdout and stderr both in
 * one call. Set either @a opts.stdout or @a opts.stderr.
 *
 * Raise an error on incorrect parameters or when the fiber is
 * cancelled:
 *
 * - IllegalParams:    incorrect type or value of a parameter.
 * - IllegalParams:    called on a closed handle.
 * - IllegalParams:    opts.stdout and opts.stderr are set both
 * - IllegalParams:    a requested IO operation is not supported
 *                     by the handle (stdout / stderr is not
 *                     piped).
 * - IllegalParams:    attempt to operate on a closed file
 *                     descriptor.
 * - FiberIsCancelled: cancelled by an outside code.
 *
 * Return a string on success, an empty string at EOF.
 *
 * Return `nil, err` on a failure. Possible reasons:
 *
 * - SocketError: an IO error occurs at read().
 * - TimedOut:    @a timeout quota is exceeded.
 * - OutOfMemory: no memory space for a buffer to read into.
 * - LuajitError: ("not enough memory"): no memory space for
 *                the Lua string.
 */
static int
lbox_popen_read(struct lua_State *L)
{
	struct popen_handle *handle;
	bool is_closed;

	/*
	 * Actual default is POPEN_FLAG_FD_STDOUT, but
	 * it is set only when no std* option is passed.
	 */
	unsigned int flags = POPEN_FLAG_NONE;

	ev_tstamp timeout = TIMEOUT_INFINITY;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	/* Extract handle. */
	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL)
		goto usage;
	if (is_closed)
		return luaT_popen_handle_closed_error(L);

	/* Extract options. */
	if (!lua_isnoneornil(L, 2)) {
		if (lua_type(L, 2) != LUA_TTABLE)
			goto usage;

		/* FIXME: Shorten boolean options parsing. */

		lua_getfield(L, 2, "stdout");
		if (!lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				goto usage;
			if (lua_toboolean(L, -1) == 0)
				flags &= ~POPEN_FLAG_FD_STDOUT;
			else
				flags |= POPEN_FLAG_FD_STDOUT;
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "stderr");
		if (!lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				goto usage;
			if (lua_toboolean(L, -1) == 0)
				flags &= ~POPEN_FLAG_FD_STDERR;
			else
				flags |= POPEN_FLAG_FD_STDERR;
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "timeout");
		if (!lua_isnil(L, -1) &&
		    (timeout = luaT_check_timeout(L, -1)) < 0.0)
			goto usage;
		lua_pop(L, 1);
	}

	/* Read from stdout by default. */
	if (!(flags & (POPEN_FLAG_FD_STDOUT | POPEN_FLAG_FD_STDERR)))
		flags |= POPEN_FLAG_FD_STDOUT;

	size_t size = POPEN_LUA_READ_BUF_SIZE;
	char *buf = region_alloc(region, size);
	if (buf == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "read buffer");
		return luaT_push_nil_and_error(L);
	}

	static_assert(POPEN_LUA_READ_BUF_SIZE <= SSIZE_MAX,
		      "popen: read buffer is too big");
	ssize_t rc = popen_read_timeout(handle, buf, size, flags, timeout);
	if (rc < 0 || luaT_push_string_noxc(L, buf, rc) != 0)
		goto err;
	region_truncate(region, region_svp);
	return 1;

usage:
	diag_set(IllegalParams, "Bad params, use: ph:read([{"
		 "stdout = <boolean>, "
		 "stderr = <boolean>, "
		 "timeout = <number>}])");
	return luaT_error(L);
err:
	region_truncate(region, region_svp);
	struct error *e = diag_last_error(diag_get());
	if (e->type == &type_IllegalParams ||
	    e->type == &type_FiberIsCancelled)
		return luaT_error(L);
	return luaT_push_nil_and_error(L);
}

/**
 * Write data to a child peer.
 *
 * @param handle        a handle of a child process
 * @param str           a string to write
 * @param opts          table of options
 * @param opts.timeout  time quota in seconds
 *                      (default: 100 years)
 *
 * Write string @a str to stdin stream of a child process.
 *
 * The function may yield forever if a child process does
 * not read data from stdin and a pipe buffer becomes full.
 * Size of this buffer depends on a platform. Use
 * @a opts.timeout when unsure.
 *
 * When @a opts.timeout is not set, the function blocks
 * (yields the fiber) until all data is written or an error
 * happened.
 *
 * Raise an error on incorrect parameters or when the fiber is
 * cancelled:
 *
 * - IllegalParams:    incorrect type or value of a parameter.
 * - IllegalParams:    called on a closed handle.
 * - IllegalParams:    string length is greater then SSIZE_MAX.
 * - IllegalParams:    a requested IO operation is not supported
 *                     by the handle (stdin is not piped).
 * - IllegalParams:    attempt to operate on a closed file
 *                     descriptor.
 * - FiberIsCancelled: cancelled by an outside code.
 *
 * Return `true` on success.
 *
 * Return `nil, err` on a failure. Possible reasons:
 *
 * - SocketError: an IO error occurs at write().
 * - TimedOut:    @a timeout quota is exceeded.
 */
static int
lbox_popen_write(struct lua_State *L)
{
	struct popen_handle *handle;
	bool is_closed;
	const char *str;
	size_t len;
	ev_tstamp timeout = TIMEOUT_INFINITY;

	/* Extract handle and string to write. */
	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL ||
	    (str = luaL_tolstring_strict(L, 2, &len)) == NULL)
		goto usage;
	if (is_closed)
		return luaT_popen_handle_closed_error(L);

	/* Extract options. */
	if (!lua_isnoneornil(L, 3)) {
		if (lua_type(L, 3) != LUA_TTABLE)
			goto usage;

		lua_getfield(L, 3, "timeout");
		if (!lua_isnil(L, -1) &&
		    (timeout = luaT_check_timeout(L, -1)) < 0.0)
			goto usage;
		lua_pop(L, 1);
	}

	unsigned int flags = POPEN_FLAG_FD_STDIN;
	ssize_t rc = popen_write_timeout(handle, str, len, flags, timeout);
	assert(rc < 0 || rc == (ssize_t)len);
	if (rc < 0) {
		struct error *e = diag_last_error(diag_get());
		if (e->type == &type_IllegalParams ||
		    e->type == &type_FiberIsCancelled)
			return luaT_error(L);
		return luaT_push_nil_and_error(L);
	}
	lua_pushboolean(L, true);
	return 1;

usage:
	diag_set(IllegalParams, "Bad params, use: ph:write(str[, {"
		 "timeout = <number>}])");
	return luaT_error(L);
}

/**
 * Close parent's ends of std* fds.
 *
 * @param handle        handle of a child process
 * @param opts          an options table
 * @param opts.stdin    close parent's end of stdin, boolean
 * @param opts.stdout   close parent's end of stdout, boolean
 * @param opts.stderr   close parent's end of stderr, boolean
 *
 * The main reason to use this function is to send EOF to
 * child's stdin. However parent's end of stdout / stderr
 * may be closed too.
 *
 * The function does not fail on already closed fds (idempotence).
 * However it fails on attempt to close the end of a pipe that was
 * never exist. In other words, only those std* options that
 * were set to popen.opts.PIPE at a handle creation may be used
 * here (for popen.shell: 'r' corresponds to stdout, 'R' to stderr
 * and 'w' to stdin).
 *
 * The function does not close any fds on a failure: either all
 * requested fds are closed or neither of them.
 *
 * Example:
 *
 *  | local popen = require('popen')
 *  |
 *  | local ph = popen.shell('sed s/foo/bar/', 'rw')
 *  | ph:write('lorem foo ipsum')
 *  | ph:shutdown({stdin = true})
 *  | local res = ph:read()
 *  | ph:close()
 *  | print(res) -- lorem bar ipsum
 *
 * Raise an error on incorrect parameters:
 *
 * - IllegalParams:  an incorrect handle parameter.
 * - IllegalParams:  called on a closed handle.
 * - IllegalParams:  neither stdin, stdout nor stderr is choosen.
 * - IllegalParams:  a requested IO operation is not supported
 *                   by the handle (one of std* is not piped).
 *
 * Return `true` on success.
 */
static int
lbox_popen_shutdown(struct lua_State *L)
{
	struct popen_handle *handle;
	bool is_closed;

	/* Extract handle. */
	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL)
		goto usage;
	if (is_closed)
		return luaT_popen_handle_closed_error(L);

	unsigned int flags = POPEN_FLAG_NONE;

	/* Extract options. */
	if (!lua_isnoneornil(L, 2)) {
		if (lua_type(L, 2) != LUA_TTABLE)
			goto usage;

		/*
		 * FIXME: Those blocks duplicates ones from
		 * lbox_popen_read().
		 *
		 * Let's introduce a helper like
		 * luaT_popen_parse_stdX() but about boolean
		 * flags rather than stream actions.
		 */

		lua_getfield(L, 2, "stdin");
		if (!lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				goto usage;
			if (lua_toboolean(L, -1) == 0)
				flags &= ~POPEN_FLAG_FD_STDIN;
			else
				flags |= POPEN_FLAG_FD_STDIN;
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "stdout");
		if (!lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				goto usage;
			if (lua_toboolean(L, -1) == 0)
				flags &= ~POPEN_FLAG_FD_STDOUT;
			else
				flags |= POPEN_FLAG_FD_STDOUT;
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "stderr");
		if (!lua_isnil(L, -1)) {
			if (lua_type(L, -1) != LUA_TBOOLEAN)
				goto usage;
			if (lua_toboolean(L, -1) == 0)
				flags &= ~POPEN_FLAG_FD_STDERR;
			else
				flags |= POPEN_FLAG_FD_STDERR;
		}
		lua_pop(L, 1);
	}

	if (popen_shutdown(handle, flags) != 0) {
		/*
		 * FIXME: This block is often duplicated,
		 * let's extract it to a helper.
		 */
		struct error *e = diag_last_error(diag_get());
		if (e->type == &type_IllegalParams)
			return luaT_error(L);
		return luaT_push_nil_and_error(L);
	}

	lua_pushboolean(L, true);
	return 1;

usage:
	diag_set(IllegalParams, "Bad params, use: ph:shutdown({"
		 "stdin = <boolean>, "
		 "stdout = <boolean>, "
		 "stderr = <boolean>})");
	return luaT_error(L);

}

/**
 * Return information about popen handle.
 *
 * @param handle  a handle of a child process
 *
 * Raise an error on incorrect parameters:
 *
 * - IllegalParams: an incorrect handle parameter.
 * - IllegalParams: called on a closed handle.
 *
 * Return information about the handle in the following
 * format:
 *
 *     {
 *         pid = <number> or <nil>,
 *         command = <string>,
 *         opts = <table>,
 *         status = <table>,
 *         stdin = one-of(
 *             popen.stream.OPEN   (== 'open'),
 *             popen.stream.CLOSED (== 'closed'),
 *             nil,
 *         ),
 *         stdout = one-of(
 *             popen.stream.OPEN   (== 'open'),
 *             popen.stream.CLOSED (== 'closed'),
 *             nil,
 *         ),
 *         stderr = one-of(
 *             popen.stream.OPEN   (== 'open'),
 *             popen.stream.CLOSED (== 'closed'),
 *             nil,
 *         ),
 *     }
 *
 * `pid` is a process id of the process when it is alive,
 * otherwise `pid` is nil.
 *
 * `command` is a concatenation of space separated arguments
 * that were passed to execve(). Multiword arguments are quoted.
 * Quotes inside arguments are not escaped.
 *
 * `opts` is a table of handle options in the format of
 * popen.new() `opts` parameter. `opts.env` is not shown here,
 * because the environment variables map is not stored in a
 * handle.
 *
 * `status` is a table that represents a process status in the
 * following format:
 *
 *     {
 *         state = one-of(
 *             popen.state.ALIVE    (== 'alive'),
 *             popen.state.EXITED   (== 'exited'),
 *             popen.state.SIGNALED (== 'signaled'),
 *         )
 *
 *         -- Present when `state` is 'exited'.
 *         exit_code = <number>,
 *
 *         -- Present when `state` is 'signaled'.
 *         signo = <number>,
 *         signame = <string>,
 *     }
 *
 * `stdin`, `stdout`, `stderr` reflect status of parent's end
 * of a piped stream. When a stream is not piped the field is
 * not present (`nil`). When it is piped, the status may be
 * one of the following:
 *
 * - popen.stream.OPEN    (== 'open')
 * - popen.stream.CLOSED  (== 'closed')
 *
 * The status may be changed from 'open' to 'closed'
 * by :shutdown({std... = true}) call.
 *
 * @see luaT_push_popen_opts()
 * @see luaT_push_popen_process_status()
 *
 * Example 1 (tarantool console):
 *
 *  | tarantool> require('popen').new({'/usr/bin/touch', '/tmp/foo'})
 *  | ---
 *  | - command: /usr/bin/touch /tmp/foo
 *  |   status:
 *  |     state: alive
 *  |   opts:
 *  |     stdout: inherit
 *  |     stdin: inherit
 *  |     group_signal: false
 *  |     keep_child: false
 *  |     close_fds: true
 *  |     restore_signals: true
 *  |     shell: false
 *  |     setsid: false
 *  |     stderr: inherit
 *  |   pid: 9499
 *  | ...
 *
 * Example 2 (tarantool console):
 *
 *  | tarantool> require('popen').shell('grep foo', 'wrR')
 *  | ---
 *  | - stdout: open
 *  |   command: sh -c 'grep foo'
 *  |   stderr: open
 *  |   status:
 *  |     state: alive
 *  |   stdin: open
 *  |   opts:
 *  |     stdout: pipe
 *  |     stdin: pipe
 *  |     group_signal: true
 *  |     keep_child: false
 *  |     close_fds: true
 *  |     restore_signals: true
 *  |     shell: true
 *  |     setsid: true
 *  |     stderr: pipe
 *  |   pid: 10497
 *  | ...
 */
static int
lbox_popen_info(struct lua_State *L)
{
	struct popen_handle *handle;
	bool is_closed;

	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL) {
		diag_set(IllegalParams, "Bad params, use: ph:info()");
		return luaT_error(L);
	}
	if (is_closed)
		return luaT_popen_handle_closed_error(L);

	struct popen_stat st;

	popen_stat(handle, &st);

	lua_createtable(L, 0, 7);

	if (st.pid >= 0) {
		lua_pushinteger(L, st.pid);
		lua_setfield(L, -2, "pid");
	}

	lua_pushstring(L, popen_command(handle));
	lua_setfield(L, -2, "command");

	luaT_push_popen_opts(L, st.flags);
	lua_setfield(L, -2, "opts");

	int state;
	int exit_code;
	popen_state(handle, &state, &exit_code);
	assert(state < POPEN_STATE_MAX);
	luaT_push_popen_process_status(L, state, exit_code);
	lua_setfield(L, -2, "status");

	luaT_push_popen_stdX_status(L, handle, STDIN_FILENO);
	lua_setfield(L, -2, "stdin");

	luaT_push_popen_stdX_status(L, handle, STDOUT_FILENO);
	lua_setfield(L, -2, "stdout");

	luaT_push_popen_stdX_status(L, handle, STDERR_FILENO);
	lua_setfield(L, -2, "stderr");

	return 1;
}

/**
 * Close a popen handle.
 *
 * @param handle  a handle to close
 *
 * Basically it kills a process using SIGKILL and releases all
 * resources assosiated with the popen handle.
 *
 * Details about signaling:
 *
 * - The signal is sent only when opts.keep_child is not set.
 * - The signal is sent only when a process is alive according
 *   to the information available on current even loop iteration.
 *   (There is a gap here: a zombie may be signaled; it is
 *   harmless.)
 * - The signal is sent to a process or a grocess group depending
 *   of opts.group_signal. (@see lbox_popen_new() for details of
 *   group signaling).
 * - There are peculiars in group signaling on Mac OS,
 *   @see lbox_popen_signal() for details.
 *
 * Resources are released disregarding of whether a signal
 * sending succeeds: fds are closed, memory is released,
 * the handle is marked as closed.
 *
 * No operation is possible on a closed handle except
 * :close(), which always successful on closed handle
 * (idempotence).
 *
 * Raise an error on incorrect parameters:
 *
 * - IllegalParams: an incorrect handle parameter.
 *
 * The function may return `true` or `nil, err`, but it always
 * frees the handle resources. So any return value usually
 * means success for a caller. The return values are purely
 * informational: it is for logging or same kind of reporting.
 *
 * Possible diagnostics (don't consider them as errors):
 *
 * - SystemError: no permission to send a signal to
 *                a process or a process group
 *
 *                This diagnostics may appear due to
 *                Mac OS behaviour on zombies when
 *                opts.group_signal is set,
 *                @see lbox_popen_signal().
 *
 *                Whether it may appear due to other
 *                reasons is unclear.
 *
 * Always return `true` when a process is known as dead (say,
 * after ph:wait()): no signal will be send, so no 'failure'
 * may appear.
 */
static int
lbox_popen_close(struct lua_State *L)
{
	struct popen_handle *handle;
	bool is_closed;
	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL) {
		diag_set(IllegalParams, "Bad params, use: ph:close()");
		return luaT_error(L);
	}

	/* Do nothing on a closed handle. */
	if (is_closed) {
		lua_pushboolean(L, true);
		return 1;
	}

	luaT_mark_popen_handle_closed(L, 1);

	if (popen_delete(handle) != 0)
		return luaT_push_nil_and_error(L);

	lua_pushboolean(L, true);
	return 1;
}

/**
 * Get a field from a handle.
 *
 * @param handle  a handle of a child process
 * @param key     a field name, string
 *
 * The function performs the following steps.
 *
 * Raise an error on incorrect parameters:
 *
 * - IllegalParams: incorrect type or value of a parameter.
 *
 * If there is a handle method with @a key name, return it.
 *
 * Raise an error on closed popen handle:
 *
 * - IllegalParams: called on a closed handle.
 *
 * If a @key is one of the following, return a value for it:
 *
 * - pid
 * - command
 * - opts
 * - status
 * - stdin
 * - stdout
 * - stderr
 *
 * @see lbox_popen_info() for description of those fields.
 *
 * Otherwise return `nil`.
 */
static int
lbox_popen_index(struct lua_State *L)
{
	struct popen_handle *handle;
	bool is_closed;
	const char *key;

	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL ||
	    (key = luaL_tolstring_strict(L, 2, NULL)) == NULL) {
		diag_set(IllegalParams,
			 "Bad params, use __index(ph, <string>)");
		return luaT_error(L);
	}

	/*
	 * If `key` is a method name, return it.
	 *
	 * The __index metamethod performs only checks that
	 * it needs on its own. Despite there are common parts
	 * across methods, it is better to validate all
	 * parameters within a method itself.
	 *
	 * In particular, methods should perform a check for
	 * closed handles.
	 */
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);
	if (! lua_isnil(L, -1))
		return 1;

	/* Does not allow to get a field from a closed handle. */
	if (is_closed) {
		diag_set(IllegalParams,
			 "Attempt to index a closed popen handle");
		return luaT_error(L);
	}

	if (strcmp(key, "pid") == 0) {
		if (handle->pid >= 0)
			lua_pushinteger(L, handle->pid);
		else
			lua_pushnil(L);
		return 1;
	}

	if (strcmp(key, "command") == 0) {
		lua_pushstring(L, popen_command(handle));
		return 1;
	}

	if (strcmp(key, "opts") == 0) {
		luaT_push_popen_opts(L, handle->flags);
		return 1;
	}

	int state;
	int exit_code;
	popen_state(handle, &state, &exit_code);
	assert(state < POPEN_STATE_MAX);
	if (strcmp(key, "status") == 0)
		return luaT_push_popen_process_status(L, state, exit_code);

	if (strcmp(key, "stdin") == 0)
		return luaT_push_popen_stdX_status(L, handle, STDIN_FILENO);

	if (strcmp(key, "stdout") == 0)
		return luaT_push_popen_stdX_status(L, handle, STDOUT_FILENO);

	if (strcmp(key, "stderr") == 0)
		return luaT_push_popen_stdX_status(L, handle, STDERR_FILENO);

	lua_pushnil(L);
	return 1;
}

/**
 * Popen handle representation for REPL (console).
 *
 * @param handle  a handle of a child process
 *
 * The function just calls lbox_popen_info() for a
 * handle when it is not closed.
 *
 * Return '<closed popen handle>' string for a closed
 * handle.
 *
 * @see lbox_popen_info()
 */
static int
lbox_popen_serialize(struct lua_State *L)
{
	struct popen_handle *handle;
	bool is_closed;

	if ((handle = luaT_check_popen_handle(L, 1, &is_closed)) == NULL) {
		diag_set(IllegalParams, "Bad params, use: __serialize(ph)");
		return luaT_error(L);
	}

	if (is_closed) {
		lua_pushliteral(L, "<closed popen handle>");
		return 1;
	}

	return lbox_popen_info(L);
}

/**
 * Free popen handle resources.
 *
 * @param handle  a handle to free
 *
 * The same as lbox_popen_close(), but silently exits on any
 * failure.
 *
 * The method may be called manually from Lua, so it is able to
 * proceed with an incorrect and a closed handle. It also marks
 * a handle as closed to don't free resources twice if the
 * handle is collected by Lua GC after a manual call of the
 * method.
 *
 * Don't return a value.
 */
static int
lbox_popen_gc(struct lua_State *L)
{
	bool is_closed;
	struct popen_handle *handle = luaT_check_popen_handle(L, 1, &is_closed);
	if (handle == NULL || is_closed)
		return 0;
	popen_delete(handle);
	luaT_mark_popen_handle_closed(L, 1);
	return 0;
}

/* }}} */

/* {{{ Module initialization */

/**
 * Create popen functions and methods.
 *
 * Module functions
 * ----------------
 *
 * - popen.new()
 * - popen.shell()
 *
 * Module constants
 * ----------------
 *
 * - popen.opts
 *   - INHERIT (== 'inherit')
 *   - DEVNULL (== 'devnull')
 *   - CLOSE   (== 'close')
 *   - PIPE    (== 'pipe')
 *
 * - popen.signal
 *   - SIGTERM (== 9)
 *   - SIGKILL (== 15)
 *   - ...
 *
 * - popen.state
 *   - ALIVE    (== 'alive')
 *   - EXITED   (== 'exited')
 *   - SIGNALED (== 'signaled')
 *
 * - popen.stream
 *   - OPEN    (== 'open')
 *   - CLOSED  (== 'closed')
 */
void
tarantool_lua_popen_init(struct lua_State *L)
{
	/* Popen module methods. */
	static const struct luaL_Reg popen_methods[] = {
		{"new",		lbox_popen_new,		},
		{"shell",	lbox_popen_shell,	},
		{NULL, NULL},
	};
	luaL_register_module(L, "popen", popen_methods);

	/*
	 * Popen handle methods and metamethods.
	 *
	 * Usual and closed popen handle userdata types have
	 * the same set of methods and metamethods.
	 */
	static const struct luaL_Reg popen_handle_methods[] = {
		{"signal",		lbox_popen_signal,	},
		{"terminate",		lbox_popen_terminate,	},
		{"kill",		lbox_popen_kill,	},
		{"wait",		lbox_popen_wait,	},
		{"read",		lbox_popen_read,	},
		{"write",		lbox_popen_write,	},
		{"shutdown",		lbox_popen_shutdown,	},
		{"info",		lbox_popen_info,	},
		{"close",		lbox_popen_close,	},
		{"__index",		lbox_popen_index	},
		{"__serialize",		lbox_popen_serialize	},
		{"__gc",		lbox_popen_gc		},
		{NULL, NULL},
	};
	luaL_register_type(L, popen_handle_uname, popen_handle_methods);
	luaL_register_type(L, popen_handle_closed_uname, popen_handle_methods);

	/* Signals. */
	lua_newtable(L);
	for (int i = 0; popen_lua_signals[i].signame != NULL; ++i) {
		lua_pushinteger(L, popen_lua_signals[i].signo);
		lua_setfield(L, -2, popen_lua_signals[i].signame);
	}
	lua_setfield(L, -2, "signal");

	/* Stream actions. */
	lua_newtable(L);
	for (int i = 0; popen_lua_actions[i].name != NULL; ++i) {
		lua_pushstring(L, popen_lua_actions[i].value);
		lua_setfield(L, -2, popen_lua_actions[i].name);
	}
	lua_setfield(L, -2, "opts");

	/* Stream status. */
	lua_newtable(L);
	for (int i = 0; popen_lua_stream_status[i].name != NULL; ++i) {
		lua_pushstring(L, popen_lua_stream_status[i].value);
		lua_setfield(L, -2, popen_lua_stream_status[i].name);
	}
	lua_setfield(L, -2, "stream");

	/* Process states. */
	lua_newtable(L);
	for (int i = 0; popen_lua_states[i].name != NULL; ++i) {
		lua_pushstring(L, popen_lua_states[i].value);
		lua_setfield(L, -2, popen_lua_states[i].name);
	}
	lua_setfield(L, -2, "state");
}

/* }}} */
