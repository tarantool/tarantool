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
 * The job of lbox_push_event_f is to push trigger arguments
 * to Lua stack.
 */

typedef int
(*lbox_push_event_f)(struct lua_State *L, void *event);

/**
 * Create a Lua trigger, replace an existing one,
 * or delete a trigger.
 *
 * The function accepts a Lua stack with at least
 * one argument.
 *
 * The argument at the top of the stack defines the old value
 * of the trigger, which serves as a search key if the trigger
 * needs to be updated. If it is not present or is nil, a new
 * trigger is created.
 * The argument just below the top must reference a Lua function
 * or closure for which the trigger needs to be set.
 * If argument below the top is nil, but argument at the top is an
 * existing trigger, it's erased.
 *
 * An existing trigger is searched on the 'list' by checking
 * trigger->data of all triggers on the list which have the same
 * trigger->run function as passed in in 'run' argument.
 *
 * When a new trigger is set, the function passed in the first
 * value on Lua stack is referenced, and the reference is saved
 * in trigger->data (if an old trigger is found it's current Lua
 * function is first dereferenced, the reference is destroyed).
 *
 * @param top defines the top of the stack. If the actual
 *        lua_gettop(L) is less than 'top', the stack is filled
 *        with nils (this allows the second argument to be
 *        optional).
 */
int
lbox_trigger_reset(struct lua_State *L, int top,
		   struct rlist *list, lbox_push_event_f push_f);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_LUA_TRIGGER_H */
