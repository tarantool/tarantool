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
#include "lua/fiber.h"

#include <fiber.h>
#include "lua/utils.h"
#include "lua/serializer.h"
#include "lua/backtrace.h"
#include "tt_static.h"

#include <lua.h>
#include <lauxlib.h>

static_assert(FIBER_LUA_NOREF == LUA_NOREF, "FIBER_LUA_NOREF is ok");

void
luaL_testcancel(struct lua_State *L)
{
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		luaT_error(L);
	}
}

/* {{{ fiber Lua library: access to Tarantool fibers
 *
 * Each fiber can be running, suspended or dead.
 * When a fiber is created (fiber.create()) it's
 * running.
 *
 * All fibers are part of the fiber registry, fiber.
 * This registry can be searched either by
 * fiber id (fid), which is numeric, or by fiber name,
 * which is a string. If there is more than one
 * fiber with the given name, the first fiber that
 * matches is returned.
 *
 * Once fiber chunk is done or calls "return",
 * the fiber is considered dead. Its carcass is put into
 * fiber pool, and can be reused when another fiber is
 * created.
 *
 * A runaway fiber can be stopped with fiber.cancel().
 * fiber.cancel(), however, is advisory -- it works
 * only if the runaway fiber is calling fiber.testcancel()
 * once in a while. Most box.* hooks, such as box.delete()
 * or box.update(), are calling fiber.testcancel().
 *
 * Thus a runaway fiber can really only become cuckoo
 * if it does a lot of computations and doesn't check
 * whether it's been cancelled (just don't do that).
 *
 * The other potential problem comes from
 * fibers which never get scheduled, because are subscribed
 * to or get no events. Such morphing fibers can be killed
 * with fiber.cancel(), since fiber.cancel()
 * sends an asynchronous wakeup event to the fiber.
 */

static const char *fiberlib_name = "fiber";

/**
 * Trigger invoked when the fiber has stopped execution of its
 * current request. Only purpose - delete storage.lua.fid_ref and
 * storage.lua.storage_ref keeping a reference of Lua
 * fiber and fiber.storage objects. Unlike Lua stack,
 * Lua fiber storage may be created not only for fibers born from
 * Lua land. For example, an IProto request may execute a Lua
 * function, which can create the storage. Trigger guarantees,
 * that even for non-Lua fibers the Lua storage is destroyed.
 */
static int
lbox_fiber_on_stop(struct trigger *trigger, void *event)
{
	struct fiber *f = event;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, f->storage.lua.storage_ref);
	f->storage.lua.storage_ref = FIBER_LUA_NOREF;
	luaL_unref(tarantool_L, LUA_REGISTRYINDEX, f->storage.lua.fid_ref);
	f->storage.lua.fid_ref = FIBER_LUA_NOREF;
	trigger_clear(trigger);
	free(trigger);
	return 0;
}

/**
 * Push a userdata for the given fiber onto Lua stack.
 */
static void
lbox_pushfiber(struct lua_State *L, struct fiber *f)
{
	int fid_ref = f->storage.lua.fid_ref;
	if (fid_ref == FIBER_LUA_NOREF) {
		struct trigger *t = malloc(sizeof(*t));
		if (t == NULL) {
			diag_set(OutOfMemory, sizeof(*t), "malloc", "t");
			luaT_error(L);
		}
		trigger_create(t, lbox_fiber_on_stop, NULL, (trigger_f0)free);
		trigger_add(&f->on_stop, t);

		uint64_t fid = f->fid;
		/* create a new userdata */
		uint64_t *ptr = lua_newuserdata(L, sizeof(*ptr));
		*ptr = fid;
		luaL_getmetatable(L, fiberlib_name);
		lua_setmetatable(L, -2);
		fid_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		f->storage.lua.fid_ref = fid_ref;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, fid_ref);
}

static struct fiber *
lbox_checkfiber(struct lua_State *L, int index)
{
	uint64_t fid;
	if (lua_type(L, index) == LUA_TNUMBER) {
		fid = luaL_touint64(L, index);
	} else {
		fid = *(uint64_t *)luaL_checkudata(L, index, fiberlib_name);
	}
	struct fiber *f = fiber_find(fid);
	if (f == NULL)
		luaL_error(L, "the fiber is dead");
	return f;
}

