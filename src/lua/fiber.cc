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
#include "lua/init.h"
#include "tarantool.h"
#include "box/box.h"
#include "tbuf.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lj_obj.h>
#include <lj_ctype.h>
#include <lj_cdata.h>
#include <lj_cconv.h>
#include <lj_state.h>
} /* extern "C" */

#include "lua/fiber.h"
#include "fiber.h"

#include <sys/types.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>
/*
 * }}}
 */

/* {{{ box.fiber Lua library: access to Tarantool fibers
 *
 * Each fiber can be running, suspended or dead.
 * A fiber is created (box.fiber.create()) suspended.
 * It can be started with box.fiber.resume(), yield
 * the control back with box.fiber.yield() end
 * with return or just by reaching the end of the
 * function.
 *
 * A fiber can also be attached or detached.
 * An attached fiber is a child of the creator,
 * and is running only if the creator has called
 * box.fiber.resume(). A detached fiber is a child of
 * Tarntool/Box internal 'sched' fiber, and is gets
 * scheduled only if there is a libev event associated
 * with it.
 * To detach, a running fiber must invoke box.fiber.detach().
 * A detached fiber loses connection with its parent
 * forever.
 *
 * All fibers are part of the fiber registry, box.fiber.
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
 * The other potential problem comes from detached
 * fibers which never get scheduled, because are subscribed
 * or get no events. Such morphing fibers can be killed
 * with box.fiber.cancel(), since box.fiber.cancel()
 * sends an asynchronous wakeup event to the fiber.
 */

static const char *fiberlib_name = "box.fiber";

enum fiber_state { DONE, YIELD, DETACH };

/**
 * @pre: stack top contains a table
 * @post: sets table field specified by name of the table on top
 * of the stack to a weak kv table and pops that weak table.
 */
static void
lbox_create_weak_table(struct lua_State *L, const char *name)
{
	lua_newtable(L);
	/* and a metatable */
	lua_newtable(L);
	/* weak keys and values */
	lua_pushstring(L, "kv");
	/* pops 'kv' */
	lua_setfield(L, -2, "__mode");
	/* pops the metatable */
	lua_setmetatable(L, -2);
	/* assigns and pops table */
	lua_setfield(L, -2, name);
	/* gets memoize back. */
	lua_getfield(L, -1, name);
	assert(! lua_isnil(L, -1));
}

/**
 * Push a userdata for the given fiber onto Lua stack.
 */
static void
lbox_pushfiber(struct lua_State *L, struct fiber *f)
{
	/*
	 * Use 'memoize'  pattern and keep a single userdata for
	 * the given fiber.
	 */
	luaL_getmetatable(L, fiberlib_name);
	int top = lua_gettop(L);
	lua_getfield(L, -1, "memoize");
	if (lua_isnil(L, -1)) {
		/* first access - instantiate memoize */
		/* pop the nil */
		lua_pop(L, 1);
		/* create memoize table */
		lbox_create_weak_table(L, "memoize");
	}
	/* Find out whether the fiber is  already in the memoize table. */
	lua_pushlightuserdata(L, f);
	lua_gettable(L, -2);
	if (lua_isnil(L, -1)) {
		/* no userdata for fiber created so far */
		/* pop the nil */
		lua_pop(L, 1);
		/* push the key back */
		lua_pushlightuserdata(L, f);
		/* create a new userdata */
		void **ptr = (void **) lua_newuserdata(L, sizeof(void *));
		*ptr = f;
		luaL_getmetatable(L, fiberlib_name);
		lua_setmetatable(L, -2);
		/* memoize it */
		lua_settable(L, -3);
		lua_pushlightuserdata(L, f);
		/* get it back */
		lua_gettable(L, -2);
	}
	/*
	 * Here we have a userdata on top of the stack and
	 * possibly some garbage just under the top. Move the
	 * result to the beginning of the stack and clear the rest.
	 */
	/* moves the current top to the old top */
	lua_replace(L, top);
	/* clears everything after the old top */
	lua_settop(L, top);
}

