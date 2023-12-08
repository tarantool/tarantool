/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/lua/config/utils/expression_lexer.h"

#include <assert.h>
#include <ctype.h>
#include <lua.h>

#include "lua/utils.h"

/**
 * Accepts a string and returns an array-like table of tokens.
 *
 * The following tokens are possible.
 *
 * {
 *     type = 'variable',
 *     value = <string>,
 * }
 *
 * {
 *     type = 'version_literal',
 *     value = <string>,
 * }
 *
 * {
 *     -- The value is one of '>=', '<=', '>', '<', '!=', '==',
 *     -- '&&', '||'.
 *     type = 'operation',
 *     value = <string>,
 * }
 *
 * {
 *     -- The value is '(' or ')'.
 *     type = 'grouping',
 *     value = <string>,
 * }
 */
static int
luaT_expression_lexer_split(struct lua_State *L)
{
	/* The input string must be at the top of the stack. */
	int top = lua_gettop(L);
	if (top < 1 || lua_type(L, top) != LUA_TSTRING)
		return luaL_error(L, "expected string");
	const char *s = lua_tostring(L, top);
	assert(s != NULL);
	/* Push the tokens list. */
	lua_newtable(L);
	enum {
		START,
		VARIABLE,
		VERSION_LITERAL_DIGIT,
		VERSION_LITERAL_FULL_STOP,
		NEEDS_SEPARATOR,
		COMPARE,
		LOGICAL,
		EQUALITY,
	} state = START;
	const char *token_start = NULL;
	int literal_components = 0;
	/*
	 * Position of the current character in the input string
	 * recorded as line and column numbers. Used for error
	 * reporting.
	 */
	int line = 1;
	int column = 1;
#define ERROR(fmt, ...) \
	luaL_error(L, "Expression parsing error at line %d, column %d: " fmt,	\
		   line, column, ##__VA_ARGS__)
#define PUSH_TOKEN(token_type, token_start, token_end) do {		\
	/*								\
	 * Workaround a false-positive integer overflow warning on	\
	 * GCC 4.8.5.							\
	 *								\
	 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61240		\
	 */								\
	const char *start = (token_start);				\
	lua_createtable(L, 0, 2);					\
	lua_pushliteral(L, token_type);					\
	lua_setfield(L, -2, "type");					\
	lua_pushlstring(L, (token_start), (token_end) - start + 1);	\
	lua_setfield(L, -2, "value");					\
	lua_rawseti(L, -2, lua_objlen(L, -2) + 1);			\
} while (0)
	do {
		/*
		 * State machine:
		 *  - 'break' skips the current character by
		 *    leaving the switch-case.
		 *  - 'continue' rechecks the state without
		 *    skipping the current character.
		 */
		switch (state) {
		case START:
			/*
			 * Stop parsing at end of the input
			 * string.
			 */
			if (*s == '\0')
				break;
			/* Skip spaces. */
			if (isspace(*s))
				break;
			/* Variable. */
			if (isalpha(*s) || *s == '_') {
				state = VARIABLE;
				token_start = s;
				break;
			}
			/* Version literal. */
			if (isdigit(*s)) {
				state = VERSION_LITERAL_DIGIT;
				token_start = s;
				literal_components = 1;
				break;
			}
			/* Single character operator. */
			if (*s == '(' || *s == ')') {
				PUSH_TOKEN("grouping", s, s);
				break;
			}
			/* Single or two character operator. */
			if (*s == '>' || *s == '<') {
				state = COMPARE;
				break;
			}
			/* Two character operators. */
			if (*s == '&' || *s == '|') {
				state = LOGICAL;
				break;
			}
			if (*s == '!' || *s == '=') {
				state = EQUALITY;
				break;
			}
			return ERROR("invalid token");
		case VARIABLE:
			/*
			 * Consume a-z, A-Z, 0-9 and underscore.
			 */
			if (isalnum(*s) || *s == '_')
				break;
			/*
			 * The series of the variable characters
			 * has been ended.
			 *
			 * Push the token and verify that the next
			 * character is a separator.
			 */
			PUSH_TOKEN("variable", token_start, s - 1);
			token_start = NULL;
			state = NEEDS_SEPARATOR;
			continue;
		case VERSION_LITERAL_DIGIT:
			/* Consume 0-9. */
			if (isdigit(*s))
				break;
			/* Consume full stop. */
			if (*s == '.') {
				++literal_components;
				state = VERSION_LITERAL_FULL_STOP;
				break;
			}
			/*
			 * The series of digits and full stops has
			 * been ended.
			 *
			 * Verify amount of components in the version
			 * literal.
			 */
			if (literal_components != 3)
				return ERROR("invalid version literal: "
					     "expected 3 components, got %d",
					     literal_components);
			/*
			 * Push the token and verify that the next
			 * character is a separator.
			 */
			PUSH_TOKEN("version_literal", token_start, s - 1);
			token_start = NULL;
			literal_components = 0;
			state = NEEDS_SEPARATOR;
			continue;
		case VERSION_LITERAL_FULL_STOP:
			/* Consume 0-9. */
			if (isdigit(*s)) {
				state = VERSION_LITERAL_DIGIT;
				break;
			}
			/*
			 * Forbid a second full stop in a row.
			 *
			 * Forbid a version literal ending with a
			 * full stop.
			 */
			return ERROR("invalid token");
		case NEEDS_SEPARATOR:
			/*
			 * The end of input, a space, an operator
			 * symbol are the separators.
			 */
			if (*s == '\0' || isspace(*s) ||
			    *s == '(' || *s == ')' ||
			    *s == '>' || *s == '<' ||
			    *s == '&' || *s == '|' ||
			    *s == '!' || *s == '=') {
				state = START;
				continue;
			}
			return ERROR("invalid token");
		case COMPARE:
			/* Push >= or <=. */
			if (*s == '=') {
				PUSH_TOKEN("operation", s - 1, s);
				state = START;
				break;
			}
			/* Push > or <. */
			PUSH_TOKEN("operation", s - 1, s - 1);
			state = START;
			continue;
		case LOGICAL:
			/* &<eof> or |<eof> is an error. */
			if (*s == '\0')
				return ERROR("truncated expression");
			/*
			 * Anything other than && and || is an
			 * error.
			 */
			if (*s != '&' && *s != '|')
				return ERROR("invalid token");
			/*
			 * Including &| and |& -- it is an error
			 * too.
			 */
			if (*(s - 1) != *s)
				return ERROR("invalid token");
			PUSH_TOKEN("operation", s - 1, s);
			state = START;
			break;
		case EQUALITY:
			/* !<eof> or =<eof> is an error. */
			if (*s == '\0')
				return ERROR("truncated expression");
			/*
			 * Anything other than != and == is an
			 * error.
			 */
			if (*s != '=')
				return ERROR("invalid token");
			PUSH_TOKEN("operation", s - 1, s);
			state = START;
			break;
		}
		if (*s == '\0')
			break;
		if (*s == '\n') {
			line++;
			column = 1;
		} else {
			column++;
		}
		++s;
	} while (1);
#undef ERROR
#undef PUSH_TOKEN
	/* Replace the input string with the tokens list. */
	lua_replace(L, -2);
	return 1;
}

void
box_lua_expression_lexer_init(struct lua_State *L)
{
	const struct luaL_Reg module_funcs[] = {
		{"split", luaT_expression_lexer_split},
		{NULL, NULL},
	};
	luaT_newmodule(L, "internal.config.utils.expression_lexer",
		       module_funcs);
	lua_pop(L, 1);
}