static int
lbox_fiber_id(struct lua_State *L)
{
	uint64_t fid;
	if (lua_gettop(L)  == 0)
		fid = fiber()->fid;
	else
		fid = *(uint64_t *)luaL_checkudata(L, 1, fiberlib_name);
	luaL_pushuint64(L, fid);
	return 1;
}

static int
lbox_fiber_statof_map(struct fiber *f, void *cb_ctx, bool backtrace)
{
	struct lua_State *L = (struct lua_State *) cb_ctx;

	lua_pushliteral(L, "name");
	lua_pushstring(L, fiber_name(f));
	lua_settable(L, -3);

	lua_pushstring(L, "fid");
	luaL_pushuint64(L, f->fid);
	lua_settable(L, -3);

	lua_pushstring(L, "csw");
	lua_pushnumber(L, f->csw);
	lua_settable(L, -3);

	lua_pushliteral(L, "time");
	lua_pushnumber(L, f->clock_stat.cputime / (double) FIBER_TIME_RES);
	lua_settable(L, -3);

	lua_pushliteral(L, "memory");
	lua_newtable(L);
	lua_pushstring(L, "used");
	lua_pushnumber(L, region_used(&f->gc));
	lua_settable(L, -3);
	lua_pushstring(L, "total");
	lua_pushnumber(L, region_total(&f->gc) + f->stack_size +
		       sizeof(struct fiber));
	lua_settable(L, -3);
	lua_settable(L, -3);

	if (backtrace) {
#ifdef ENABLE_BACKTRACE
		lua_pushstring(L, "backtrace");
		lua_newtable(L);
		struct backtrace_lua bt;
		backtrace_lua_collect(&bt, f, 1);
		if (fiber_parent_backtrace_is_enabled() && f->parent_bt != NULL)
			backtrace_lua_cat(&bt, f->parent_bt);
		backtrace_lua_stack_push(&bt, L);
		lua_settable(L, -3);
#endif /* ENABLE_BACKTRACE */
	}
	return 0;
}

static int
lbox_fiber_statof(struct fiber *f, void *cb_ctx, bool backtrace)
{
	struct lua_State *L = cb_ctx;
	luaL_pushuint64(L, f->fid);
	lua_newtable(L);
	lbox_fiber_statof_map(f, cb_ctx,
			      backtrace && (f->flags & FIBER_IS_IDLE) == 0);
	lua_settable(L, -3);
	return 0;
}

#ifdef ENABLE_BACKTRACE
static int
lbox_fiber_statof_bt(struct fiber *f, void *cb_ctx)
{
	return lbox_fiber_statof(f, cb_ctx, true);
}
#endif /* ENABLE_BACKTRACE */

static int
lbox_fiber_statof_nobt(struct fiber *f, void *cb_ctx)
{
	return lbox_fiber_statof(f, cb_ctx, false);
}

static int
lbox_fiber_top_entry(struct fiber *f, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State *) cb_ctx;

	char sbuf[FIBER_NAME_MAX + 32];
	snprintf(sbuf, sizeof(sbuf), "%llu/%s",
		 (long long)f->fid, f->name);
	lua_pushstring(L, sbuf);

	lua_newtable(L);

	lua_pushliteral(L, "average");
	if (cord()->clock_stat.acc != 0) {
		lua_pushnumber(L, f->clock_stat.acc /
				  (double)cord()->clock_stat.acc * 100);
	} else {
		lua_pushnumber(L, 0);
	}
	lua_settable(L, -3);
	lua_pushliteral(L, "instant");
	if (cord()->clock_stat.prev_delta != 0) {
		lua_pushnumber(L, f->clock_stat.prev_delta /
				  (double)cord()->clock_stat.prev_delta * 100);
	} else {
		lua_pushnumber(L, 0);
	}
	lua_settable(L, -3);
	lua_pushliteral(L, "time");
	lua_pushnumber(L, f->clock_stat.cputime / (double) FIBER_TIME_RES);
	lua_settable(L, -3);
	lua_settable(L, -3);

	return 0;
}