static struct fiber *
lbox_checkfiber(struct lua_State *L, int index)
{
	return *(struct fiber **) luaL_checkudata(L, index, fiberlib_name);
}

static struct fiber *
lua_isfiber(struct lua_State *L, int narg)
{
	if (lua_getmetatable(L, narg) == 0)
		return NULL;
	luaL_getmetatable(L, fiberlib_name);
	struct fiber *f = NULL;
	if (lua_equal(L, -1, -2))
		f = * (struct fiber **) lua_touserdata(L, narg);
	lua_pop(L, 2);
	return f;
}

static int
lbox_fiber_id(struct lua_State *L)
{
	struct fiber *f = lua_gettop(L) ? lbox_checkfiber(L, 1) : fiber;
	lua_pushinteger(L, f->fid);
	return 1;
}

static struct lua_State *
box_lua_fiber_get_coro(struct lua_State *L, struct fiber *f)
{
	lua_pushlightuserdata(L, f);
	lua_gettable(L, LUA_REGISTRYINDEX);
	struct lua_State *child_L = lua_tothread(L, -1);
	lua_pop(L, 1);
	return child_L;
}

static void
box_lua_fiber_clear_coro(struct lua_State *L, struct fiber *f)
{
	lua_pushlightuserdata(L, f);
	lua_pushnil(L);
	lua_settable(L, LUA_REGISTRYINDEX);
}

/**
 * To yield control to the calling fiber
 * we need to be able to find the caller of an
 * attached fiber. Instead of passing the caller
 * around on the child fiber stack, we create a
 * weak table associated with child fiber
 * lua_State, and save the caller in it.
 *
 * When the child fiber lua thread is garbage collected,
 * the table is automatically cleared.
 */
static void
box_lua_fiber_push_caller(struct lua_State *child_L)
{
	luaL_getmetatable(child_L, fiberlib_name);
	lua_getfield(child_L, -1, "callers");
	if (lua_isnil(child_L, -1)) {
		lua_pop(child_L, 1);
		lbox_create_weak_table(child_L, "callers");
	}
	lua_pushthread(child_L);
	lua_pushlightuserdata(child_L, fiber);
	lua_settable(child_L, -3);
	/* Pop the fiberlib metatable and callers table. */
	lua_pop(child_L, 2);
}


static struct fiber *
box_lua_fiber_get_caller(struct lua_State *L)
{
	luaL_getmetatable(L, fiberlib_name);
	lua_getfield(L, -1, "callers");
	lua_pushthread(L);
	lua_gettable(L, -2);
	struct fiber *caller = (struct fiber *) lua_touserdata(L, -1);
	/* Pop the caller, the callers table, the fiberlib metatable. */
	lua_pop(L, 3);
	return caller;
}

static int
lbox_fiber_gc(struct lua_State *L)
{
	struct fiber *f = lbox_checkfiber(L, 1);
	struct lua_State *child_L = box_lua_fiber_get_coro(L, f);
	/*
	 * A non-NULL coro is an indicator of a 1) alive,
	 * 2) suspended and 3) attached fiber. The coro is
	 * an outlet to pass arguments in and out the Lua
	 * routine being executed by the fiber (see fiber.resume()
	 * and fiber.yield()), and as soon as the Lua routine
	 * completes, the "plug" is shut down (see
	 * box_lua_fiber_run()). When its routine completes,
	 * the fiber recycles itself.
	 * Likewise, when a fiber becomes detached, this plug is
	 * removed, since we no longer need to pass arguments
	 * to and from it, and 'sched' garbage collects all detached
	 * fibers (see lbox_fiber_detach()).
	 * We also know that the fiber is suspended, not running,
	 * because any running and attached fiber is referenced,
	 * if only by the fiber which called lbox_lua_resume()
	 * on it. lbox_lua_resume() is the only entry point
	 * to resume an attached fiber.
	 */
	if (child_L) {
		assert(f != fiber && child_L != L);
		/*
		 * Garbage collect the associated coro.
		 * Do it first, since the cancelled fiber
		 * can get recycled quickly.
		 */
		box_lua_fiber_clear_coro(L, f);
		/*
		 * Cancel and recycle the fiber. This
		 * returns only after the fiber has died.
		 */
		fiber_cancel(f);
	}
	return 0;
}

