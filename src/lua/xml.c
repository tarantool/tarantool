/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lua/xml.h"

#include <assert.h>
#include <ctype.h>
#include <lua.h>
#include <lauxlib.h>
#include <stddef.h>
#include <string.h>

#include "lua/utils.h"

int
luaT_xml_decode(struct lua_State *L)
{
	/* The input string must be at the top of the stack. */
	int top = lua_gettop(L);
	if (top < 1 || lua_type(L, top) != LUA_TSTRING)
		return luaL_error(L, "expected string");
	const char *s = lua_tostring(L, top);
	assert(s != NULL);
	/* Push the document table. */
	lua_newtable(L);
	enum {
		ELEM,
		TAG,
		START_TAG,
		END_TAG,
		SPACE_AFTER_END_TAG,
		ATTR,
		ATTR_NAME,
		ATTR_VALUE_SEP,
		ATTR_VALUE_BEGIN,
		ATTR_VALUE,
		ATTR_VALUE_END,
		ELEM_END,
		DOC_END,
	} state = ELEM;
	const char *tag_name = NULL;
	const char *attr_name = NULL;
	const char *attr_value = NULL;
	/*
	 * Position of the current character in the input string recorded
	 * as line and column numbers. Used for error reporting.
	 */
	int line = 1;
	int column = 1;
#define ERROR(msg) \
	luaL_error(L, "XML decode error at line %d, column %d: %s", \
		   line, column, (msg))
	while (*s != '\0') {
		/*
		 * State machine:
		 *  - 'break' skips the current character by leaving
		 *    the switch-case.
		 *  - 'continue' rechecks the state without skipping
		 *    the current character.
		 */
		switch (state) {
		/*
		 * Expected an element. Currently, only tags are supported
		 * (<section> or </section>) while values enclosed in tags are
		 * considered invalid. Skip optional spaces and '<', then
		 * transition to TAG.
		 *
		 * TODO: Handle values, such as <section>value</section>.
		 */
		case ELEM:
			if (isspace(*s))
				break; /* skip spaces */
			if (*s != '<')
				return ERROR("invalid token");
			state = TAG;
			break; /* skip '<' */
		/*
		 * If the current character is '/', skip it and transition to
		 * END_TAG, otherwise transition to START_TAG.
		 */
		case TAG:
			if (*s != '/') {
				tag_name = s;
				state = START_TAG;
				continue;
			}
			if (lua_gettop(L) == top + 1) {
				/*
				 * The stack only contains the resulting
				 * document table, which means there's no
				 * start tag matching this end tag.
				 */
				return ERROR("invalid token");
			}
			tag_name = s + 1;
			state = END_TAG;
			break; /* skip '/' */
		/*
		 * Skip the start tag name, create a table for the tag
		 * attributes and child elements, then transition to ATTR.
		 */
		case START_TAG: {
			assert(tag_name != NULL);
			/* TODO: Check names according to XML standard. */
			if (isalnum(*s))
				break; /* skip the tag name */
			if ((!isspace(*s) && *s != '/' && *s != '>') ||
			    s == tag_name)
				return ERROR("invalid token");
			/* Stack: parent table, ... */
			assert(lua_gettop(L) >= top + 1);
			assert(lua_istable(L, -1));
			lua_pushlstring(L, tag_name, s - tag_name);
			/*
			 * Check that there's no attributes with the same name
			 * in the parent table. A table is accepted because
			 * there may be more than one element with the same
			 * name (elements are stored in an array).
			 */
			lua_pushvalue(L, -1); /* tag name */
			lua_rawget(L, -3);
			int type = lua_type(L, -1);
			if (type != LUA_TNIL && type != LUA_TTABLE)
				return ERROR("duplicate name");
			/*
			 * Create an array entry in the parent table under this
			 * name if it doesn't exist.
			 */
			if (type == LUA_TNIL) {
				lua_pop(L, 1);
				lua_newtable(L);
				lua_pushvalue(L, -2); /* tag name */
				lua_pushvalue(L, -2); /* new array */
				lua_rawset(L, -5);
			}
			assert(lua_istable(L, -1));
			int len = lua_objlen(L, -1);
			/*
			 * Create a new attribute table for this element and
			 * append it to the array. Then pop the array leaving
			 * the attribute table and the new tag name at the top
			 * of the stack.
			 */
			lua_newtable(L);
			lua_pushvalue(L, -1); /* attribute table */
			lua_rawseti(L, -3, len + 1);
			lua_replace(L, -2);
			state = ATTR;
			continue;
		}
		/*
		 * Skip the end tag name, check it against the corresponding
		 * start tag name, and transition to SPACE_AFTER_END_TAG.
		 */
		case END_TAG: {
			assert(tag_name != NULL);
			if (isalnum(*s))
				break; /* skip the tag name */
			if ((!isspace(*s) && *s != '>') ||
			    s == tag_name)
				return ERROR("invalid token");
			/* Stack: attr table, tag name, parent table, ... */
			assert(lua_gettop(L) >= top + 3);
			/* Check that the start and end tag names match. */
			size_t len;
			const char *expected = lua_tolstring(L, -2, &len);
			assert(expected != NULL);
			if ((size_t)(s - tag_name) != len ||
			    strncmp(tag_name, expected, len) != 0)
				return ERROR("mismatched tag");
			state = SPACE_AFTER_END_TAG;
			continue;
		}
		/*
		 * Skip optional spaces after the end tag name and transition
		 * to ELEM_END.
		 */
		case SPACE_AFTER_END_TAG:
			if (isspace(*s))
				break; /* skip spaces */
			state = ELEM_END;
			continue;
		/*
		 * Skip optional spaces then transition to ELEM, ELEM_END, or
		 * ATTR_NAME, depending on the current character.
		 */
		case ATTR:
			if (isspace(*s))
				break; /* skip spaces */
			if (*s == '/') {
				state = ELEM_END;
				break; /* skip '/' */
			} else if (*s == '>') {
				state = ELEM;
				break; /* skip '>' */
			}
			attr_name = s;
			state = ATTR_NAME;
			continue;
		/*
		 * Skip the attribute name and transition to ATTR_VALUE_SEP.
		 */
		case ATTR_NAME:
			assert(attr_name != NULL);
			/* TODO: Check names according to XML standard. */
			if (isalnum(*s))
				break; /* skip the attribute name */
			if ((!isspace(*s) && *s != '=') ||
			    s == attr_name)
				return ERROR("invalid token");
			/* Stack: attr table, tag name, parent table, ... */
			assert(lua_gettop(L) >= top + 3);
			lua_pushlstring(L, attr_name, s - attr_name);
			/* Check that there's no duplicate attributes. */
			lua_pushvalue(L, -1); /* attribute name */
			lua_rawget(L, -3);
			if (!lua_isnil(L, -1))
				return ERROR("duplicate name");
			lua_pop(L, 1);
			state = ATTR_VALUE_SEP;
			continue;
		/*
		 * Skip optional spaces and '=' separating the attribute value
		 * from the name, then transition to ATTR_VALUE_BEGIN.
		 */
		case ATTR_VALUE_SEP:
			if (isspace(*s))
				break; /* skip spaces */
			if (*s != '=')
				return ERROR("invalid token");
			state = ATTR_VALUE_BEGIN;
			break; /* skip '=' */
		/*
		 * Skip optional spaces and '"' preceding the attribute value,
		 * then transition to ATTR_VALUE.
		 */
		case ATTR_VALUE_BEGIN:
			if (isspace(*s))
				break; /* skip spaces */
			if (*s != '"')
				return ERROR("invalid token");
			attr_value = s + 1;
			state = ATTR_VALUE;
			break; /* skip '"' */
		/*
		 * Skip until and including '"' following the attribute value,
		 * insert the new attribute to the attribute table, then
		 * transition to ATTR_VALUE_END.
		 */
		case ATTR_VALUE:
			assert(attr_value != NULL);
			/* TODO: Handle escaped quotation marks. */
			if (*s != '"')
				break; /* skip until '"' */
			/*
			 * Stack: attr name, attr table, tag name,
			 *        parent table, ...
			 */
			assert(lua_gettop(L) >= top + 4);
			/* Insert the new attribute into the table. */
			lua_pushlstring(L, attr_value, s - attr_value);
			lua_rawset(L, -3);
			state = ATTR_VALUE_END;
			break; /* skip '"' */
		/*
		 * Check that the attribute value is followed by a valid token,
		 * then transition to ATTR.
		 */
		case ATTR_VALUE_END:
			if (!isspace(*s) && *s != '/' && *s != '>')
				return ERROR("invalid token");
			state = ATTR;
			continue;
		/*
		 * Skip '>' terminating the current element, pop the attribute
		 * table and the tag name created for the element from the
		 * stack, then transition to ELEM or DOC_END, depending on
		 * whether we expect more elements.
		 */
		case ELEM_END:
			if (*s != '>')
				return ERROR("invalid token");
			/* Stack: attr table, tag name, parent table, ... */
			assert(lua_gettop(L) >= top + 3);
			lua_pop(L, 2);
			state = lua_gettop(L) == top + 1 ? DOC_END : ELEM;
			break; /* skip '>' */
		/*
		 * End of document. No input except for spaces is expected in
		 * this state.
		 */
		case DOC_END:
			if (isspace(*s))
				break; /* skip spaces */
			return ERROR("junk after document");
		}
		if (*s == '\n') {
			line++;
			column = 1;
		} else {
			column++;
		}
		s++;
	}
	if (state != DOC_END)
		return ERROR("truncated document");
#undef ERROR
	/* Replace the input string with the document table. */
	lua_replace(L, -2);
	return 1;
}

void
tarantool_lua_xml_init(struct lua_State *L)
{
	const struct luaL_Reg module_funcs[] = {
		{"decode", luaT_xml_decode},
		{NULL, NULL},
	};
	luaT_newmodule(L, "internal.xml", module_funcs);
	lua_pop(L, 1);
}
