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

#include <sys/types.h>
#include <dirent.h>

#include "fiber.h"
#include "lua_ipc.h"
#include "lua_socket.h"
#include "lua/info.h"
#include "lua/slab.h"
#include "lua/stat.h"
#include "lua/session.h"
#include "lua/cjson.h"

#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>
#include "scoped_guard.h"

extern "C" {
#include <cfg/tarantool_box_cfg.h>
} /* extern "C" */

/**
 * tarantool start-up file
 */
#define TARANTOOL_LUA_INIT_SCRIPT "init.lua"

struct lua_State *tarantool_L;

/* contents of src/lua/ files */
extern char uuid_lua[];
extern char session_lua[];
static const char *lua_sources[] = { uuid_lua, session_lua, NULL };

/**
 * Remember the output of the administrative console in the
 * registry, to use with 'print'.
 */
static void
tarantool_lua_set_out(struct lua_State *L, const struct tbuf *out)
{
	lua_pushthread(L);
	if (out)
		lua_pushlightuserdata(L, (void *) out);
	else
		lua_pushnil(L);
	lua_settable(L, LUA_REGISTRYINDEX);
}

/**
 * dup out from parent to child L. Used in fiber_create
 */
static void
tarantool_lua_dup_out(struct lua_State *L, struct lua_State *child_L)
{
	lua_pushthread(L);
	lua_gettable(L, LUA_REGISTRYINDEX);
	struct tbuf *out = (struct tbuf *) lua_topointer(L, -1);
	/* pop 'out' */
	lua_pop(L, 1);
	if (out)
		tarantool_lua_set_out(child_L, out);
}


/*
 * {{{ box Lua library: common functions
 */

const char *boxlib_name = "box";

uint64_t
tarantool_lua_tointeger64(struct lua_State *L, int idx)
{
	uint64_t result = 0;

	switch (lua_type(L, idx)) {
	case LUA_TNUMBER:
		result = lua_tonumber(L, idx);
		break;
	case LUA_TSTRING:
	{
		const char *arg = luaL_checkstring(L, idx);
		char *arge;
		errno = 0;
		result = strtoull(arg, &arge, 10);
		if (errno != 0 || arge == arg)
			luaL_error(L, "lua_tointeger64: bad argument");
		break;
	}
	case LUA_TCDATA:
	{
		/* Calculate absolute value in the stack. */
		if (idx < 0)
			idx = lua_gettop(L) + idx + 1;
		GCcdata *cd = cdataV(L->base + idx - 1);
		if (cd->ctypeid != CTID_INT64 && cd->ctypeid != CTID_UINT64) {
			luaL_error(L,
				   "lua_tointeger64: unsupported cdata type");
		}
		result = *(uint64_t*)cdataptr(cd);
		break;
	}
	default:
		luaL_error(L, "lua_tointeger64: unsupported type: %s",
			   lua_typename(L, lua_type(L, idx)));
	}

	return result;
}

static GCcdata*
luaL_pushcdata(struct lua_State *L, CTypeID id, int bits)
{
	CTState *cts = ctype_cts(L);
	CType *ct = ctype_raw(cts, id);
	CTSize sz;
	lj_ctype_info(cts, id, &sz);
	GCcdata *cd = lj_cdata_new(cts, id, bits);
	TValue *o = L->top;
	setcdataV(L, o, cd);
	lj_cconv_ct_init(cts, ct, sz, (uint8_t *) cdataptr(cd), o, 0);
	incr_top(L);
	return cd;
}

int
luaL_pushnumber64(struct lua_State *L, uint64_t val)
{
	GCcdata *cd = luaL_pushcdata(L, CTID_UINT64, 8);
	*(uint64_t*)cdataptr(cd) = val;
	return 1;
}

/** Report libev time (cheap). */
static int
lbox_time(struct lua_State *L)
{
	lua_pushnumber(L, ev_now());
	return 1;
}

/** Report libev time as 64-bit integer */
static int
lbox_time64(struct lua_State *L)
{
	luaL_pushnumber64(L, (uint64_t) ( ev_now() * 1000000 + 0.5 ) );
	return 1;
}

/**
 * descriptor for box methods
 */