static int
fiber_backtrace_cb(int frameno, void *frameret, const char *func, size_t offset, void *cb_ctx)
{
	char buf[512];
	int l = snprintf(buf, sizeof(buf), "#%-2d %p in ", frameno, frameret);
	if (func)
		snprintf(buf + l, sizeof(buf) - l, "%s+%" PRI_SZ "", func, offset);
	else
		snprintf(buf + l, sizeof(buf) - l, "?");
	struct lua_State *L = (struct lua_State*)cb_ctx;
	lua_pushnumber(L, frameno + 1);
	lua_pushstring(L, buf);
	lua_settable(L, -3);
	return 0;
}

static int
lbox_fiber_statof(struct fiber *f, void *cb_ctx)
{
	struct lua_State *L = (struct lua_State *) cb_ctx;

	lua_pushstring(L, fiber_name(f));
	lua_newtable(L);

	lua_pushstring(L, "fid");
	lua_pushnumber(L, f->fid);
	lua_settable(L, -3);

	lua_pushstring(L, "csw");
	lua_pushnumber(L, f->csw);
	lua_settable(L, -3);

#ifdef ENABLE_BACKTRACE
	lua_pushstring(L, "backtrace");
	lua_newtable(L);
	backtrace_foreach(fiber_backtrace_cb,
	                  f->last_stack_frame,
	                  f->coro.stack, f->coro.stack_size, L);
	lua_settable(L, -3);
#endif /* ENABLE_BACKTRACE */

	lua_settable(L, -3);
	return 0;
}

/**
 * Return fiber statistics.
 */
static int
lbox_fiber_info(struct lua_State *L)
{
	lua_newtable(L);
	fiber_stat(lbox_fiber_statof, L);
	return 1;
}

/**
 * Detach the current fiber.
 */
static int
lbox_fiber_detach(struct lua_State *L)
{
	if (box_lua_fiber_get_coro(L, fiber) == NULL)
		luaL_error(L, "fiber.detach(): not attached");
	struct fiber *caller = box_lua_fiber_get_caller(L);
	/* Clear the caller, to avoid a reference leak. */
	/* Request a detach. */
	lua_pushinteger(L, DETACH);
	/* A detached fiber has no associated session. */
	fiber_set_sid(fiber, 0);
	fiber_yield_to(caller);
	return 0;
}

