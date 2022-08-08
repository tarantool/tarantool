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

#include "box/lua/console.h"
#include "box/session.h"
#include "box/port.h"
#include "box/error.h"
#include "lua/utils.h"
#include "lua/serializer.h"
#include "lua/fiber.h"
#include "fiber.h"
#include "coio.h"
#include "iostream.h"
#include "lua/msgpack.h"
#include "lua-yaml/lyaml.h"
#include "main.h"
#include "serialize_lua.h"
#include "say.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

struct rlist on_console_eval = RLIST_HEAD_INITIALIZER(on_console_eval);

static struct luaL_serializer *serializer_yaml;
static struct luaL_serializer *serializer_lua;

/*
 * Completion engine (Mike Paul's).
 * Used internally when collecting completions locally. Also a Lua
 * wrapper is provided enabling a remote server to compute completions
 * for a client.
 */
static char **
lua_rl_complete(lua_State *L, const char *text, int start, int end);

/*
 * Lua state that made the pending readline call.
 * This Lua state is accessed in readline callbacks. Unfortunately
 * readline library doesn't allow to pass it as a function argument.
 * Two concurrent readline() calls never happen.
 */
static struct lua_State *readline_L;

/**
 * Encode Lua object into Lua form.
 */
static int
lbox_console_format_lua(struct lua_State *L)
{
	lua_dumper_opts_t opts;
	int arg_count;

	/* Parse options and remove them */
	lua_parse_opts(L, &opts);
	lua_remove(L, 1);

	arg_count = lua_gettop(L);

	/* If nothing to process just exit early */
	if (arg_count == 0)
		return 0;

	/*
	 * When processing arguments we might
	 * need to modify reference (for example
	 * when __index references to object itself)
	 * thus make a copy of incoming data.
	 *
	 * Note that in yaml there is no map for
	 * Lua's "nil" value so for yaml encoder
	 * we do
	 *
	 * if (lua_isnil(L, i + 1))
	 *     luaL_pushnull(L);
	 * else
	 *     lua_pushvalue(L, i + 1);
	 *
	 *
	 * For lua mode we have to preserve "nil".
	 */
	lua_createtable(L, arg_count, 0);
	for (int i = 0; i < arg_count; ++i) {
		lua_pushvalue(L, i + 1);
		lua_rawseti(L, -2, i + 1);
	}

	lua_replace(L, 1);
	lua_settop(L, 1);
	int ret = lua_encode(L, serializer_lua, &opts);
	if (ret == 2) {
		/*
		 * Nil and error object are pushed onto the stack.
		 */
		assert(lua_isnil(L, -2));
		assert(lua_isstring(L, -1));
		return luaL_error(L, lua_tostring(L, -1));
	}
	assert(ret == 1);
	return ret;
}

/*
 * console_completion_handler()
 * Called by readline to collect plausible completions;
 * The call stack is as follows:
 *
 * - lbox_console_readline
 *  - (loop) rl_callback_read_char
 *    - console_completion_handler
 *
 * Delegates to the func selected when the call to lbox_console_readline
 * was made, e.g. readline({ completion = ... }).
 */
static char **
console_completion_handler(const char *text, int start, int end)
{
	size_t n, i;
	char **res;

	/*
	 * Don't falback to builtin filename completion, ever.
	 */
	rl_attempted_completion_over = 1;

	/*
	 * The lbox_console_readline() frame is still on the top of Lua
	 * stack. We can reach the function arguments. Assuming arg#1 is
	 * the options table.
	 */
	lua_getfield(readline_L, 1, "completion");
	if (lua_isnil(readline_L, -1)) {
		lua_pop(readline_L, 1);
		return NULL;
	}

	/*
	 * If the completion func is lbox_console_completion_handler()
	 * /we have it in upvalue #1/ which is a wrapper on top of
	 * lua_rl_complete, call lua_rl_complete func directly.
	 */
	if (lua_equal(readline_L, -1, lua_upvalueindex(1))) {
		lua_pop(readline_L, 1);
		res = lua_rl_complete(readline_L, text, start, end);
		goto done;
	}

	/* Slow path - arbitrary completion handler. */
	lua_pushstring(readline_L, text);
	lua_pushinteger(readline_L, start);
	lua_pushinteger(readline_L, end);
	if (lua_pcall(readline_L, 3, 1, 0) != 0 ||
	    !lua_istable(readline_L, -1) ||
	    (n = lua_objlen(readline_L, -1)) == 0) {

		lua_pop(readline_L, 1);
		return NULL;
	}
	res = malloc(sizeof(res[0]) * (n + 1));
	if (res == NULL) {
		lua_pop(readline_L, 1);
		return NULL;
	}
	res[n] = NULL;
	for (i = 0; i < n; i++) {
		lua_pushinteger(readline_L, i + 1);
		lua_gettable(readline_L, -2);
		res[i] = strdup(lua_tostring(readline_L, -1));
		lua_pop(readline_L, 1);
	}
	lua_pop(readline_L, 1);
done:
#if RL_READLINE_VERSION >= 0x0600
	rl_completion_suppress_append = 1;
#endif
	return res;
}

