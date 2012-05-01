/*
 * Copyright (C) 2011 Yuriy Vostrikov
 * Copyright (C) 2011 Konstantin Osipov
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
#include "tarantool.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"
#include "lj_ctype.h"
#include "lj_cdata.h"
#include "lj_cconv.h"
#include "lj_state.h"

#include "pickle.h"
#include "fiber.h"
#include <ctype.h>
#include TARANTOOL_CONFIG

/** tarantool start-up file */
#define TARANTOOL_LUA_INIT_SCRIPT "init.lua"

struct lua_State *tarantool_L;

/* Remember the output of the administrative console in the
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

/* dup out from parent to child L. Used in fiber_create */

void
tarantool_lua_dup_out(struct lua_State *L, struct lua_State *child_L)
{
	lua_pushthread(L);
	lua_gettable(L, LUA_REGISTRYINDEX);
	struct tbuf *out = (struct tbuf *) lua_topointer(L, -1);
	lua_pop(L, 1); /* pop 'out' */
	if (out)
		tarantool_lua_set_out(child_L, out);
}


/** {{{ box Lua library: common functions
 */

const char *boxlib_name = "box";

/** Pack our BER integer into luaL_Buffer */
static void
luaL_addvarint32(luaL_Buffer *b, u32 u32)
{
	char varint_buf[sizeof(u32)+1];
	struct tbuf tbuf = { .size = 0, .capacity = sizeof(varint_buf),
		.data = varint_buf };
	write_varint32(&tbuf, u32);
	luaL_addlstring(b, tbuf.data, tbuf.size);
}

uint64_t
tarantool_lua_tointeger64(struct lua_State *L, int idx)
{
	uint64_t result = 0;
	switch (lua_type(L, idx)) {
	case LUA_TNUMBER:
		result = lua_tointeger(L, idx);
		break;
	case LUA_TSTRING: {
		const char *arg = luaL_checkstring(L, idx);
		char *arge;
		errno = 0;
		result = strtoull(arg, &arge, 10);
		if (errno != 0 || arge == arg)
			luaL_error(L, "lua_tointeger64: bad argument");
		break;
	}
	case LUA_TCDATA: {
		if (lua_type(L, idx) != LUA_TCDATA)
			luaL_error(L, "lua_tointeger64: cdata expected");
		GCcdata *cd = cdataV(L->base + (idx - 1));
		if (cd->typeid != CTID_INT64 && cd->typeid != CTID_UINT64)
			luaL_error(L, "lua_tointeger64: unsupported cdata type");
		result = *(uint64_t*)cdataptr(cd);
		break;
	}
	default:
		luaL_error(L, "lua_tointeger64: unsupported type");
	}
	return result;
}

/* Convert box.pack() format specifier to Tarantool
 * binary protocol UPDATE opcode
 */
static char format_to_opcode(char format)
{
	switch (format) {
	case '=': return 0;
	case '+': return 1;
	case '&': return 2;
	case '^': return 3;
	case '|': return 4;
	case ':': return 5;
	case '#': return 6;
	case '!': return 7;
	default: return format;
	}
}

/**
 * To use Tarantool/Box binary protocol primitives from Lua, we
 * need a way to pack Lua variables into a binary representation.
 * We do it by exporting a helper function
 *
 * box.pack(format, args...)
 *
 * which takes the format, which is very similar to Perl 'pack'
 * format, and a list of arguments, and returns a binary string
 * which has the arguments packed according to the format.
 *
 * For example, a typical SELECT packet packs in Lua like this:
 *
 * pkt = box.pack("iiiiiip", -- pack format
 *                         0, -- space id
 *                         0, -- index id
 *                         0, -- offset
 *                         2^32, -- limit
 *                         1, -- number of SELECT arguments
 *                         1, -- tuple cardinality
 *                         key); -- the key to use for SELECT
 *
 * @sa doc/box-protocol.txt, binary protocol description
 * @todo: implement box.unpack(format, str), for testing purposes
 */