static void
box_lua_fiber_run(va_list ap __attribute__((unused)))
{
	fiber_testcancel();
	fiber_setcancellable(false);

	struct lua_State *L = box_lua_fiber_get_coro(tarantool_L, fiber);
	/*
	 * Reference the coroutine to make sure it's not garbage
	 * collected when detached.
	 */
	lua_pushthread(L);
	int coro_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	/*
	 * Lua coroutine.resume() returns true/false for
	 * completion status plus whatever the coroutine main
	 * function returns. Follow this style here.
	 */
	auto cleanup = [=] {
		/*
		 * If the coroutine has detached itself, collect
		 * its resources here.
		 */
		luaL_unref(L, LUA_REGISTRYINDEX, coro_ref);
	};

	try {
		lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
		/* push completion status */
		lua_pushboolean(L, true);
		/* move 'true' to stack start */
		lua_insert(L, 1);

		cleanup();
	} catch (const FiberCancelException& e) {
		box_lua_fiber_clear_coro(tarantool_L, fiber);
		/*
		 * Note: FiberCancelException leaves garbage on
		 * coroutine stack. This is OK since it is only
		 * possible to cancel a fiber which is not
		 * scheduled, and cancel() is synchronous.
		 */

		cleanup();
		throw;
	} catch (const Exception& e) {
		/* pop any possible garbage */
		lua_settop(L, 0);
		/* completion status */
		lua_pushboolean(L, false);
		/* error message */
		lua_pushstring(L, e.errmsg());

		if (box_lua_fiber_get_coro(tarantool_L, fiber) == NULL) {
			/* The fiber is detached, log the error. */
			e.log();
		}

		cleanup();
	} catch (...) {
		lua_settop(L, 1);
		/*
		 * The error message is already there.
		 * Add completion status.
		 */
		lua_pushboolean(L, false);
		lua_insert(L, -2);
		if (box_lua_fiber_get_coro(tarantool_L, fiber) == NULL &&
		    lua_tostring(L, -1) != NULL) {

			/* The fiber is detached, log the error. */
			say_error("%s", lua_tostring(L, -1));
		}

		cleanup();
	}
	/*
	 * L stack contains nothing but call results.
	 * If we're still attached, synchronously pass
	 * them to the caller, and then terminate.
	 */
	if (box_lua_fiber_get_coro(L, fiber)) {
		struct fiber *caller = box_lua_fiber_get_caller(L);
		lua_pushinteger(L, DONE);
		fiber_yield_to(caller);
	}
}

/** @retval true if check failed, false otherwise */
static bool
lbox_fiber_checkstack(struct lua_State *L)
{
	fiber_checkstack();
	struct fiber *f = fiber;
	const int MAX_STACK_DEPTH = 16;
	int depth = 1;
	while ((L = box_lua_fiber_get_coro(L, f)) != NULL) {
		if (depth++ == MAX_STACK_DEPTH)
			return true;
		f = box_lua_fiber_get_caller(L);
	}
	return false;
}


static int
lbox_fiber_create(struct lua_State *L)
{
	if (lua_gettop(L) != 1 || !lua_isfunction(L, 1))
		luaL_error(L, "fiber.create(function): bad arguments");
	if (lbox_fiber_checkstack(L))
		luaL_error(L, "fiber.create(function): recursion limit"
			   " reached");

	struct fiber *f = fiber_new("lua", box_lua_fiber_run);
	/* Preserve the session in a child fiber. */
	fiber_set_sid(f, fiber->sid);
	/* Initially the fiber is cancellable */
	f->flags |= FIBER_USER_MODE | FIBER_CANCELLABLE;

	/* associate coro with fiber */
	lua_pushlightuserdata(L, f);
	struct lua_State *child_L = lua_newthread(L);
	lua_settable(L, LUA_REGISTRYINDEX);
	/* Move the argument (function of the coro) to the new coro */
	lua_xmove(L, child_L, 1);
	lbox_pushfiber(L, f);
	return 1;
}