/*
 * console_push_line()
 * Readline invokes this callback once the whole line is ready.
 * The call stack is as follows:
 *
 * - lbox_console_readline
 *  - (loop) rl_callback_read_char
 *    - console_push_line
 *
 * The callback creates a copy of the line on the Lua stack; this copy
 * becomes the lbox_console_readline()'s ultimate result.
 *
 * The second return value is boolean, which means 'discard the line'.
 */
static void
console_push_line(char *line)
{
	/* XXX pushnil/pushstring may err */
	if (line == NULL)
		lua_pushnil(readline_L);
	else
		lua_pushstring(readline_L, line);

	lua_pushboolean(readline_L, false);
#ifdef HAVE_GNU_READLINE
	/*
	 * This is to avoid a stray prompt on the next line with GNU
	 * readline. Interestingly, it botches the terminal when
	 * attempted with libeditline.
	 */
	rl_callback_handler_install(NULL, NULL);
#endif
	free(line);
}

/*
 * The flag indicates if sigint was sent.
 */
static bool sigint_called;
/*
 * The pointer to interactive fiber is needed to wake it up
 * when SIGINT handler is called.
 */
static struct fiber *interactive_fb;

/*
 * The sigint callback for console mode.
 */
static void
console_sigint_handler(ev_loop *loop, struct ev_signal *w, int revents)
{
	(void)loop;
	(void)w;
	(void)revents;

	sigint_called = true;
	fiber_wakeup(interactive_fb);
}

/* {{{ Show/hide prompt */

/*
 * The idea is borrowed from
 * https://metacpan.org/dist/AnyEvent-ReadLine-Gnu/source/Gnu.pm
 *
 * Since this feature is not thread-safe, it will work only when logging occurs
 * from main (transaction) thread.
 */

static char *saved_prompt = NULL;
static char *saved_line_buffer = NULL;
static int saved_line_buffer_len = 0;
static int saved_point = 0;

static int console_hide_prompt_ref = LUA_NOREF;
static int console_show_prompt_ref = LUA_NOREF;

/**
 * Don't attempt to hide/show prompt in certain readline states.
 *
 * There are readline states, where rl_message() is called
 * internally. In this case an actual readline's line on the
 * screen is not prompt + line buffer. Current code can't properly
 * save and restore the line.
 */
static bool
console_can_hide_show_prompt(void)
{
	if (RL_ISSTATE(RL_STATE_NSEARCH))
		return false;
	if (RL_ISSTATE(RL_STATE_ISEARCH))
		return false;
	if (RL_ISSTATE(RL_STATE_NUMERICARG))
		return false;
	return true;
}

/**
 * Save and hide readline's output (prompt and current user
 * input).
 */
static void
console_hide_prompt(void)
{
	if (!console_can_hide_show_prompt() || !cord_is_main())
		return;

	if (rl_prompt == NULL) {
		saved_prompt = NULL;
	} else {
		saved_prompt = xstrdup(rl_prompt);
	}
	rl_set_prompt("");

	saved_point = rl_point;

	if (rl_line_buffer == NULL) {
		saved_line_buffer = NULL;
		saved_line_buffer_len = 0;
	} else {
		saved_line_buffer = xmalloc(rl_end + 1);
		memcpy(saved_line_buffer, rl_line_buffer, rl_end);
		saved_line_buffer[rl_end] = '\0';
		saved_line_buffer_len = rl_end;
	}
	rl_replace_line("", 0);

	rl_redisplay();
}

/**
 * Show saved readline's output and free saved strings.
 */
static void
console_show_prompt(void)
{
	if (!console_can_hide_show_prompt() || !cord_is_main())
		return;

	rl_set_prompt(saved_prompt);
	free(saved_prompt);
	saved_prompt = NULL;

	if (saved_line_buffer == NULL) {
		rl_replace_line("", 0);
	} else {
		rl_replace_line(saved_line_buffer, saved_line_buffer_len);
	}
	free(saved_line_buffer);
	saved_line_buffer = NULL;
	saved_line_buffer_len = 0;

	rl_point = saved_point;
	saved_point = 0;

	rl_redisplay();
}