static int
lbox_pack(struct lua_State *L)
{
	luaL_Buffer b;
	const char *format = luaL_checkstring(L, 1);
	int i = 2; /* first arg comes second */
	int nargs = lua_gettop(L);
	u32 u32buf;
	u64 u64buf;
	size_t size;
	const char *str;

	luaL_buffinit(L, &b);

	while (*format) {
		if (i > nargs)
			luaL_error(L, "box.pack: argument count does not match the format");
		switch (*format) {
		case 'B':
		case 'b':
			/* signed and unsigned 8-bit integers */
			u32buf = lua_tointeger(L, i);
			if (u32buf > 0xff)
				luaL_error(L, "box.pack: argument too big for 8-bit integer");
			luaL_addchar(&b, (char) u32buf);
			break;
		/* signed and unsigned 32-bit integers */
		case 'I':
		case 'i':
		{
			u32buf = lua_tointeger(L, i);
			luaL_addlstring(&b, (char *) &u32buf, sizeof(u32));
			break;
		}
		case 'L':
		case 'l':
		{
			u64buf = tarantool_lua_tointeger64(L, i);
			luaL_addlstring(&b, (char *) &u64buf, sizeof(u64));
			break;
		}
		/* Perl 'pack' BER-encoded integer */
		case 'w':
			luaL_addvarint32(&b, lua_tointeger(L, i));
			break;
		/* A sequence of bytes */
		case 'A':
		case 'a':
			str = luaL_checklstring(L, i, &size);
			luaL_addlstring(&b, str, size);
			break;
		case 'P':
		case 'p':
			if (lua_type(L, i) == LUA_TNUMBER) {
				u32buf = (u32) lua_tointeger(L, i);
				str = (char *) &u32buf;
				size = sizeof(u32);
			} else
			if (lua_type(L, i) == LUA_TCDATA) {
				u64buf = tarantool_lua_tointeger64(L, i);
				str = (char *) &u64buf;
				size = sizeof(u64);
			} else {
				str = luaL_checklstring(L, i, &size);
			}
			luaL_addvarint32(&b, size);
			luaL_addlstring(&b, str, size);
			break;
		case '=': /* update tuple set foo=bar */
		case '+': /* set field+=val */
		case '&': /* set field&=val */
		case '|': /* set field|=val */
		case '^': /* set field^=val */
		case ':': /* splice */
		case '#': /* delete field */
		case '!': /* insert field */
			u32buf= (u32) lua_tointeger(L, i); /* field no */
			luaL_addlstring(&b, (char *) &u32buf, sizeof(u32));
			luaL_addchar(&b, format_to_opcode(*format));
			break;
		default:
			luaL_error(L, "box.pack: unsupported pack "
				   "format specifier '%c'", *format);
		} /* end switch */
		i++;
		format++;
	}
	luaL_pushresult(&b);
	return 1;
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
	lj_cconv_ct_init(cts, ct, sz, cdataptr(cd), o, 0);
	incr_top(L);
	return cd;
}

static int
luaL_pushnumber64(struct lua_State *L, uint64_t val)
{
	GCcdata *cd = luaL_pushcdata(L, CTID_UINT64, 8);
	*(uint64_t*)cdataptr(cd) = val;
	return 1;
}

static int
lbox_unpack(struct lua_State *L)
{
	const char *format = luaL_checkstring(L, 1);
	int i = 2; /* first arg comes second */
	int nargs = lua_gettop(L);
	size_t size;
	const char *str;
	u32 u32buf;

	while (*format) {
		if (i > nargs)
			luaL_error(L, "box.unpack: argument count does not match the format");
		switch (*format) {
		case 'i':
			str = lua_tolstring(L, i, &size);
			if (str == NULL || size != sizeof(u32))
				luaL_error(L, "box.unpack('%c'): got %d bytes (expected: 4)", *format, (int) size);
			u32buf = * (u32 *) str;
			lua_pushnumber(L, u32buf);
			break;
		case 'l':
		{
			str = lua_tolstring(L, i, &size);
			if (str == NULL || size != sizeof(u64))
				luaL_error(L, "box.unpack('%c'): got %d bytes (expected: 8)", *format, (int) size);
			GCcdata *cd = luaL_pushcdata(L, CTID_UINT64, 8);
			uint64_t *u64buf = (uint64_t*)cdataptr(cd);
			*u64buf = *(u64*)str;
			break;
		}
		default:
			luaL_error(L, "box.unpack: unsupported pack "
				   "format specifier '%c'", *format);
		} /* end switch */
		i++;
		format++;
	}
	return i-2;
}

