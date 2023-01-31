#ifndef TARANTOOL_BOX_LUA_KEY_DEF_H_INCLUDED
#define TARANTOOL_BOX_LUA_KEY_DEF_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;
struct key_def;

/**
 * Push a copy of key_def as a cdata object to a Lua stack, and set finalizer
 * function lbox_key_def_gc for it.
 */
void
luaT_push_key_def(struct lua_State *L, const struct key_def *key_def);

/**
 * Push a new table representing a key_def to a Lua stack.
 * Table is consists of key_def::parts tables that describe
 * each part correspondingly.
 * The collation and path fields are optional so resulting
 * object doesn't declare them where not necessary.
 */
void
luaT_push_key_def_parts(struct lua_State *L, const struct key_def *key_def);

/**
 * Check key_def pointer in LUA stack by specified index.
 * The value by idx is expected to be key_def's cdata.
 * Returns not NULL tuple pointer on success, NULL otherwise.
 */
struct key_def *
luaT_is_key_def(struct lua_State *L, int idx);

/**
 * Extract key from tuple by given key definition and return
 * tuple representing this key.
 * Push the new key tuple as cdata to a LUA stack on success.
 * Raise error otherwise.
 */
int
luaT_key_def_extract_key(struct lua_State *L, int idx);

/**
 * Compare tuples using the key definition.
 * Push 0  if key_fields(tuple_a) == key_fields(tuple_b)
 *      <0 if key_fields(tuple_a) < key_fields(tuple_b)
 *      >0 if key_fields(tuple_a) > key_fields(tuple_b)
 * integer to a LUA stack on success.
 * Raise error otherwise.
 */
int
luaT_key_def_compare(struct lua_State *L, int idx);

/**
 * Compare tuple with key using the key definition.
 * Push 0  if key_fields(tuple) == parts(key)
 *      <0 if key_fields(tuple) < parts(key)
 *      >0 if key_fields(tuple) > parts(key)
 * integer to a LUA stack on success.
 * Raise error otherwise.
 */
int
luaT_key_def_compare_with_key(struct lua_State *L, int idx);

/**
 * Construct and export to LUA a new key definition with a set
 * union of key parts from first and second key defs. Parts of
 * the new key_def consist of the first key_def's parts and those
 * parts of the second key_def that were not among the first
 * parts.
 * Push the new key_def as cdata to a LUA stack on success.
 * Raise error otherwise.
 */
int
luaT_key_def_merge(struct lua_State *L, int idx_a, int idx_b);

/**
 * Create a new key_def from a Lua table.
 *
 * Expected a table of key parts on the Lua stack. The format is
 * the same as box.space.<...>.index.<...>.parts or corresponding
 * net.box's one.
 *
 * Push the new key_def as cdata to a Lua stack.
 */
int
lbox_key_def_new(struct lua_State *L);

/**
 * Register the module.
 */
int
luaopen_key_def(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_LUA_KEY_DEF_H_INCLUDED */