static int
lbox_fiber_top(struct lua_State *L)
{
	if (!fiber_top_is_enabled()) {
		luaL_error(L, "fiber.top() is disabled. Enable it with"
			      " fiber.top_enable() first");
	}
	lua_newtable(L);
	lua_pushliteral(L, "cpu");
	lua_newtable(L);
	lbox_fiber_top_entry(&cord()->sched, L);
	fiber_stat(lbox_fiber_top_entry, L);
	lua_settable(L, -3);

	return 1;
}

static int
lbox_fiber_top_enable(struct lua_State *L)
{
	(void) L;
	fiber_top_enable();
	return 0;
}

static int
lbox_fiber_top_disable(struct lua_State *L)
{
	(void) L;
	fiber_top_disable();
	return 0;
}

#ifdef ENABLE_BACKTRACE
bool
lbox_do_backtrace(struct lua_State *L, int index)
{
	if (lua_istable(L, index)) {
		lua_pushstring(L, "backtrace");
		lua_gettable(L, index);
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			lua_pushstring(L, "bt");
			lua_gettable(L, index);
		}
		if (!lua_isnil(L, -1))
			return lua_toboolean(L, -1);
		lua_pop(L, 1);
	}
	return true;
}

static int
lbox_fiber_parent_backtrace_enable(struct lua_State *L)
{
	(void)L;
	fiber_parent_backtrace_enable();
	return 0;
}

static int
lbox_fiber_parent_backtrace_disable(struct lua_State *L)
{
	(void)L;
	fiber_parent_backtrace_disable();
	return 0;
}
#endif /* ENABLE_BACKTRACE */

/**
 * Return fiber statistics.
 */
static int
lbox_fiber_info(struct lua_State *L)
{
#ifdef ENABLE_BACKTRACE
	bool do_backtrace = lbox_do_backtrace(L, 1);
	if (do_backtrace) {
		lua_newtable(L);
		fiber_stat(lbox_fiber_statof_bt, L);
	} else
#endif /* ENABLE_BACKTRACE */
	{
		lua_newtable(L);
		fiber_stat(lbox_fiber_statof_nobt, L);
	}
	lua_createtable(L, 0, 1);
	lua_pushliteral(L, "mapping"); /* YAML will use block mode */
	lua_setfield(L, -2, LUAL_SERIALIZE);
	lua_setmetatable(L, -2);
	return 1;
}

static int
lua_fiber_run_f(MAYBE_UNUSED va_list ap)
{
	int result;
	struct fiber *f = fiber();
	struct lua_State *L = f->storage.lua.stack;
	int coro_ref = lua_tointeger(L, -1);
	lua_pop(L, 1);
	result = luaT_call(L, lua_gettop(L) - 1, LUA_MULTRET);
	/*
	 * If fiber is not joinable
	 * We can unref child stack here,
	 * otherwise we have to unref child stack in join
	 */
	if (f->flags & FIBER_IS_JOINABLE)
		lua_pushinteger(L, coro_ref);
	else
		luaL_unref(L, LUA_REGISTRYINDEX, coro_ref);

	return result;
}

/**
 * Utility function for fiber.create and fiber.new
 */
static struct fiber *
fiber_create(struct lua_State *L)
{
	lua_State *child_L = luaT_newthread(L);
	if (child_L == NULL)
		luaT_error(L);
	int coro_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	struct fiber *f = fiber_new("lua", lua_fiber_run_f);
	if (f == NULL) {
		luaL_unref(L, LUA_REGISTRYINDEX, coro_ref);
		luaT_error(L);
	}
#ifdef ENABLE_BACKTRACE
	if (fiber_parent_backtrace_is_enabled()) {
		struct fiber *parent = fiber();
		f->parent_bt = region_alloc(&f->gc, sizeof(*f->parent_bt));
		if (f->parent_bt != NULL)
			backtrace_lua_collect(f->parent_bt, parent, 3);
	}
#endif /* ENABLE_BACKTRACE */

	/* Move the arguments to the new coro */
	lua_xmove(L, child_L, lua_gettop(L));
	/* XXX: 'fiber' is leaked if this throws a Lua error. */
	lbox_pushfiber(L, f);
	/* Pass coro_ref via lua stack so that we don't have to pass it
	 * as an argument of fiber_run function.
	 * No function will work with child_L until the function is called.
	 * At that time we can pop coro_ref from stack
	 */
	lua_pushinteger(child_L, coro_ref);
	f->storage.lua.stack = child_L;
	return f;
}

