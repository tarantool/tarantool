#ifndef INCLUDES_TARANTOOL_LUA_TRIGGER_H
#define INCLUDES_TARANTOOL_LUA_TRIGGER_H
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include <trigger.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */
struct lua_State;

/**
 * If not NULL, lbox_push_event_f will be called before execution
 * of the trigger callback. It's supposed to push trigger arguments
 * to Lua stack and return the number of pushed values on success.
 * On error, it should set diag and return a negative number.
 */
typedef int
(*lbox_push_event_f)(struct lua_State *L, void *event);

/**
 * If not NULL, lbox_pop_event_f will be called after successful
 * execution of the trigger callback. It can be used to parse the
 * return value of the trigger callback and update the 'event'
 * accordingly. If this function returns a non-zero value, an
 * error will be raised for the caller.
 */
typedef int
(*lbox_pop_event_f)(struct lua_State *L, int nret, void *event);

/**
 * Creates a Lua trigger, replaces an existing one, or deletes a trigger.
 *
 * The function accepts a Lua stack. Values starting from index bottom are
 * considered as the function arguments.
 *
 * The function supports two API versions.
 *
 * The first version - key-value arguments. The function is called with one Lua
 * argument which is not callable table. In this case, the table must contain
 * key "name" with value of type string - the name of a trigger. The second
 * key, "func", is optional. If it is not present, a trigger with passed name
 * is deleted, or no-op, if there is no such trigger. If key "func" is present,
 * it must contain a callable object as a value - it will be used as a handler
 * for a new trigger. The new trigger will be appended to the beginning of the
 * trigger list or replace an existing one with the same name. The function
 * returns new trigger (or nothing, if it was deleted).
 *
 * The second version - positional arguments. The function is called with up to
 * three Lua arguments. The first one is a new trigger handler - it must be a
 * callable object or nil. The second one is an old trigger handler - it must
 * be a callable object or nil as well. The third argument is a trigger name of
 * type string (it can be nil too).
 * If the name is passed, the logic is equivalent to key-value API -
 * the third argument is a trigger name, the first one is a trigger handler (or
 * nil if the function is called to delete a trigger by name), the second
 * argument is ignored, but the type check is still performed. If the name is
 * not passed, the function mimics the behavior of function lbox_trigger_reset:
 * 1. If triggers (first and second arguments) are not passed, returns table of
 * triggers.
 * 2. If new trigger is passed and old one is not - sets new trigger using
 * its address as name. The new trigger is returned.
 * 3. If old trigger is passed and new trigger is not - deletes a trigger by
 * address of an old trigger as a name. Returns nothing.
 * 4. If both triggers are provided - replace old trigger with new one if they
 * have the same address, delete old trigger and insert new one at the beginning
 * of the trigger list otherwise. The new trigger is returned.
 *
 * Argument push_f pushes arguments from C to Lua stack. Argument pop_f pops
 * returned values from Lua stack to C.
 */
int
lbox_trigger_reset(struct lua_State *L, int bottom, struct rlist *list,
		   lbox_push_event_f push_f, lbox_pop_event_f pop_f);

/**
 * Registers internal trigger list object in Lua.
 */
void
tarantool_lua_trigger_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_LUA_TRIGGER_H */