/** A descriptor for box methods */

static const struct luaL_reg boxlib[] = {
	{"pack", lbox_pack},
	{"unpack", lbox_unpack},
	{NULL, NULL}
};

/* }}} */

/** {{{ box.fiber Lua library: access to Tarantool/Box fibers
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

/** Push a userdata for the given fiber onto Lua stack. */
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
	if (lua_isnil(L, -1)) { /* first access - instantiate memoize */
		lua_pop(L, 1);                  /* pop the nil */
		lua_newtable(L);                /* create memoize table */
		lua_newtable(L);                /* and a metatable */
		lua_pushstring(L, "kv"); /* weak keys and values */
		lua_setfield(L, -2, "__mode"); /* pops 'kv' */
		lua_setmetatable(L, -2); /* pops the metatable */
		lua_setfield(L, -2, "memoize"); /* assigns and pops memoize */
		lua_getfield(L, -1, "memoize"); /* gets memoize back. */
		assert(! lua_isnil(L, -1));
	}
	/* Find out whether the fiber is  already in the memoize table. */
	lua_pushlightuserdata(L, f);
	lua_gettable(L, -2);
	if (lua_isnil(L, -1)) { /* no userdata for fiber created so far */
		lua_pop(L, 1);                  /* pop the nil */
		lua_pushlightuserdata(L, f);    /* push the key back */
		/* create a new userdata */
		void **ptr = lua_newuserdata(L, sizeof(void *));
		*ptr = f;
		luaL_getmetatable(L, fiberlib_name);
		lua_setmetatable(L, -2);
		lua_settable(L, -3);            /* memoize it */
		lua_pushlightuserdata(L, f);
		lua_gettable(L, -2);            /* get it back */
	}
	/*
	 * Here we have a userdata on top of the stack and
	 * possibly some garbage just under the top. Move the
	 * result to the beginning of the stack and clear the rest.
	 */
	lua_replace(L, top); /* moves the current top to the old top */
	lua_settop(L, top); /* clears everything after the old top */
}

static struct fiber *
lbox_checkfiber(struct lua_State *L, int index)
{
	return *(void **) luaL_checkudata(L, index, fiberlib_name);
}

static int
lbox_fiber_id(struct lua_State *L)
{
	struct fiber *f = lbox_checkfiber(L, 1);
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
		/* Garbage collect the associated coro.
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


static void
box_lua_fiber_run(void *arg __attribute__((unused)))
{
	fiber_setcancelstate(true);
	fiber_testcancel();
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
	@try {
		lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
		lua_pushboolean(L, true); /* push completion status */
		lua_insert(L, 1); /* move 'true' to stack start */
	} @catch (ClientError *e) {
		/*
		 * Note: FiberCancelException passes through this
		 * catch and thus leaves garbage on coroutine
		 * stack. This is OK since it is only possible to
		 * cancel a fiber which is not scheduled, and
		 * cancel() is synchronous.
		 */
		lua_settop(L, 0); /* pop any possible garbage */
		lua_pushboolean(L, false); /* completion status */
		lua_pushstring(L, e->errmsg); /* error message */
	} @finally {
		/*
		 * If the coroutine has detached itself, collect
		 * its resources here.
		 */
		luaL_unref(L, LUA_REGISTRYINDEX, coro_ref);
	}
	/* L stack contains nothing but call results */
}

