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
#include "lua/utils.h"
#include "lua/fiber.h"
#include "fiber.h"
#include "coio.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdlib.h>
#include <ctype.h>

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
 */
static void
console_push_line(char *line)
{
	/* XXX pushnil/pushstring may err */
	if (line == NULL)
		lua_pushnil(readline_L);
	else
		lua_pushstring(readline_L, line);

#ifdef HAVE_GNU_READLINE
	/*
	 * This is to avoid a stray prompt on the next line with GNU
	 * readline. Interestingly, it botches the terminal when
	 * attempted with libeditline.
	 */
	rl_callback_handler_install(NULL, NULL);
#endif
}

/* implements readline() Lua API */
static int
lbox_console_readline(struct lua_State *L)
{
	const char *prompt = NULL;
	int top;
	int completion = 0;

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
				 TIMEOUT_INFINITY) == 0);

		rl_callback_read_char();
	}

	readline_L = NULL;
	/* Incidents happen. */
#pragma GCC poison readline_L
	rl_attempted_completion_function = NULL;
	luaL_testcancel(L);
	return 1;
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

void
tarantool_lua_console_init(struct lua_State *L)
{
	static const struct luaL_Reg consolelib[] = {
		{"load_history",       lbox_console_load_history},
		{"save_history",       lbox_console_save_history},
		{"add_history",        lbox_console_add_history},
		{"completion_handler", lbox_console_completion_handler},
		{NULL, NULL}
	};
	luaL_register_module(L, "console", consolelib);

	/* readline() func needs a ref to completion_handler (in upvalue) */
	lua_getfield(L, -1, "completion_handler");
	lua_pushcclosure(L, lbox_console_readline, 1);
	lua_setfield(L, -2, "readline");
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

/* Get __index field of metatable of object on top of stack. */
static int
lua_rl_getmetaindex(lua_State *L)
{
	if (!lua_getmetatable(L, -1)) {
		lua_pop(L, 1);
		return 0;
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
	} while (lua_rl_getmetaindex(L));
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
	} while (++loop < METATABLE_RECURSION_MAX && lua_rl_getmetaindex(L));

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