static int
lbox_console_hide_prompt(struct lua_State *L)
{
	(void)L;
	console_hide_prompt();
	return 0;
}

static int
lbox_console_show_prompt(struct lua_State *L)
{
	(void)L;
	console_show_prompt();
	return 0;
}

/**
 * Allow to disable hide/show prompt actions using an environment
 * variable.
 *
 * It is not supposed to be a documented variable, but rather just
 * a way to turn off the feature if something goes wrong.
 */
static bool
console_hide_show_prompt_is_enabled(void)
{
	char var_buf[10];
	const char *envvar = getenv_safe("TT_CONSOLE_HIDE_SHOW_PROMPT", var_buf,
					 sizeof(var_buf));

	/* Enabled by default. */
	if (envvar == NULL || *envvar == '\0')
		return true;

	/* Explicitly enabled or disabled. */
	if (strcasecmp(envvar, "false") == 0)
		return false;
	if (strcasecmp(envvar, "true") == 0)
		return true;

	/* Accept 0/1 as boolean values. */
	if (strcmp(envvar, "0") == 0)
		return false;
	if (strcmp(envvar, "1") == 0)
		return true;

	/* Can't parse the value, let's use the default. */
	return true;
}

static void
luaT_console_setup_write_cb(struct lua_State *L)
{
	if (!console_hide_show_prompt_is_enabled())
		return;

	/*
	 * Set the print callback first, because technically
	 * luaT_call() may fail, and then set the logger callback.
	 * If the former will fail, things will be consistent:
	 * no callbacks are set.
	 *
	 * In fact, the require call may fail only if a user
	 * removes the internal module from package.loaded
	 * manually. A user shouldn't do that.
	 */
	lua_getfield(L, LUA_GLOBALSINDEX, "require");
	lua_pushstring(L, "internal.print");
	if (luaT_call(L, 1, 1) != 0)
		return;

	lua_rawgeti(L, LUA_REGISTRYINDEX, console_hide_prompt_ref);
	lua_setfield(L, -2, "before_cb");
	lua_rawgeti(L, LUA_REGISTRYINDEX, console_show_prompt_ref);
	lua_setfield(L, -2, "after_cb");

	lua_pop(L, 1);

	say_set_stderr_callback(console_hide_prompt, console_show_prompt);
}

static void
luaT_console_cleanup_write_cb(struct lua_State *L)
{
	if (!console_hide_show_prompt_is_enabled())
		return;

	/* See a comment in luaT_console_setup_write_cb(). */
	lua_getfield(L, LUA_GLOBALSINDEX, "require");
	lua_pushstring(L, "internal.print");
	if (luaT_call(L, 1, 1) != 0)
		return;

	lua_pushnil(L);
	lua_setfield(L, -2, "before_cb");
	lua_pushnil(L);
	lua_setfield(L, -2, "after_cb");

	lua_pop(L, 1);

	say_set_stderr_callback(NULL, NULL);
}

/* }}} Show/hide prompt */