static int
lbox_fiber_create(struct lua_State *L)
{
        if (lua_gettop(L) != 1 || !lua_isfunction(L, 1))
                luaL_error(L, "fiber.create(function): bad arguments");
	if (fiber_checkstack()) {
		luaL_error(L, "fiber.create(function): recursion limit"
			   " reached");
	}
	struct fiber *f= fiber_create("lua", -1, box_lua_fiber_run, NULL);

	lua_pushlightuserdata(L, f); /* associate coro with fiber */
	struct lua_State *child_L = lua_newthread(L);
	lua_settable(L, LUA_REGISTRYINDEX);
	lua_xmove(L, child_L, 1); /* move the argument to the new coro */
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
	fiber_call(f);
	tarantool_lua_set_out(child_L, NULL);
	/* Get the results */
	nargs = lua_gettop(child_L);
	lua_xmove(child_L, L, nargs);
	if (f->f == 0) { /* The fiber is dead. */
		/* Garbage collect the associated coro. */
		box_lua_fiber_clear_coro(L, f);
	}
	return nargs;
}

/** Yield the current fiber.
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
	fiber_yield();
	fiber_testcancel(); /* throws an error if we were cancelled. */
	/*
	 * Got resumed. Return whatever the caller has passed
	 * to us with box.fiber.resume().
	 * As a side effect, the detached fiber which yields
	 * to sched always gets back whatever it yields.
	 */
	return lua_gettop(L);
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
	struct fiber *f = lbox_checkfiber(L, 1);
	const char *status;
	if (f->fid == 0) {
		/* This fiber is dead. */
		status = "dead";
	} else if (f == fiber) {
		/* The fiber is the current running fiber. */
		status = "running";
	} else if (fiber_is_caller(f)) {
		/* The fiber is current fiber's caller. */
		status = "normal";
	} else {
		/* None of the above: must be suspended. */
		status = "suspended";
	}
	lua_pushstring(L, status);
	return 1;
}

/**
 * Detach the current fiber.
 */
static int
lbox_fiber_detach(struct lua_State *L)
{
	struct lua_State *self_L = box_lua_fiber_get_coro(L, fiber);
	if (self_L == NULL)
		luaL_error(L, "fiber.detach(): not attached");
	assert(self_L == L);
	/* Clear our association with the parent. */
	box_lua_fiber_clear_coro(L, fiber);
	tarantool_lua_set_out(L, NULL);
	/* Make sure we get awoken, at least once. */
	fiber_wakeup(fiber);
	/* Yield to the parent. */
	fiber_yield();
	fiber_testcancel(); /* check if we were cancelled. */
	return 0;
}

/** Yield to the sched fiber and sleep.
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

/** Running and suspended fibers can be cancelled.
 * Zombie fibers can't.
 */
static int
lbox_fiber_cancel(struct lua_State *L)
{
	struct fiber *f = lbox_checkfiber(L, 1);
	if (! (f->flags & FIBER_CANCELLABLE))
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
	{"__gc", lbox_fiber_gc},
	{NULL, NULL}
};

static const struct luaL_reg fiberlib[] = {
	{"sleep", lbox_fiber_sleep},
	{"self", lbox_fiber_self},
	{"find", lbox_fiber_find},
	{"cancel", lbox_fiber_cancel},
	{"testcancel", lbox_fiber_testcancel},
	{"create", lbox_fiber_create},
	{"resume", lbox_fiber_resume},
	{"yield", lbox_fiber_yield},
	{"status", lbox_fiber_status},
	{"detach", lbox_fiber_detach},
	{NULL, NULL}
};

/* }}} */

/*
 * This function exists because lua_tostring does not use
 * __tostring metamethod, and this metamethod has to be used
 * if we want to print Lua userdata correctly.
 */
const char *
tarantool_lua_tostring(struct lua_State *L, int index)
{
	if (index < 0) /* we need an absolute index */
		index = lua_gettop(L) + index + 1;
	lua_getglobal(L, "tostring");
	lua_pushvalue(L, index);
	lua_call(L, 1, 1); /* pops both "tostring" and its argument */
	lua_replace(L, index);
	return lua_tostring(L, index);
}

