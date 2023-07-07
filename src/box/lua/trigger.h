/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct event;

/**
 * Initializes module trigger.
 */
void
box_lua_trigger_init(struct lua_State *L);

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
 */
int
luaT_event_reset_trigger(struct lua_State *L, int bottom, struct event *event);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