/**
 * Create, resume and detach a fiber
 * given the function and its arguments.
 */
static int
lbox_fiber_create(struct lua_State *L)
{
	if (lua_gettop(L) < 1 || !lua_isfunction(L, 1))
		luaL_error(L, "fiber.create(function, ...): bad arguments");
	if (fiber_checkstack())
		luaL_error(L, "fiber.create(): out of fiber stack");
	struct fiber *f = fiber_create(L);
	fiber_start(f);
	return 1;
}

/**
 * Create a fiber, schedule it for execution, but not invoke yet
 */
static int
lbox_fiber_new(struct lua_State *L)
{
	if (lua_gettop(L) < 1 || !lua_isfunction(L, 1))
		luaL_error(L, "fiber.new(function, ...): bad arguments");
	if (fiber_checkstack())
		luaL_error(L, "fiber.new(): out of fiber stack");

	struct fiber *f = fiber_create(L);
	fiber_wakeup(f);
	return 1;
}

static struct fiber *
lbox_get_fiber(struct lua_State *L)
{
	if (lua_gettop(L) != 0) {
		uint64_t *fid = luaL_checkudata(L, 1, fiberlib_name);
		return fiber_find(*fid);
	} else {
		return fiber();
	}
}

/**
 * Get fiber status.
 * This follows the rules of Lua coroutine.status() function:
 * Returns the status of fibier, as a string:
 * - "running", if the fiber is running (that is, it called status);
 * - "suspended", if the fiber is suspended in a call to yield(),
 *    or if it has not started running yet;
 * - "dead" if the fiber has finished its body function, or if it
 *   has stopped with an error.
 */
static int
lbox_fiber_status(struct lua_State *L)
{
	struct fiber *f = lbox_get_fiber(L);
	const char *status;
	if (f == NULL) {
		/* This fiber is dead. */
		status = "dead";
	} else if (f == fiber()) {
		/* The fiber is the current running fiber. */
		status = "running";
	} else {
		/* None of the above: must be suspended. */
		status = "suspended";
	}
	lua_pushstring(L, status);
	return 1;
}

/**
 * Get fiber info: number of context switches, backtrace, id,
 * total memory, used memory.
 */
static int
lbox_fiber_object_info(struct lua_State *L)
{
	struct fiber *f = lbox_get_fiber(L);
	if (f == NULL)
		luaL_error(L, "the fiber is dead");
#ifdef ENABLE_BACKTRACE
	bool do_backtrace = lbox_do_backtrace(L, 2);
	if (do_backtrace) {
		lua_newtable(L);
		lbox_fiber_statof_map(f, L, true);
	} else
#endif /* ENABLE_BACKTRACE */
	{
		lua_newtable(L);
		lbox_fiber_statof_map(f, L, false);
	}
	return 1;
}

static int
lbox_fiber_csw(struct lua_State *L)
{
	struct fiber *f = lbox_get_fiber(L);
	if (f == NULL) {
		luaL_error(L, "the fiber is dead");
	} else {
		lua_pushinteger(L, f->csw);
	}
	return 1;
}

/**
 * Get or set fiber name.
 * With no arguments, gets or sets the current fiber
 * name. It's also possible to get/set the name of
 * another fiber.
 * Last argument can be a map with a single key:
 * {truncate = boolean}. If truncate is true, then a new fiber
 * name is truncated to a max possible fiber name length.
 * If truncate is false (or was not specified), then too long
 * new name raise error.
 */
