/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;

/**
 * Takes a string that is supposed to contain a valid XML document, decodes it,
 * and replaces the string with a Lua table representation of the XML document.
 * Raises a Lua error on failure. On success returns 1.
 *
 * Each XML element (including an input document) is represented by a Lua table.
 * An attribute is stored in the table as a string keyed by the attribute name
 * while a sub-element is stored in an array keyed by the sub-element tag.
 *
 * For example, the following document
 *
 *   <section version="1">
 *     <element value="foo"/>
 *     <element value="bar"/>
 *   </section>
 *
 * will be transformed to
 *
 *   {
 *     section = {
 *       [1] = {
 *         version = '1',
 *         element = {
 *           [1] = {value = 'foo'},
 *           [2] = {value = 'bar'},
 *         }
 *       }
 *     }
 *   }
 *
 * Spaces and new lines in the input string are ignored.
 *
 * Limitations:
 *  - Element values, such as <section>value</section>, aren't supported.
 *  - Escape sequences in attribute values aren't supported.
 *  - Tag and attribute names aren't checked according to the XML standard.
 *    The parser allows only digits and letters while in XML a name may also
 *    contain dots, dashes, and underscores, and must start with a letter or
 *    an underscore.
 */
int
luaT_xml_decode(struct lua_State *L);

void
tarantool_lua_xml_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