static int
lbox_fiber_resume(struct lua_State *L)
{
	struct fiber *f = lbox_checkfiber(L, 1);
	if (f->fid == 0)
		luaL_error(L, "fiber.resume(): the fiber is dead");
	struct lua_State *child_L = box_lua_fiber_get_coro(L, f);
	if (child_L == NULL)
		luaL_error(L, "fiber.resume(): can't resume a "
			   "detached fiber");
	int nargs = lua_gettop(L) - 1;
	if (nargs > 0)
		lua_xmove(L, child_L, nargs);
	/* dup 'out' for admin fibers */
	tarantool_lua_dup_out(L, child_L);
	int fid = f->fid;
	/* Silent compiler warnings in a release build. */
	(void) fid;
	box_lua_fiber_push_caller(child_L);
	/*
	 * We don't use fiber_call() since this breaks any sort
	 * of yield in the called fiber: for a yield to work,
	 * the callee got to be scheduled by 'sched'.
	 */
	fiber_yield_to(f);
	/*
	 * The called fiber could have done only 3 things:
	 * - yielded to us (then we should grab its return)
	 * - completed (grab return values, wake up the fiber,
	 *   so that it can die)
	 * - detached (grab return values, wakeup the fiber so it
	 *   can continue).
	 */
	assert(f->fid == fid);
	tarantool_lua_set_out(child_L, NULL);
	/* Find out the state of the child fiber. */
	enum fiber_state child_state = (enum fiber_state) lua_tointeger(child_L, -1);
	lua_pop(child_L, 1);
	/* Get the results */
	nargs = lua_gettop(child_L);
	lua_xmove(child_L, L, nargs);
	if (child_state != YIELD) {
		/*
		 * The fiber is dead or requested a detach.
		 * Garbage collect the associated coro.
		 */
		box_lua_fiber_clear_coro(L, f);
		if (child_state == DETACH) {
			/*
			 * Schedule the runaway child at least
			 * once.
			 */
			fiber_wakeup(f);
		} else {
			/* Synchronously reap a dead child. */
			fiber_call(f);
		}
	}
	return nargs;
}

static void
box_lua_fiber_run_detached(va_list ap)
{
	int coro_ref = va_arg(ap, int);
	struct lua_State *L = va_arg(ap, struct lua_State *);
	auto cleanup = [=] {
		luaL_unref(L, LUA_REGISTRYINDEX, coro_ref);
	};
	try {
		lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
		cleanup();
	} catch (const FiberCancelException &e) {
		cleanup();
		throw;
	} catch (const Exception &e) {
		e.log();
		cleanup();
	} catch (...) {
		lua_settop(L, 1);
		if (lua_tostring(L, -1) != NULL)
			say_error("%s", lua_tostring(L, -1));
		cleanup();
	}
}

/**
 * Create, resume and detach a fiber
 * given the function and its arguments.
 */
static int
lbox_fiber_wrap(struct lua_State *L)
{
	if (lua_gettop(L) < 1 || !lua_isfunction(L, 1))
		luaL_error(L, "fiber.wrap(function, ...): bad arguments");
	fiber_checkstack();

	struct fiber *f = fiber_new("lua", box_lua_fiber_run_detached);
	/* Not a system fiber. */
	f->flags |= FIBER_USER_MODE;
	struct lua_State *child_L = lua_newthread(L);
	int coro_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	/* Move the arguments to the new coro */
	lua_xmove(L, child_L, lua_gettop(L));
	fiber_call(f, coro_ref, child_L);
	if (f->fid)
		lbox_pushfiber(L, f);
	else
		lua_pushnil(L);
	return 1;
}

/**
 * Yield the current fiber.
 *
 * Yield control to the calling fiber -- if the fiber
 * is attached, or to sched otherwise.
 * If the fiber is attached, whatever arguments are passed
 * to this call, are passed on to the calling fiber.
 * If the fiber is detached, simply returns everything back.
 */
static int
lbox_fiber_yield(struct lua_State *L)
{
	/*
	 * Yield to the caller. The caller will take care of
	 * whatever arguments are taken.
	 */
	fiber_setcancellable(true);
	if (box_lua_fiber_get_coro(L, fiber) == NULL) {
		fiber_wakeup(fiber);
		fiber_yield();
		fiber_testcancel();
	} else {
		struct fiber *caller = box_lua_fiber_get_caller(L);
		lua_pushinteger(L, YIELD);
		fiber_yield_to(caller);
	}
	fiber_setcancellable(false);
	/*
	 * Got resumed. Return whatever the caller has passed
	 * to us with box.fiber.resume().
	 * As a side effect, the detached fiber which yields
	 * to sched always gets back whatever it yields.
	 */
	return lua_gettop(L);
}