static int
lbox_fiber_name(struct lua_State *L)
{
	struct fiber *f = fiber();
	int name_index;
	int opts_index;
	int top = lua_gettop(L);
	if (lua_type(L, 1) == LUA_TUSERDATA) {
		f = lbox_checkfiber(L, 1);
		name_index = 2;
		opts_index = 3;
	} else {
		name_index = 1;
		opts_index = 2;
	}
	if (top == name_index || top == opts_index) {
		/* Set name. */
		const char *name = luaL_checkstring(L, name_index);
		int name_size = strlen(name) + 1;
		if (top == opts_index && lua_istable(L, opts_index)) {
			lua_getfield(L, opts_index, "truncate");
			/* Truncate the name if needed. */
			if (lua_isboolean(L, -1) && lua_toboolean(L, -1) &&
			    name_size > FIBER_NAME_MAX)
				name_size = FIBER_NAME_MAX;
			lua_pop(L, 1);
		}
		if (name_size > FIBER_NAME_MAX)
			luaL_error(L, "Fiber name is too long");
		fiber_set_name(f, name);
		return 0;
	} else {
		lua_pushstring(L, fiber_name(f));
		return 1;
	}
}

static int
lbox_fiber_storage(struct lua_State *L)
{
	struct fiber *f = lbox_checkfiber(L, 1);
	int storage_ref = f->storage.lua.storage_ref;
	if (storage_ref == FIBER_LUA_NOREF) {
		lua_newtable(L); /* create local storage on demand */
		storage_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		f->storage.lua.storage_ref = storage_ref;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, storage_ref);
	return 1;
}

static int
lbox_fiber_index(struct lua_State *L)
{
	if (lua_gettop(L) < 2)
		return 0;
	if (lua_isstring(L, 2) && strcmp(lua_tostring(L, 2), "storage") == 0)
		return lbox_fiber_storage(L);

	/* Get value from metatable */
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_gettable(L, -2);
	return 1;
}

/**
 * Yield to the sched fiber and sleep.
 * @param[in]  amount of time to sleep (double)
 *
 * Only the current fiber can be made to sleep.
 */
static int
lbox_fiber_sleep(struct lua_State *L)
{
	if (! lua_isnumber(L, 1) || lua_gettop(L) != 1)
		luaL_error(L, "fiber.sleep(delay): bad arguments");
	double delay = lua_tonumber(L, 1);
	fiber_sleep(delay);
	luaL_testcancel(L);
	return 0;
}

static int
lbox_fiber_yield(struct lua_State *L)
{
	fiber_sleep(0);
	luaL_testcancel(L);
	return 0;
}

static int
lbox_fiber_self(struct lua_State *L)
{
	lbox_pushfiber(L, fiber());
	return 1;
}

static int
lbox_fiber_find(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "fiber.find(id): bad arguments");
	uint64_t fid = luaL_touint64(L, -1);
	struct fiber *f = fiber_find(fid);
	if (f)
		lbox_pushfiber(L, f);
	else
		lua_pushnil(L);
	return 1;
}

/**
 * Running and suspended fibers can be cancelled.
 * Zombie fibers can't.
 */
static int
lbox_fiber_cancel(struct lua_State *L)
{
	struct fiber *f = lbox_checkfiber(L, 1);
	fiber_cancel(f);
	/*
	 * Check if we're ourselves cancelled.
	 * This also implements cancel for the case when
	 * f == fiber().
	 */
	luaL_testcancel(L);
	return 0;
}

static int
lbox_fiber_serialize(struct lua_State *L)
{
	struct fiber *f = lbox_checkfiber(L, 1);
	lua_createtable(L, 0, 1);
	luaL_pushuint64(L, f->fid);
	lua_setfield(L, -2, "id");
	lua_pushstring(L, fiber_name(f));
	lua_setfield(L, -2, "name");
	lbox_fiber_status(L);
	lua_setfield(L, -2, "status");
	return 1;
}

static int
lbox_fiber_tostring(struct lua_State *L)
{
	char buf[32];
	struct fiber *f = lbox_checkfiber(L, 1);
	snprintf(buf, sizeof(buf), "fiber: %llu",
		 (long long)f->fid);
	lua_pushstring(L, buf);
	return 1;
}

/**
 * Check if this current fiber has been cancelled and
 * throw an exception if this is the case.
 */