static const struct luaL_reg boxlib[] = {
	{"time", lbox_time},
	{"time64", lbox_time64},
	{NULL, NULL}
};

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
 * Tarntool/Box internal 'sched' fiber, and gets
 * scheduled only if there is a libev event associated
 * with it.
 * To detach itself, a running fiber must invoke
 * box.fiber.detach().
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
	 * the given fiber. This is important to not run __gc
	 * twice for a copy of an attached fiber -- __gc should
	 * not remove attached fiber's coro prematurely.
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
	lua_pushinteger(L, f->fid);
	lua_gettable(L, -2);
	if (lua_isnil(L, -1)) {
		/* no userdata for fiber created so far */
		/* pop the nil */
		lua_pop(L, 1);
		/* push the key back */
		lua_pushinteger(L, f->fid);
		/* create a new userdata */
		int *ptr = (int *) lua_newuserdata(L, sizeof(int));
		*ptr = f->fid;
		luaL_getmetatable(L, fiberlib_name);
		lua_setmetatable(L, -2);
		/* memoize it */
		lua_settable(L, -3);
		lua_pushinteger(L, f->fid);
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
	uint32_t fid = *(uint32_t *) luaL_checkudata(L, index, fiberlib_name);
	struct fiber *f = fiber_find(fid);
	if (f == NULL)
		luaL_error(L, "fiber.resume(): the fiber is dead");
	return f;
}

static struct fiber *
lbox_isfiber(struct lua_State *L, int narg)
{
	if (lua_getmetatable(L, narg) == 0)
		return NULL;
	luaL_getmetatable(L, fiberlib_name);
	struct fiber *f = NULL;
	if (lua_equal(L, -1, -2))
		f = fiber_find(* (int *) lua_touserdata(L, narg));
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
	lua_pushinteger(child_L, fiber->fid);
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
	struct fiber *caller = fiber_find(lua_tointeger(L, -1));
	/* Pop the caller, the callers table, the fiberlib metatable. */
	lua_pop(L, 3);
	return caller;
}