/* implements readline() Lua API */
static int
lbox_console_readline(struct lua_State *L)
{
	const char *prompt = NULL;
	int top;
	int completion = 0;
	interactive_fb = fiber();
	sigint_cb_t old_cb = set_sigint_cb(console_sigint_handler);
	sigint_called = false;

	if (lua_gettop(L) > 0) {
		switch (lua_type(L, 1)) {
		case LUA_TSTRING:
			prompt = lua_tostring(L, 1);
			break;
		case LUA_TTABLE:
			lua_getfield(L, 1, "prompt");
			prompt = lua_tostring(L, -1);
			lua_pop(L, 1);
			lua_getfield(L, 1, "completion");
			if (!lua_isnil(L, -1))
				completion = 1;
			lua_pop(L, 1);
			break;
		default:
			luaL_error(L, "readline([prompt])");
		}
	}

	if (prompt == NULL)
		prompt = "> ";

	if (readline_L != NULL)
		luaL_error(L, "readline(): earlier call didn't complete yet");

	luaT_console_setup_write_cb(L);

	readline_L = L;

	if (completion) {
		rl_inhibit_completion = 0;
		rl_attempted_completion_function = console_completion_handler;
		rl_completer_word_break_characters =
			"\t\r\n !\"#$%&'()*+,-/;<=>?@[\\]^`{|}~";
		rl_completer_quote_characters = "\"'";
#if RL_READLINE_VERSION < 0x0600
		rl_completion_append_character = '\0';
#endif
	} else {
		rl_inhibit_completion = 1;
		rl_attempted_completion_function = NULL;
	}

	/*
	 * Readline library provides eventloop-friendly API; repeat
	 * until console_push_line() manages to capture the result.
	 */
	rl_callback_handler_install(prompt, console_push_line);
	top = lua_gettop(L);
	while (top == lua_gettop(L)) {
		while (coio_wait(STDIN_FILENO, COIO_READ,
				 TIMEOUT_INFINITY) == 0) {
			if (sigint_called) {
				const char *line_end = "^C\n";
				ssize_t rc = write(STDOUT_FILENO, line_end,
						   strlen(line_end));
				(void)rc;
				/*
				 * Discard current input and disable search
				 * mode.
				 */
				RL_UNSETSTATE(RL_STATE_ISEARCH |
					      RL_STATE_NSEARCH |
					      RL_STATE_SEARCH);
				rl_on_new_line();
				rl_replace_line("", 0);
				lua_pushstring(L, "");
				lua_pushboolean(L, sigint_called);

				luaT_console_cleanup_write_cb(L);

				readline_L = NULL;
				sigint_called = false;
				set_sigint_cb(old_cb);
				return 2;
			}
			/*
			 * Make sure the user of interactive
			 * console has not hanged us, otherwise
			 * we might spin here forever eating
			 * the whole cpu time.
			 */
			if (fiber_is_cancelled()) {
				luaT_console_cleanup_write_cb(L);
				set_sigint_cb(old_cb);
			}
			luaL_testcancel(L);
		}
		rl_callback_read_char();
	}

	readline_L = NULL;
	/* Incidents happen. */
#pragma GCC poison readline_L
	rl_attempted_completion_function = NULL;
	luaT_console_cleanup_write_cb(L);
	set_sigint_cb(old_cb);
	luaL_testcancel(L);
	return 2;
}

