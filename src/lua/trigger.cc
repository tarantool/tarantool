/*
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
#include "lua/trigger.h"
#include "lua/utils.h"

void
lbox_trigger_destroy(struct trigger *trigger)
{
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, (intptr_t) trigger->data);
	free(trigger);
}


struct trigger *
lbox_trigger_find(struct lua_State *L, int index,
		  struct rlist *list, trigger_f run)
{
	struct trigger *trigger;
	/** Find the old trigger, if any. */
	rlist_foreach_entry(trigger, list, link) {
		if (trigger->run == run) {
			lua_rawgeti(L, LUA_REGISTRYINDEX,
				    (intptr_t) trigger->data);
			bool found = lua_equal(L, index, lua_gettop(L));
			lua_pop(L, 1);
			if (found)
				return trigger;
		}
	}
	return NULL;
}

void
lbox_trigger_check_input(struct lua_State *L, int top)
{
	assert(lua_checkstack(L, top));
	/* Push optional arguments. */
	while (lua_gettop(L) < top)
		lua_pushnil(L);
	/* (nil, function) is OK,
	 * (function, nil), is OK,
	 * (function, function) is OK,
	 * anything else is error.
	 */
	if ((lua_isfunction(L, top) && lua_isnil(L, top - 1)) ||
	    (lua_isnil(L, top) && lua_isfunction(L, top - 1)) ||
	    (lua_isfunction(L, top) && lua_isfunction(L, top - 1)))
		return;

	luaL_error(L, "trigger reset: incorrect arguments");
}

int
lbox_trigger_reset(struct lua_State *L, int top,
		   struct rlist *list, trigger_f run)
{
	lbox_trigger_check_input(L, top);

	struct trigger *trg = lbox_trigger_find(L, top, list, run);

	if (trg) {
		luaL_unref(L, LUA_REGISTRYINDEX, (intptr_t) trg->data);

	} else if (lua_isfunction(L, top)) {
		tnt_raise(ClientError, ER_NO_SUCH_TRIGGER);
	}
	/*
	 * During update of a trigger, we must preserve its
	 * relative position in the list.
	 */
	if (lua_isfunction(L, top - 1)) {
		if (trg == NULL) {
			trg = (struct trigger *) malloc(sizeof(*trg));
			if (trg == NULL)
				luaL_error(L, "failed to allocate trigger");
			trg->run = run;
			trg->destroy = lbox_trigger_destroy;
			trigger_add(list, trg);
		}
		/*
		 * Make the new trigger occupy the top
		 * slot of the Lua stack.
		 */
		lua_pop(L, 1);
		/* Reference. */
		trg->data = (void *) (intptr_t)
			luaL_ref(L, LUA_REGISTRYINDEX);

	} else {
		trigger_clear(trg);
		free(trg);
	}
	return 0;
}