static int
lbox_fiber_gc(struct lua_State *L)
{
	if (lua_gettop(L) == 0)
		return 0;
	struct fiber *f = lbox_isfiber(L, 1);
	if (f == NULL)
		return 0;
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
	fiber_set_sid(fiber, 0, 0);
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
	} catch (const FiberCancelException &e) {
		if (box_lua_fiber_get_coro(L, fiber)) {
			struct fiber *caller = box_lua_fiber_get_caller(L);
			fiber_wakeup(caller);
		}
		box_lua_fiber_clear_coro(tarantool_L, fiber);
		/*
		 * Note: FiberCancelException leaves garbage on
		 * coroutine stack. This is OK since it is only
		 * possible to cancel a fiber which is not
		 * scheduled, and cancel() is synchronous.
		 */

		cleanup();
		throw;
	} catch (const Exception &e) {
		/* pop any possible garbage */
		lua_settop(L, 0);
		/* completion status */
		lua_pushboolean(L, false);
		/* error message */
		lua_pushstring(L, e.errmsg());

		/* Always log the error. */
		e.log();
		cleanup();
	} catch (...) {
		lua_settop(L, 1);
		/*
		 * The error message is already there.
		 * Add completion status.
		 */
		lua_pushboolean(L, false);
		lua_insert(L, -2);
		/* Always log the error. */
		say_error("%s", lua_tostring(L, -1));

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
	fiber_set_sid(f, fiber->sid, fiber->cookie);
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
	 * The called fiber could have done 4 things:
	 * - yielded to us (then we should grab its return)
	 * - completed (grab return values, wake up the fiber,
	 *   so that it can die)
	 * - detached (grab return values, wakeup the fiber so it
	 *   can continue).
	 * - got cancelled (return)
	 */
	if (f->fid != fid)
		luaL_error(L, "fiber.resume(): the child fiber got cancelled");

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
	struct lua_State *coro_L;
	struct fiber *caller;
	if ((coro_L = box_lua_fiber_get_coro(L, fiber)) &&
	     (caller = box_lua_fiber_get_caller(coro_L))) {
		lua_pushinteger(L, YIELD);
		fiber_yield_to(caller);
	} else {
		fiber_wakeup(fiber);
		fiber_yield();
		fiber_testcancel();
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
fiber_is_caller(struct lua_State *L, struct fiber *f)
{
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
	struct fiber *f;
	if (lua_gettop(L)) {
		uint32_t fid = *(uint32_t *)
			luaL_checkudata(L, 1, fiberlib_name);
		f = fiber_find(fid);
	} else {
		f = fiber;
	}
	const char *status;
	if (f == NULL || f->fid == 0) {
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
	if (lua_gettop(L) >= 1 && lbox_isfiber(L, 1)) {
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
	if (lua_gettop(L) != 1)
		luaL_error(L, "fiber.find(): bad arguments");
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
	{"cancel", lbox_fiber_cancel},
	{"resume", lbox_fiber_resume},
	{"__gc", lbox_fiber_gc},
	{NULL, NULL}
};

static const struct luaL_reg fiberlib[] = {
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

/*
 * }}}
 */

const char *
tarantool_lua_tostring(struct lua_State *L, int index)
{
	/* we need an absolute index */
	if (index < 0)
		index = lua_gettop(L) + index + 1;
	lua_getglobal(L, "tostring");
	lua_pushvalue(L, index);
	/* pops both "tostring" and its argument */
	lua_call(L, 1, 1);
	lua_replace(L, index);
	return lua_tostring(L, index);
}

/**
 * Convert Lua stack to YAML and append to the given tbuf.
 */
static void
tarantool_lua_printstack_yaml(struct lua_State *L, struct tbuf *out)
{
	int top = lua_gettop(L);
	for (int i = 1; i <= top; i++) {
		if (lua_type(L, i) == LUA_TCDATA) {
			GCcdata *cd = cdataV(L->base + i - 1);
			const char *sz = tarantool_lua_tostring(L, i);
			int len = strlen(sz);
			int chop;
			switch (cd->ctypeid){
			case CTID_UINT64:
				chop = 3;
				break;
			case CTID_INT64:
				chop = 2;
				break;
			default:
				chop = 0;
			}
			tbuf_printf(out, " - %-.*s" CRLF, len - chop, sz);
		} else
			tbuf_printf(out, " - %s" CRLF,
				    tarantool_lua_tostring(L, i));
	}
}

/**
 * A helper to serialize arguments of 'print' Lua built-in
 * to tbuf.
 */
static void
tarantool_lua_printstack(struct lua_State *L, struct tbuf *out)
{
	int top = lua_gettop(L);
	for (int i = 1; i <= top; i++) {
		if (lua_type(L, i) == LUA_TCDATA) {
			GCcdata *cd = cdataV(L->base + i - 1);
			const char *sz = tarantool_lua_tostring(L, i);
			int len = strlen(sz);
			int chop = (cd->ctypeid == CTID_UINT64 ? 3 : 2);
			tbuf_printf(out, "%-.*s", len - chop, sz);
		} else
			tbuf_printf(out, "%s", tarantool_lua_tostring(L, i));
	}
}

/**
 * Redefine lua 'print' built-in to print either to the log file
 * (when Lua is used inside a module) or back to the user (for the
 * administrative console).
 *
 * When printing to the log file, we use 'say_info'.  To print to
 * the administrative console, we simply append everything to the
 * 'out' buffer, which is flushed to network at the end of every
 * administrative command.
 *
 * Note: administrative console output must be YAML-compatible.
 * If this is done automatically, the result is ugly, so we
 * don't do it. Creators of Lua procedures have to do it
 * themselves. Best we can do here is to add a trailing
 * CRLF if it's forgotten.
 */
static int
lbox_print(struct lua_State *L)
{
	lua_pushthread(L);
	lua_gettable(L, LUA_REGISTRYINDEX);
	struct tbuf *out = (struct tbuf *) lua_topointer(L, -1);
	/* pop 'out' */
	lua_pop(L, 1);

	if (out) {
		/* Administrative console */
		tarantool_lua_printstack(L, out);
		/* Courtesy: append YAML's end of line if it's not already there */
		if (out->size < 2 || tbuf_str(out)[out->size-1] != '\n')
			tbuf_printf(out, CRLF);
	} else {
		size_t allocated = palloc_allocated(fiber->gc_pool);
		auto scoped_guard = make_scoped_guard([=] {
			ptruncate(fiber->gc_pool, allocated);
		});

		/* Add a message to the server log */
		out = tbuf_new(fiber->gc_pool);
		tarantool_lua_printstack(L, out);
		say_info("%s", tbuf_str(out));
	}
	return 0;
}

/**
 * Redefine lua 'pcall' built-in to correctly handle exceptions,
 * produced by 'box' C functions.
 *
 * See Lua documentation on 'pcall' for additional information.
 */
static int
lbox_pcall(struct lua_State *L)
{
	/*
	 * Lua pcall() returns true/false for completion status
	 * plus whatever the called function returns.
	 */
	try {
		lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
		/* push completion status */
		lua_pushboolean(L, true);
		/* move 'true' to stack start */
		lua_insert(L, 1);
	} catch (const ClientError& e) {
		/*
		 * Note: FiberCancelException passes through this
		 * catch and thus leaves garbage on coroutine
		 * stack.
		 */
		/* pop any possible garbage */
		lua_settop(L, 0);
		/* completion status */
		lua_pushboolean(L, false);
		/* error message */
		lua_pushstring(L, e.errmsg());
	} catch (const Exception& e) {
		throw;
	} catch (...) {
		lua_settop(L, 1);
		/* completion status */
		lua_pushboolean(L, false);
		/* put the completion status below the error message. */
		lua_insert(L, -2);
	}
	return lua_gettop(L);
}

/**
 * Convert lua number or string to lua cdata 64bit number.
 */
static int
lbox_tonumber64(struct lua_State *L)
{
	if (lua_gettop(L) != 1)
		luaL_error(L, "tonumber64: wrong number of arguments");
	uint64_t result = tarantool_lua_tointeger64(L, 1);
	return luaL_pushnumber64(L, result);
}



/**
 * A helper to register a single type metatable.
 */
void
tarantool_lua_register_type(struct lua_State *L, const char *type_name,
			    const struct luaL_Reg *methods)
{
	luaL_newmetatable(L, type_name);
	/*
	 * Conventionally, make the metatable point to itself
	 * in __index. If 'methods' contain a field for __index,
	 * this is a no-op.
	 */
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushstring(L, type_name);
	lua_setfield(L, -2, "__metatable");
	luaL_register(L, NULL, methods);
	lua_pop(L, 1);
}

static const struct luaL_reg errorlib [] = {
	{NULL, NULL}
};

static void
tarantool_lua_error_init(struct lua_State *L) {
	luaL_register(L, "box.error", errorlib);
	for (int i = 0; i < tnt_error_codes_enum_MAX; i++) {
		const char *name = tnt_error_codes[i].errstr;
		if (strstr(name, "UNUSED") || strstr(name, "RESERVED"))
			continue;
		lua_pushnumber(L, tnt_errcode_val(i));
		lua_setfield(L, -2, name);
	}
	lua_pop(L, 1);
}

static void
tarantool_lua_setpath(struct lua_State *L, const char *type, ...)
__attribute__((sentinel));

/**
 * Prepend the variable list of arguments to the Lua
 * package search path (or cpath, as defined in 'type').
 */
static void
tarantool_lua_setpath(struct lua_State *L, const char *type, ...)
{
	char path[PATH_MAX];
	va_list args;
	va_start(args, type);
	int off = 0;
	const char *p;
	while ((p = va_arg(args, const char*))) {
		/*
		 * If MODULE_PATH is an empty string, skip it.
		 */
		if (*p == '\0')
			continue;
		off += snprintf(path + off, sizeof(path) - off, "%s;", p);
	}
	va_end(args);
	lua_getglobal(L, "package");
	lua_getfield(L, -1, type);
	snprintf(path + off, sizeof(path) - off, "%s",
	         lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_pushstring(L, path);
	lua_setfield(L, -2, type);
	lua_pop(L, 1);
}

struct lua_State *
tarantool_lua_init()
{
	lua_State *L = luaL_newstate();
	if (L == NULL)
		return L;
	luaL_openlibs(L);
	/*
	 * Search for Lua modules, apart from the standard
	 * locations, in the server script_dir and in the
	 * system-wide Tarantool paths. This way 3 types
	 * of packages become available for use: standard Lua
	 * packages, Tarantool-specific Lua libs and
	 * instance-specific Lua scripts.
	 */

	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/?.lua", cfg.script_dir);
	tarantool_lua_setpath(L, "path", path, MODULE_LUAPATH, NULL);
	snprintf(path, sizeof(path), "%s/?.so", cfg.script_dir);
	tarantool_lua_setpath(L, "cpath", path, MODULE_LIBPATH, NULL);

	/* Loadi 'ffi' extension and make it inaccessible */
	lua_getglobal(L, "require");
	lua_pushstring(L, "ffi");
	if (lua_pcall(L, 1, 0, 0) != 0)
		panic("%s", lua_tostring(L, -1));
	lua_getglobal(L, "ffi");
	/**
	 * Remember the LuaJIT FFI extension reference index
	 * to protect it from being garbage collected.
	 */
	(void) luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushnil(L);
	lua_setglobal(L, "ffi");
	luaL_register(L, boxlib_name, boxlib);
	lua_pop(L, 1);
	luaL_register(L, fiberlib_name, fiberlib);
	lua_pop(L, 1);
	tarantool_lua_register_type(L, fiberlib_name, lbox_fiber_meta);
	lua_register(L, "print", lbox_print);
	lua_register(L, "pcall", lbox_pcall);
	lua_register(L, "tonumber64", lbox_tonumber64);

	tarantool_lua_cjson_init(L);
	tarantool_lua_info_init(L);
	tarantool_lua_slab_init(L);
	tarantool_lua_stat_init(L);
	tarantool_lua_ipc_init(L);
	tarantool_lua_socket_init(L);
	tarantool_lua_session_init(L);
	tarantool_lua_error_init(L);

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s++) {
		if (luaL_dostring(L, *s))
			panic("Error loading Lua source %.160s...: %s",
			      *s, lua_tostring(L, -1));
	}

	mod_lua_init(L);

	/* clear possible left-overs of init */
	lua_settop(L, 0);
	return L;
}

void
tarantool_lua_close(struct lua_State *L)
{
	/* collects garbage, invoking userdata gc */
	lua_close(L);
}

/**
 * Attempt to append 'return ' before the chunk: if the chunk is
 * an expression, this pushes results of the expression onto the
 * stack. If the chunk is a statement, it won't compile. In that
 * case try to run the original string.
 */
static int
tarantool_lua_dostring(struct lua_State *L, const char *str)
{
	struct tbuf *buf = tbuf_new(fiber->gc_pool);
	size_t allocated = palloc_allocated(fiber->gc_pool);
	tbuf_printf(buf, "%s%s", "return ", str);
	int r = luaL_loadstring(L, tbuf_str(buf));
	ptruncate(fiber->gc_pool, allocated);
	if (r) {
		/* pop the error message */
		lua_pop(L, 1);
		r = luaL_loadstring(L, str);
		if (r)
			return r;
	}
	try {
		lua_call(L, 0, LUA_MULTRET);
	} catch (const FiberCancelException& e) {
		throw;
	} catch (const Exception& e) {
		lua_pushstring(L, e.errmsg());
		return 1;
	} catch (...) {
		return 1;
	}
	return 0;
}

static int
tarantool_lua_dofile(struct lua_State *L, const char *filename)
{
	lua_getglobal(L, "dofile");
	lua_pushstring(L, filename);
	lbox_pcall(L);
	bool result = lua_toboolean(L, 1);
	return result ? 0 : 1;
}

void
tarantool_lua(struct lua_State *L,
	      struct tbuf *out, const char *str)
{
	tarantool_lua_set_out(L, out);
	int r = tarantool_lua_dostring(L, str);
	tarantool_lua_set_out(L, NULL);
	if (r) {
		const char *msg = lua_tostring(L, -1);
		msg = msg ? msg : "";
		/* Make sure the output is YAMLish */
		tbuf_printf(out, "error: '%s'" CRLF,
			    luaL_gsub(L, msg, "'", "''"));
	} else {
		tarantool_lua_printstack_yaml(L, out);
	}
	/* clear the stack from return values. */
	lua_settop(L, 0);
}

/**
 * Check if the given literal is a number/boolean or string
 * literal. A string literal needs quotes.
 */
static bool
is_string(const char *str)
{
	if (strcmp(str, "true") == 0 || strcmp(str, "false") == 0)
	    return false;
	if (! isdigit(*str))
	    return true;
	char *endptr;
	double r = strtod(str, &endptr);
	/* -Wunused-result warning suppression */
	(void) r;
	return *endptr != '\0';
}

/**
 * Make a new configuration available in Lua.
 * We could perhaps make Lua bindings to access the C
 * structure in question, but for now it's easier and just
 * as functional to convert the given configuration to a Lua
 * table and export the table into Lua.
 */
void
tarantool_lua_load_cfg(struct lua_State *L, struct tarantool_cfg *cfg)
{
	luaL_Buffer b;
	char *key, *value;

	luaL_buffinit(L, &b);
	tarantool_cfg_iterator_t *i = tarantool_cfg_iterator_init();
	luaL_addstring(&b,
		       "box.cfg = {}\n"
		       "setmetatable(box.cfg, {})\n"
		       "getmetatable(box.cfg).__index = "
		       "function(table, index)\n"
		       "  table[index] = {}\n"
		       "  setmetatable(table[index], getmetatable(table))\n"
		       "  return rawget(table, index)\n"
		       "end\n");
	while ((key = tarantool_cfg_iterator_next(i, cfg, &value)) != NULL) {
		if (value == NULL)
			continue;
		const char *quote = is_string(value) ? "'" : "";
		if (strchr(key, '.') == NULL) {
			lua_pushfstring(L, "box.cfg.%s = %s%s%s\n",
					key, quote, value, quote);
			luaL_addvalue(&b);
		}
		free(value);
	}

	luaL_addstring(&b,
		       "getmetatable(box.cfg).__newindex = "
		       "function(table, index)\n"
		       "  error('Attempt to modify a read-only table')\n"
		       "end\n"
		       "getmetatable(box.cfg).__index = nil\n");
	luaL_pushresult(&b);
	if (luaL_loadstring(L, lua_tostring(L, -1)) != 0 ||
	    lua_pcall(L, 0, 0, 0) != 0) {
		panic("%s", lua_tostring(L, -1));
	}
	lua_pop(L, 1);	/* cleanup stack */

	box_lua_load_cfg(L);
	/*
	 * Invoke a user-defined on_reload_configuration hook,
	 * if it exists. Do it after everything else is done.
	 */
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushstring(L, "on_reload_configuration");
	lua_gettable(L, -2);
	if (lua_isfunction(L, -1) && lua_pcall(L, 0, 0, 0) != 0) {
		say_error("on_reload_configuration() hook failed: %s",
			  lua_tostring(L, -1));
	}
	lua_pop(L, 1);	/* cleanup stack */
}

/**
 * Load start-up file routine.
 */
static void
load_init_script(va_list ap)
{
	struct lua_State *L = va_arg(ap, struct lua_State *);

	/*
	 * Return control to tarantool_lua_load_init_script.
	 * tarantool_lua_load_init_script when will start an auxiliary event
	 * loop and re-schedule this fiber.
	 */
	fiber_sleep(0.0);

	char path[PATH_MAX + 1];
	snprintf(path, PATH_MAX, "%s/%s",
		 cfg.script_dir, TARANTOOL_LUA_INIT_SCRIPT);

	if (access(path, F_OK) == 0) {
		say_info("loading %s", path);
		/* Execute the init file. */
		if (tarantool_lua_dofile(L, path))
			panic("%s", lua_tostring(L, -1));

		/* clear the stack from return values. */
		lua_settop(L, 0);
	}
	/*
	 * The file doesn't exist. It's OK, tarantool may
	 * have no init file.
	 */

	/*
	 * Lua script finished. Stop the auxiliary event loop and
	 * return control back to tarantool_lua_load_init_script.
	 */
	ev_break(EVBREAK_ALL);
}

/**
 * Unset functions in the Lua state which can be used to
 * execute external programs or otherwise introduce a breach
 * in security.
 *
 * @param L is a Lua State.
 */
static void
tarantool_lua_sandbox(struct lua_State *L)
{
	/*
	 * Unset some functions for security reasons:
	 * 1. Some os.* functions (like os.execute, os.exit, etc..)
	 * 2. require(), since it can be used to provide access to ffi
	 * or anything else we unset in 1.
	 * 3. package, because it can be used to invoke require or to get
	 * any builtin module using package.loaded
	 */
	int result = tarantool_lua_dostring(L,
					    "os.execute = nil\n"
					    "os.exit = nil\n"
					    "os.rename = nil\n"
					    "os.tmpname = nil\n"
					    "os.remove = nil\n"
					    "io = nil\n"
					    "require = nil\n"
					    "package = nil\n");

	if (result)
		panic("%s", lua_tostring(L, -1));
}

void
tarantool_lua_load_init_script(struct lua_State *L)
{
	/*
	 * init script can call box.fiber.yield (including implicitly via
	 * box.insert, box.update, etc...), but box.fiber.yield() today,
	 * which, when called from 'sched' fiber crashes the server.
	 * To work this problem around we must run init script in
	 * a separate fiber.
	 */
	struct fiber *loader = fiber_new(TARANTOOL_LUA_INIT_SCRIPT,
					    load_init_script);
	fiber_call(loader, L);

	/*
	 * Run an auxiliary event loop to re-schedule load_init_script fiber.
	 * When this fiber finishes, it will call ev_break to stop the loop.
	 */
	ev_run(0);

	/* Outside the startup file require() or ffi are not
	 * allowed.
	*/
	tarantool_lua_sandbox(tarantool_L);
}

void *
lua_region_alloc(void *ctx, size_t size)
{
	struct lua_State *L = (struct lua_State *) ctx;
	return lua_newuserdata(L, size);
}