/*
 * Convert Lua stack to YAML and append to
 * the given tbuf.
 */
static void
tarantool_lua_printstack_yaml(struct lua_State *L, struct tbuf *out)
{
	int top = lua_gettop(L);
	for (int i = 1; i <= top; i++) {
		if (lua_type(L, i) == LUA_TCDATA) {
			const char *sz = tarantool_lua_tostring(L, i);
			int len = strlen(sz);
			tbuf_printf(out, " - %-.*s\r\n", len - 3, sz);
		} else
			tbuf_printf(out, " - %s\r\n", tarantool_lua_tostring(L, i));
	}
}

/*
 * A helper to serialize arguments of 'print' Lua built-in
 * to tbuf.
 */
static void
tarantool_lua_printstack(struct lua_State *L, struct tbuf *out)
{
	int top = lua_gettop(L);
	for (int i = 1; i <= top; i++) {
		if (lua_type(L, i) == LUA_TCDATA) {
			const char *sz = tarantool_lua_tostring(L, i);
			int len = strlen(sz);
			tbuf_printf(out, "%-.*s\r\n", len - 3, sz);
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
 * \r\n if it's forgotten.
 */
static int
lbox_print(struct lua_State *L)
{
	lua_pushthread(L);
	lua_gettable(L, LUA_REGISTRYINDEX);
	struct tbuf *out = (struct tbuf *) lua_topointer(L, -1);
	lua_pop(L, 1); /* pop 'out' */

	if (out) { /* Administrative console */
		tarantool_lua_printstack(L, out);
		/* Courtesy: append YAML's \r\n if it's not already there */
		if (out->size < 2 || tbuf_str(out)[out->size-1] != '\n')
			tbuf_printf(out, "\r\n");
	} else { /* Add a message to the server log */
		out = tbuf_alloc(fiber->gc_pool);
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
	@try {
		lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
		lua_pushboolean(L, true); /* push completion status */
		lua_insert(L, 1); /* move 'true' to stack start */
	} @catch (ClientError *e) {
		/*
		 * Note: FiberCancelException passes through this
		 * catch and thus leaves garbage on coroutine
		 * stack.
		 */
		lua_settop(L, 0); /* pop any possible garbage */
		lua_pushboolean(L, false); /* completion status */
		lua_pushstring(L, e->errmsg); /* error message */
	}
	return lua_gettop(L);
}

/*
 * Convert lua number or string to lua cdata 64bit number.
 */
static int
lbox_tonumber64(struct lua_State *L)
{
	uint64_t result = tarantool_lua_tointeger64(L, -1);
	return luaL_pushnumber64(L, result);
}

/** A helper to register a single type metatable. */
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

/**
 * Remember the LuaJIT FFI extension reference index
 * to protect it from being garbage collected.
 */
static int ffi_ref = 0;

struct lua_State *
tarantool_lua_init()
{
	lua_State *L = luaL_newstate();
	if (L == NULL)
		return L;
	luaL_openlibs(L);
	/* Loading 'ffi' extension and making it inaccessible */
	lua_getglobal(L, "require");
	lua_pushstring(L, "ffi");
	if (lua_pcall(L, 1, 0, 0) != 0)
		panic("%s", lua_tostring(L, -1));
	lua_getglobal(L, "ffi");
	ffi_ref = luaL_ref(L, LUA_REGISTRYINDEX);
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
	L = mod_lua_init(L);
	lua_settop(L, 0); /* clear possible left-overs of init */
	return L;
}

void
tarantool_lua_close(struct lua_State *L)
{
	luaL_unref(L, LUA_REGISTRYINDEX, ffi_ref);
	lua_close(L); /* collects garbage, invoking userdata gc */
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
	struct tbuf *buf = tbuf_alloc(fiber->gc_pool);
	tbuf_printf(buf, "%s%s", "return ", str);
	int r = luaL_loadstring(L, tbuf_str(buf));
	if (r) {
		lua_pop(L, 1); /* pop the error message */
		r = luaL_loadstring(L, str);
		if (r)
			return r;
	}
	@try {
		lua_call(L, 0, LUA_MULTRET);
	} @catch (ClientError *e) {
		lua_pushstring(L, e->errmsg);
		return 1;
	}
	return 0;
}

static int
tarantool_lua_dofile(struct lua_State *L, const char *filename)
{
	lua_getglobal(L, "dofile");
	lua_pushstring(L, filename);
	return lua_pcall(L, 1, 1, 0);
}

void
tarantool_lua(struct lua_State *L,
	      struct tbuf *out, const char *str)
{
	tarantool_lua_set_out(L, out);
	int r = tarantool_lua_dostring(L, str);
	tarantool_lua_set_out(L, NULL);
	if (r) {
		/* Make sure the output is YAMLish */
		tbuf_printf(out, "error: '%s'\r\n",
			    luaL_gsub(L, lua_tostring(L, -1),
				      "'", "''"));
	}
	else {
		tarantool_lua_printstack_yaml(L, out);
	}
	lua_settop(L, 0); /* clear the stack from return values. */
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
	(void) r;  /* -Wunused-result warning suppression */
	return *endptr != '\0';
}

/*
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
"box.space = {}\n"
"setmetatable(box.space, getmetatable(box.cfg))\n"
"getmetatable(box.space).__index = function(table, index)\n"
"  table[index] = {}\n"
"  setmetatable(table[index], getmetatable(table))\n"
"  return rawget(table, index)\n"
"end\n");
	while ((key = tarantool_cfg_iterator_next(i, cfg, &value)) != NULL) {
		if (value == NULL)
			continue;
		char *quote = is_string(value) ? "'" : "";
		if (strchr(key, '.') == NULL) {
			lua_pushfstring(L, "box.cfg.%s = %s%s%s\n",
					key, quote, value, quote);
			luaL_addvalue(&b);
		} else if (strncmp(key, "space", strlen("space")) == 0) {
			lua_pushfstring(L, "box.%s = %s%s%s\n",
					key, quote, value, quote);
			luaL_addvalue(&b);
		}
		free(value);
	}
	luaL_addstring(&b,
"getmetatable(box.cfg).__newindex = function(table, index)\n"
"  error('Attempt to modify a read-only table')\n"
"end\n"
"getmetatable(box.cfg).__index = nil\n"
"if type(box.on_reload_configuration) == 'function' then\n"
"  box.on_reload_configuration()\n"
"end\n");
	luaL_pushresult(&b);
	if (luaL_loadstring(L, lua_tostring(L, -1)) != 0 ||
	    lua_pcall(L, 0, 0, 0) != 0) {
		panic("%s", lua_tostring(L, -1));
	}
	lua_pop(L, 1);
}

/**
 * Load start-up file routine
 */
static void
load_init_script(void *L_ptr)
{
	struct lua_State *L = (struct lua_State *) L_ptr;
	struct stat st;
	/* checking that Lua start-up file exist. */
	if (stat(TARANTOOL_LUA_INIT_SCRIPT, &st)) {
		/*
		 * File doesn't exist. It's OK, tarantool may not have
		 * start-up file.
		 */
		return;
	}

	/* execute start-up file */
	if (tarantool_lua_dofile(L, TARANTOOL_LUA_INIT_SCRIPT))
		panic("%s", lua_tostring(L, -1));
}

void tarantool_lua_load_init_script(struct lua_State *L)
{
	/*
	 * init script can call box.fiber.yield (including implicitly via
	 * box.insert, box.update, etc...) but yield which called in sched
	 * fiber will crash the server. That why, to avoid the problem, we must
	 * run init script in to separate fiber.
	 */
	struct fiber *loader = fiber_create(TARANTOOL_LUA_INIT_SCRIPT, -1,
					    load_init_script, L);
	fiber_call(loader);
}

/*
 * vim: foldmethod=marker
 */