static int
lbox_fiber_testcancel(struct lua_State *L)
{
	if (lua_gettop(L) != 0)
		luaL_error(L, "fiber.testcancel(): bad arguments");
	luaL_testcancel(L);
	return 0;
}

static int
lbox_fiber_wakeup(struct lua_State *L)
{
	struct fiber *f = lbox_checkfiber(L, 1);
	/*
	 * It's unsafe to wakeup fibers which don't expect
	 * it.
	 */
	if (f->flags & FIBER_IS_CANCELLABLE)
		fiber_wakeup(f);
	return 0;
}

static int
lbox_fiber_join(struct lua_State *L)
{
	struct fiber *fiber = lbox_checkfiber(L, 1);
	struct lua_State *child_L = fiber->storage.lua.stack;
	struct error *e = NULL;
	int num_ret = 0;
	int coro_ref = 0;

	if (!(fiber->flags & FIBER_IS_JOINABLE))
		luaL_error(L, "the fiber is not joinable");
	double timeout = TIMEOUT_INFINITY;
	if (!lua_isnoneornil(L, 2)) {
		if (!lua_isnumber(L, 2) ||
		    (timeout = lua_tonumber(L, 2)) < .0) {
			luaL_error(L, "fiber:join(timeout): bad arguments");
		}
	}
	int rc = fiber_join_timeout(fiber, timeout);

	if (child_L != NULL) {
		coro_ref = lua_tointeger(child_L, -1);
		lua_pop(child_L, 1);
	}
	if (rc != 0) {
		/*
		 * After fiber_join the error of fiber being joined was moved to
		 * current fiber diag so we have to get it from there.
		 */
		assert(!diag_is_empty(&fiber()->diag));
		e = diag_last_error(&fiber()->diag);
		lua_pushboolean(L, false);
		luaT_pusherror(L, e);
		diag_clear(&fiber()->diag);
		num_ret = 1;
	} else {
		lua_pushboolean(L, true);
		if (child_L != NULL) {
			num_ret = lua_gettop(child_L);
			lua_xmove(child_L, L, num_ret);
		}
	}
	if (child_L != NULL)
		luaL_unref(L, LUA_REGISTRYINDEX, coro_ref);
	return num_ret + 1;
}

static int
lbox_fiber_set_joinable(struct lua_State *L)
{

	if (lua_gettop(L) != 2) {
		luaL_error(L, "fiber.set_joinable(id, yesno): bad arguments");
	}
	struct fiber *fiber = lbox_checkfiber(L, 1);
	bool yesno = lua_toboolean(L, 2);
	fiber_set_joinable(fiber, yesno);
	return 0;
}

/**
 * Alternative to fiber.sleep(infinite) which does not participate
 * in an event loop at all until an explicit wakeup. This is less
 * overhead. Useful for fibers sleeping most of the time.
 */
static int
lbox_fiber_stall(struct lua_State *L)
{
	(void) L;
	fiber_yield();
	return 0;
}

/** Helper for fiber slice parsing. */
static struct fiber_slice
lbox_fiber_slice_parse(struct lua_State *L, int idx)
{
	struct fiber_slice slice;
	if (lua_istable(L, idx)) {
		lua_getfield(L, idx, "warn");
		slice.warn = luaL_checknumber(L, -1);
		lua_getfield(L, idx, "err");
		slice.err = luaL_checknumber(L, -1);
		lua_pop(L, 2);
	} else if (lua_isnumber(L, idx)) {
		slice.warn = TIMEOUT_INFINITY;
		slice.err = lua_tonumber(L, idx);
	} else {
		luaL_error(L, "slice must be a table or a number");
		unreachable();
	}
	if (!fiber_slice_is_valid(slice))
		luaL_error(L, "slice must be greater than 0");
	return slice;
}

/** Set slice for current fiber execution. */
static int
lbox_fiber_set_slice(struct lua_State *L)
{
	if (lua_gettop(L) != 1) {
		luaL_error(L, "fiber.set_slice(slice): bad arguments");
	}
	struct fiber_slice slice = lbox_fiber_slice_parse(L, 1);
	fiber_set_slice(slice);
	return 0;
}