/* C string array to lua table converter */
static int
console_completion_helper(struct lua_State *L)
{
	size_t i;
	char **res = *(char ***)lua_topointer(L, -1);
	assert(lua_islightuserdata(L, -1));
	assert(L != NULL);
	lua_createtable(L, 0, 0);
	for (i = 0; res[i]; i++) {
		lua_pushstring(L, res[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

/*
 * completion_handler() Lua API
 * Exposing completion engine to Lua.
 */
static int
lbox_console_completion_handler(struct lua_State *L)
{
	size_t i;
	char **res;
	int st;

	/*
	 * Prepare for the future pcall;
	 * this may err, hence do it before res is created
	 */
	lua_pushcfunction(L, console_completion_helper);
	lua_pushlightuserdata(L, &res);

	res = lua_rl_complete(L, lua_tostring(L, 1),
			      lua_tointeger(L, 2), lua_tointeger(L, 3));

	if (res == NULL) {
		return 0;
	}

	st = lua_pcall(L, 1, 1, 0);

	/* free res */
	for (i = 0; res[i]; i++) {
		free(res[i]);
	}
	free(res);
	res = NULL;

	if (st != 0) {
		lua_error(L);
	}

	return 1;
}

static int
lbox_console_load_history(struct lua_State *L)
{
	if (!lua_isstring(L, 1))
		luaL_error(L, "load_history(filename: string)");

	read_history(lua_tostring(L, 1));
	return 0;
}

static int
lbox_console_save_history(struct lua_State *L)
{
	if (!lua_isstring(L, 1))
		luaL_error(L, "save_history(filename: string)");

	write_history(lua_tostring(L, 1));
	return 0;
}

static int
lbox_console_add_history(struct lua_State *L)
{
	if (lua_gettop(L) < 1 || !lua_isstring(L, 1))
		luaL_error(L, "add_history(string)");

	const char *s = lua_tostring(L, 1);
	if (*s) {
		HIST_ENTRY *hist_ent = history_get(history_length - 1 + history_base);
		const char *prev_s = hist_ent ? hist_ent->line : "";
		if (strcmp(prev_s, s) != 0)
			add_history(s);
	}
	return 0;
}

/**
 * Encode Lua object into YAML documents. Gets variable count of
 * parameters.
 * @retval 1 Pushes string with YAML documents - one per
 *         parameter.
 */
static int
lbox_console_format_yaml(struct lua_State *L)
{
	int arg_count = lua_gettop(L);
	if (arg_count == 0) {
		lua_pushstring(L, "---\n...\n");
		return 1;
	}
	lua_createtable(L, arg_count, 0);
	for (int i = 0; i < arg_count; ++i) {
		if (lua_isnil(L, i + 1))
			luaL_pushnull(L);
		else
			lua_pushvalue(L, i + 1);
		lua_rawseti(L, -2, i + 1);
	}
	lua_replace(L, 1);
	lua_settop(L, 1);
	int ret = lua_yaml_encode(L, serializer_yaml, NULL, NULL);
	if (ret == 2) {
		/*
		 * Nil and error object are pushed onto the stack.
		 */
		assert(lua_isnil(L, -2));
		assert(lua_isstring(L, -1));
		return luaL_error(L, lua_tostring(L, -1));
	}
	assert(ret == 1);
	return ret;
}

/**
 * Runs registered on_console_eval triggers.
 * Takes eval expression string, which is passed to trigger callback.
 */
static int
lbox_console_run_on_eval(struct lua_State *L)
{
	const char *expr = lua_tostring(L, 1);
	trigger_run(&on_console_eval, (void *)expr);
	return 0;
}

int
console_session_fd(struct session *session)
{
	return session->meta.fd;
}

enum output_format
console_get_output_format()
{
	return current_session()->meta.output_format;
}

void
console_set_output_format(enum output_format output_format)
{
	current_session()->meta.output_format = output_format;
}

/**
 * Dump Lua data to text with respect to output format:
 * YAML document tagged with !push! global tag or Lua string.
 * @param L Lua state.
 * @param[out] size Size of the result.
 *
 * @retval not NULL Tagged YAML document or Lua text.
 * @retval NULL Error.
 */
static const char *
console_dump_plain(struct lua_State *L, uint32_t *size)
{
	enum output_format fmt = console_get_output_format();
	if (fmt == OUTPUT_FORMAT_YAML) {
		int rc = lua_yaml_encode(L, serializer_yaml, "!push!",
					 "tag:tarantool.io/push,2018");
		if (rc == 2) {
			/*
			 * Nil and error object are pushed onto the stack.
			 */
			assert(lua_isnil(L, -2));
			assert(lua_isstring(L, -1));
			diag_set(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
			return NULL;
		}
		assert(rc == 1);
	} else {
		assert(fmt == OUTPUT_FORMAT_LUA_LINE ||
		       fmt == OUTPUT_FORMAT_LUA_BLOCK);
		luaL_findtable(L, LUA_GLOBALSINDEX, "box.internal", 1);
		lua_getfield(L, -1, "format_lua_push");
		lua_pushvalue(L, -3);
		if (lua_pcall(L, 1, 1, 0) != 0) {
			diag_set(LuajitError, lua_tostring(L, -1));
			return NULL;
		}
	}
	assert(lua_isstring(L, -1));
	size_t len;
	const char *result = lua_tolstring(L, -1, &len);
	*size = (uint32_t) len;
	return result;
}

/** Plain text converter for port Lua data. */
const char *
port_lua_dump_plain(struct port *base, uint32_t *size)
{
	return console_dump_plain(((struct port_lua *)base)->L, size);
}

/**
 * A helper for port_msgpack_dump_plain() to execute it safely
 * regarding Lua errors.
 */
static int
port_msgpack_dump_plain_via_lua(struct lua_State *L)
{
	void **ctx = (void **)lua_touserdata(L, 1);
	struct port_msgpack *port = (struct port_msgpack *)ctx[0];
	uint32_t *size = (uint32_t *)ctx[1];
	const char *data = port->data;
	/*
	 * Need to pop, because YAML decoder will consume all what
	 * can be found on the stack.
	 */
	lua_pop(L, 1);
	/*
	 * MessagePack -> Lua object -> YAML/Lua text. The middle
	 * is not really needed here, but there is no
	 * MessagePack -> YAML encoder yet. Neither
	 * MessagePack -> Lua text.
	 */
	luamp_decode(L, luaL_msgpack_default, &data);
	data = console_dump_plain(L, size);
	if (data == NULL) {
		assert(port->plain == NULL);
	} else {
		/*
		 * Result is ignored, because in case of an error
		 * port->plain will stay NULL. And it will be
		 * returned by port_msgpack_dump_plain() as is.
		 */
		port_msgpack_set_plain((struct port *)port, data, *size);
	}
	return 0;
 }

/** Plain text converter for raw MessagePack. */
const char *
port_msgpack_dump_plain(struct port *base, uint32_t *size)
{
	struct lua_State *L = tarantool_L;
	void *ctx[2] = {(void *)base, (void *)size};
	/*
	 * lua_cpcall() protects from errors thrown from Lua which
	 * may break a caller, not knowing about Lua and not
	 * expecting any exceptions.
	 */
	if (lua_cpcall(L, port_msgpack_dump_plain_via_lua, ctx) != 0) {
		/*
		 * Error string is pushed in case it was a Lua
		 * error.
		 */
		assert(lua_isstring(L, -1));
		diag_set(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
		lua_pop(L, 1);
		return NULL;
	}
	/*
	 * If there was an error, port->plain stayed NULL with
	 * installed diag.
	 */
	return ((struct port_msgpack *)base)->plain;
}

/**
 * Push a tagged YAML document or a Lua string into a console
 * socket.
 * @param session Console session.
 * @param port Port with the data to push.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
static int
console_session_push(struct session *session, struct port *port)
{
	assert(session_vtab_registry[session->type].push ==
	       console_session_push);
	uint32_t text_len;
	const char *text = port_dump_plain(port, &text_len);
	if (text == NULL)
		return -1;
	struct iostream io;
	plain_iostream_create(&io, session_fd(session));
	int ret = coio_write_timeout(&io, text, text_len, TIMEOUT_INFINITY);
	iostream_destroy(&io);
	return ret >= 0 ? 0 : -1;
}

void
tarantool_lua_console_init(struct lua_State *L)
{
	static const struct luaL_Reg consolelib[] = {
		{"load_history",	lbox_console_load_history},
		{"save_history",	lbox_console_save_history},
		{"add_history",		lbox_console_add_history},
		{"completion_handler",	lbox_console_completion_handler},
		{"format_yaml",		lbox_console_format_yaml},
		{"format_lua",		lbox_console_format_lua},
		{"run_on_eval",		lbox_console_run_on_eval},
		{NULL, NULL}
	};
	luaT_newmodule(L, "console.lib", consolelib);

	/* readline() func needs a ref to completion_handler (in upvalue) */
	lua_getfield(L, -1, "completion_handler");
	lua_pushcclosure(L, lbox_console_readline, 1);
	lua_setfield(L, -2, "readline");

	/* Readline setup that provides timestamps and multiline history. */
	history_comment_char = '#';
	history_write_timestamps = 1;

	/*
	 * Force disable readline bracketed paste in console, even if it's
	 * set in the inputrc, is enabled by default (eg GNU Readline 8.1),
	 * or by user.
	 */
	rl_variable_bind("enable-bracketed-paste", "off");

	serializer_yaml = lua_yaml_new_serializer(L);
	serializer_yaml->encode_invalid_numbers = 1;
	serializer_yaml->encode_load_metatables = 1;
	serializer_yaml->encode_use_tostring = 1;
	serializer_yaml->encode_invalid_as_nil = 1;
	/*
	 * Hold reference to the formatter in module local
	 * variable.
	 *
	 * This member is not visible to a user, because
	 * console.lua modifies itself, holding the formatter in
	 * module local variable. Add_history, save_history,
	 * load_history work the same way.
	 */
	lua_setfield(L, -2, "formatter");

	/*
	 * We don't export it as a module
	 * for a while, so the library
	 * is kept empty.
	 */
	static const luaL_Reg lualib[] = {
		{ .name = NULL },
	};

	serializer_lua = luaL_newserializer(L, NULL, lualib);
	serializer_lua->has_compact		= 1;
	serializer_lua->encode_invalid_numbers	= 1;
	serializer_lua->encode_load_metatables	= 1;
	serializer_lua->encode_use_tostring	= 1;
	serializer_lua->encode_invalid_as_nil	= 1;

	/*
	 * Keep a reference to this module so it
	 * won't be unloaded.
	 */
	lua_setfield(L, -2, "formatter_lua");

	/* Output formatter in Lua mode */
	lua_serializer_init(L);

	struct session_vtab console_session_vtab = {
		.push	= console_session_push,
		.fd	= console_session_fd,
		.sync	= generic_session_sync,
	};
	session_vtab_registry[SESSION_TYPE_CONSOLE] = console_session_vtab;
	session_vtab_registry[SESSION_TYPE_REPL] = console_session_vtab;

	lua_pushcfunction(L, lbox_console_hide_prompt);
	console_hide_prompt_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushcfunction(L, lbox_console_show_prompt);
	console_show_prompt_ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

/*
 * Completion engine from "Mike Paul's advanced readline patch".
 * With minor fixes and code style tweaks.
 */
#define lua_pushglobaltable(L) lua_pushvalue(L, LUA_GLOBALSINDEX)

enum {
	/*
	 * Suggest a keyword if a prefix of KEYWORD_MATCH_MIN
	 * characters or more was entered.
	 */
	KEYWORD_MATCH_MIN = 1,
	/*
	 * Metatables are consulted recursively when learning items;
	 * avoid infinite metatable loops.
	 */
	METATABLE_RECURSION_MAX = 20,
	/*
	 * Extracting all items matching a given prefix is O(n);
	 * stop once that many items were considered.
	 */
	ITEMS_CHECKED_MAX = 500
};

/* goto intentionally omited */
static const char *
const lua_rl_keywords[] = {
	"and", "break", "do", "else", "elseif", "end", "false",
	"for", "function", "if", "in", "local", "nil", "not", "or",
	"repeat", "return", "then", "true", "until", "while", NULL
};

static int
valid_identifier(const char *s)
{
	if (!(isalpha(*s) || *s == '_')) return 0;
	for (s++; *s; s++)
		if (!(isalpha(*s) || isdigit(*s) || *s == '_')) return 0;
	return 1;
}

/*
 * Dynamically resizable match list.
 * Readline consumes argv-style string list; both the list itself and
 * individual strings should be malloc-ed; readline is responsible for
 * releasing them once done. Item #0 is the longest common prefix
 * (inited last). Idx is the last index assigned (i.e. len - 1.)
 */
typedef struct {
	char **list;
	size_t idx, allocated, matchlen;
} dmlist;

static void
lua_rl_dmfree(dmlist *ml)
{
	if (ml->list == NULL)
		return;
	/*
	 * Note: item #0 isn't initialized until the very end of
	 * lua_rl_complete, the only function calling dmfree().
	 */
	for (size_t i = 1; i <= ml->idx; i++) {
		free(ml->list[i]);
	}
	free(ml->list);
	ml->list = NULL;
}

/* Add prefix + string + suffix to list and compute common prefix. */
static int
lua_rl_dmadd(dmlist *ml, const char *p, size_t pn, const char *s, int suf)
{
	char *t = NULL;

	if (ml->idx+1 >= ml->allocated) {
		char **new_list;
		new_list = realloc(
			ml->list, sizeof(char *)*(ml->allocated += 32));
		if (!new_list)
			return -1;
		ml->list = new_list;
	}

	if (s) {
		size_t n = strlen(s);
		if (!(t = (char *)malloc(sizeof(char)*(pn + n + 2))))
			return 1;
		memcpy(t, p, pn);
		memcpy(t + pn, s, n);
		n += pn;
		t[n] = suf;
		if (suf) t[++n] = '\0';

		if (ml->idx == 0) {
			ml->matchlen = n;
		} else {
			size_t i;
			for (i = 0; i < ml->matchlen && i < n &&
			     ml->list[1][i] == t[i]; i++) ;
			/* Set matchlen to common prefix. */
			ml->matchlen = i;
		}
	}

	ml->list[++ml->idx] = t;
	return 0;
}

/*
 * Get table from __autocomplete function if it's present
 * Use __index field of metatable of object as a fallback
 */
static int
lua_rl_getcompletion(lua_State *L)
{
	if (!lua_getmetatable(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	/* use __autocomplete metamethod if it's present */
	lua_pushstring(L, "__autocomplete");
	lua_rawget(L, -2);
	if (lua_isfunction(L, -1)) {
		lua_replace(L, -2);
		lua_insert(L, -2);
		if (lua_pcall(L, 1, 1, 0) != 0) {
			/* pcall returns an error to the stack */
			lua_pop(L, 1);
			return 0;
		} else {
			return 1;
		}
	} else {
		lua_pop(L, 1);
	}
	lua_pushstring(L, "__index");
	lua_rawget(L, -2);
	lua_replace(L, -2);
	if (lua_isnil(L, -1) || lua_rawequal(L, -1, -2)) {
		lua_pop(L, 2);
		return 0;
	}
	lua_replace(L, -2);
	return 1;
}	 /* 1: obj -- val, 0: obj -- */

/* Get field from object on top of stack. Avoid calling metamethods. */
static int
lua_rl_getfield(lua_State *L, const char *s, size_t n)
{
	int loop = METATABLE_RECURSION_MAX;
	do {
		if (lua_istable(L, -1)) {
			lua_pushlstring(L, s, n);
			lua_rawget(L, -2);
			if (!lua_isnil(L, -1)) {
				lua_replace(L, -2);
				return 1;
			}
			lua_pop(L, 1);
		}
		if (--loop == 0) {
			lua_pop(L, 1);
			return 0;
		}
	} while (lua_rl_getcompletion(L));
	return 0;
}	 /* 1: obj -- val, 0: obj -- */

static char **
lua_rl_complete(lua_State *L, const char *text, int start, int end)
{
	dmlist ml;
	const char *s;
	size_t i, n, dot, items_checked;
	int loop, savetop, is_method_ref = 0;

	if (!(text[0] == '\0' || isalpha(text[0]) || text[0] == '_'))
		return NULL;

	ml.list = NULL;
	ml.idx = ml.allocated = ml.matchlen = 0;

	savetop = lua_gettop(L);
	lua_pushglobaltable(L);
	for (n = (size_t)(end-start), i = dot = 0; i < n; i++) {
		if (text[i] == '.' || text[i] == ':') {
			is_method_ref = (text[i] == ':');
			if (!lua_rl_getfield(L, text+dot, i-dot))
				goto error; /* Invalid prefix. */
			dot = i+1;
			/* Points to first char after dot/colon. */
		}
	}

	/* Add all matches against keywords if there is no dot/colon. */
	if (dot == 0) {
		for (i = 0; (s = lua_rl_keywords[i]) != NULL; i++) {
			if (n >= KEYWORD_MATCH_MIN &&
			    !strncmp(s, text, n) &&
			    lua_rl_dmadd(&ml, NULL, 0, s, ' ')) {

				goto error;
			}
		}
	}

	/* Add all valid matches from all tables/metatables. */
	loop = 0;
	items_checked = 0;
	lua_pushglobaltable(L);
	lua_insert(L, -2);
	do {
		if (!lua_istable(L, -1) ||
		    (loop != 0 && lua_rawequal(L, -1, -2)))
			continue;

		for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {

			/* Beware huge tables */
			if (++items_checked > ITEMS_CHECKED_MAX)
				break;

			if (lua_type(L, -2) != LUA_TSTRING)
				continue;

			s = lua_tostring(L, -2);
			/*
			 * Only match names starting with '_'
			 * if explicitly requested.
			 */
			if (strncmp(s, text+dot, n-dot) ||
			    !valid_identifier(s) ||
			    (*s == '_' && text[dot] != '_')) continue;

			int suf = 0; /* Omit suffix by default. */
			int type = lua_type(L, -1);
			switch (type) {
			case LUA_TTABLE:
			case LUA_TUSERDATA:
				/*
				 * For tables and userdata omit a
				 * suffix, since all variants, i.e.
				 * T, T.field, T:method and T()
				 * are likely valid.
				 */
				break;
			case LUA_TFUNCTION:
				/*
				 * Prepend '(' for a function. This
				 * helps to differentiate functions
				 * visually in completion lists. It is
				 * believed that in interactive console
				 * functions are most often called
				 * rather then assigned to a variable or
				 * passed as a parameter, hence
				 * an ocasional need to delete an
				 * unwanted '(' shouldn't be a burden.
				 */
				suf = '(';
				break;
			}
			/*
			 * If completing a method ref, i.e
			 * foo:meth<TAB>, show functions only.
			 */
			if (!is_method_ref || type == LUA_TFUNCTION) {
				if (lua_rl_dmadd(&ml, text, dot, s, suf))
					goto error;
			}
		}
	} while (++loop < METATABLE_RECURSION_MAX && lua_rl_getcompletion(L));

	lua_pop(L, 1);

	if (ml.idx == 0) {
error:
		lua_rl_dmfree(&ml);
		lua_settop(L, savetop);
		return NULL;
	} else {
		/* list[0] holds the common prefix of all matches (may
		 * be ""). If there is only one match, list[0] and
		 * list[1] will be the same. */
		ml.list[0] = malloc(sizeof(char)*(ml.matchlen+1));
		if (!ml.list[0])
			goto error;
		memcpy(ml.list[0], ml.list[1], ml.matchlen);
		ml.list[0][ml.matchlen] = '\0';
		/* Add the NULL list terminator. */
		if (lua_rl_dmadd(&ml, NULL, 0, NULL, 0)) goto error;
	}

	lua_settop(L, savetop);
	return ml.list;
}