static bool
fiber_is_caller(struct lua_State *L, struct fiber *f) {
	struct fiber *child = fiber;
	while ((L = box_lua_fiber_get_coro(L, child)) != NULL) {
		struct fiber *caller = box_lua_fiber_get_caller(L);
		if (caller == f)
			return true;
		child = caller;
	}
	return false;
}

/**
 * Get fiber status.
 * This follows the rules of Lua coroutine.status() function:
 * Returns the status of fibier, as a string:
 * - "running", if the fiber is running (that is, it called status);
 * - "suspended", if the fiber is suspended in a call to yield(),
 *    or if it has not started running yet;
 * - "normal" if the fiber is active but not running (that is,
 *   it has resumed another fiber);
 * - "dead" if the fiber has finished its body function, or if it
 *   has stopped with an error.
 */
static int
lbox_fiber_status(struct lua_State *L)
{
	struct fiber *f = lua_gettop(L) ? lbox_checkfiber(L, 1) : fiber;
	const char *status;
	if (f->fid == 0) {
		/* This fiber is dead. */
		status = "dead";
	} else if (f == fiber) {
		/* The fiber is the current running fiber. */
		status = "running";
	} else if (fiber_is_caller(L, f)) {
		/* The fiber is current fiber's caller. */
		status = "normal";
	} else {
		/* None of the above: must be suspended. */
		status = "suspended";
	}
	lua_pushstring(L, status);
	return 1;
}

/** Get or set fiber name.
 * With no arguments, gets or sets the current fiber
 * name. It's also possible to get/set the name of
 * another fiber.
 */
static int
lbox_fiber_name(struct lua_State *L)
{
	struct fiber *f = fiber;
	int name_index = 1;
	if (lua_gettop(L) >= 1 && lua_isfiber(L, 1)) {
		f = lbox_checkfiber(L, 1);
		name_index = 2;
	}
	if (lua_gettop(L) == name_index) {
		/* Set name. */
		const char *name = luaL_checkstring(L, name_index);
		fiber_set_name(f, name);
		return 0;
	} else {
		lua_pushstring(L, fiber_name(f));
		return 1;
	}
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
	fiber_setcancellable(true);
	fiber_sleep(delay);
	fiber_setcancellable(false);
	return 0;
}

static int
lbox_fiber_self(struct lua_State *L)
{
	lbox_pushfiber(L, fiber);
	return 1;
}

static int
lbox_fiber_find(struct lua_State *L)
{
	int fid = lua_tointeger(L, -1);
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
	if (! (f->flags & FIBER_USER_MODE))
		luaL_error(L, "fiber.cancel(): subject fiber does "
			   "not permit cancel");
	fiber_cancel(f);
	return 0;
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
	fiber_testcancel();
	return 0;
}

static const struct luaL_reg lbox_fiber_meta [] = {
	{"id", lbox_fiber_id},
	{"name", lbox_fiber_name},
	{"__gc", lbox_fiber_gc},
	{NULL, NULL}
};

static const struct luaL_reg fiberlib[] = {
	{"info", lbox_fiber_info},
	{"sleep", lbox_fiber_sleep},
	{"self", lbox_fiber_self},
	{"id", lbox_fiber_id},
	{"find", lbox_fiber_find},
	{"cancel", lbox_fiber_cancel},
	{"testcancel", lbox_fiber_testcancel},
	{"create", lbox_fiber_create},
	{"resume", lbox_fiber_resume},
	{"wrap", lbox_fiber_wrap},
	{"yield", lbox_fiber_yield},
	{"status", lbox_fiber_status},
	{"name", lbox_fiber_name},
	{"detach", lbox_fiber_detach},
	{NULL, NULL}
};

void
tarantool_lua_fiber_init(struct lua_State *L)
{
	luaL_register(L, fiberlib_name, fiberlib);
	lua_pop(L, 1);
	tarantool_lua_register_type(L, fiberlib_name, lbox_fiber_meta);
}

/*
 * }}}
 */