/** Extend slice for current fiber execution. */
static int
lbox_fiber_extend_slice(struct lua_State *L)
{
	if (lua_gettop(L) != 1) {
		luaL_error(L, "fiber.extend_slice(slice): bad arguments");
	}
	struct fiber_slice slice = lbox_fiber_slice_parse(L, 1);
	fiber_extend_slice(slice);
	return 0;
}

/**
 * Raise an error if current fiber's slice is over.
 */
static int
lbox_check_slice(struct lua_State *L)
{
	if (lua_gettop(L) != 0)
		luaL_error(L, "fiber.check_slice(): bad arguments");
	if (fiber_check_slice() != 0)
		luaT_error(L);
	return 0;
}

/** Set max slice to current cord or to fiber if it is passed. */
static int
lbox_fiber_set_max_slice(struct lua_State *L)
{
	if (lua_gettop(L) != 1 && lua_gettop(L) != 2) {
		luaL_error(L, "fiber.set_max_slice([id,] slice): "
			      "bad arguments");
	}
	int slice_index = lua_gettop(L);
	struct fiber_slice slice = lbox_fiber_slice_parse(L, slice_index);
	if (lua_gettop(L) == 1) {
		fiber_set_default_max_slice(slice);
	} else {
		struct fiber *fiber = lbox_checkfiber(L, 1);
		fiber_set_max_slice(fiber, slice);
	}
	return 0;
}

static const struct luaL_Reg lbox_fiber_meta [] = {
	{"id", lbox_fiber_id},
	{"name", lbox_fiber_name},
	{"cancel", lbox_fiber_cancel},
	{"status", lbox_fiber_status},
	{"info", lbox_fiber_object_info},
	{"csw", lbox_fiber_csw},
	{"testcancel", lbox_fiber_testcancel},
	{"__serialize", lbox_fiber_serialize},
	{"__tostring", lbox_fiber_tostring},
	{"join", lbox_fiber_join},
	{"set_joinable", lbox_fiber_set_joinable},
	{"set_max_slice", lbox_fiber_set_max_slice},
	{"wakeup", lbox_fiber_wakeup},
	{"__index", lbox_fiber_index},
	{NULL, NULL}
};

static const struct luaL_Reg fiberlib[] = {
	{"info", lbox_fiber_info},
	{"top", lbox_fiber_top},
	{"top_enable", lbox_fiber_top_enable},
	{"top_disable", lbox_fiber_top_disable},
#ifdef ENABLE_BACKTRACE
	{"parent_backtrace_enable", lbox_fiber_parent_backtrace_enable},
	{"parent_backtrace_disable", lbox_fiber_parent_backtrace_disable},
#endif /* ENABLE_BACKTRACE */
	{"sleep", lbox_fiber_sleep},
	{"yield", lbox_fiber_yield},
	{"self", lbox_fiber_self},
	{"id", lbox_fiber_id},
	{"find", lbox_fiber_find},
	{"kill", lbox_fiber_cancel},
	{"wakeup", lbox_fiber_wakeup},
	{"join", lbox_fiber_join},
	{"set_joinable", lbox_fiber_set_joinable},
	{"cancel", lbox_fiber_cancel},
	{"testcancel", lbox_fiber_testcancel},
	{"create", lbox_fiber_create},
	{"new", lbox_fiber_new},
	{"status", lbox_fiber_status},
	{"name", lbox_fiber_name},
	{"check_slice", lbox_check_slice},
	{"set_max_slice", lbox_fiber_set_max_slice},
	{"set_slice", lbox_fiber_set_slice},
	{"extend_slice", lbox_fiber_extend_slice},
	/* Internal functions, to hide in fiber.lua. */
	{"stall", lbox_fiber_stall},
	{NULL, NULL}
};

void
tarantool_lua_fiber_init(struct lua_State *L)
{
	luaL_register_module(L, fiberlib_name, fiberlib);
	lua_pop(L, 1);
	luaL_register_type(L, fiberlib_name, lbox_fiber_meta);
}

/*
 * }}}
 */
