/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "lua/backtrace.h"

#ifdef ENABLE_BACKTRACE
#include "core/fiber.h"
#include "core/tt_static.h"

#include "lua.h"

/*
 * This is the declaration of the Lua stack entry function: it does not exactly
 * `lj_BC_FUNCC`'s signature and its only aim is to provide the `lj_BC_FUNCC`
 * symbol's address for setting up Lua stack boundaries.
 */
void
lj_BC_FUNCC(void);

static void *lua_stack_entry_start_ip = (void *)lj_BC_FUNCC;
/* See lj_vm.s */
static void *lua_stack_entry_end_ip = (char *)lj_BC_FUNCC + 72;

static const char *const lua_frame_str_fmt = "#%-2d %s in %s:%d";

void
backtrace_lua_init(void)
{
#ifndef __APPLE__
	unw_proc_info_t proc_info;
	int rc = unw_get_proc_info_by_ip(unw_local_addr_space,
					 (unw_word_t)lua_stack_entry_start_ip,
					 &proc_info, NULL);
	if (rc != 0) {
		say_debug("unwinding error: unw_get_proc_info_by_ip failed: "
			  "%s", unw_strerror(rc));
		return;
	}
	lua_stack_entry_start_ip = (void *)proc_info.start_ip;
	lua_stack_entry_end_ip = (void *)proc_info.end_ip;
#endif /* __APPLE__ */
}

/*
 * Append frame to backtrace.
 */
static void
append_c_frame(struct backtrace_lua *bt, struct backtrace_frame *c_frame)
{
	if (bt->frame_count < BACKTRACE_LUA_FRAME_COUNT_MAX) {
		struct backtrace_lua_frame *lua_frame =
			&bt->frames[bt->frame_count++];
		lua_frame->type = BACKTRACE_LUA_FRAME_C;
		lua_frame->c = *c_frame;
	}
}

/*
 * Append Lua frame to backtrace.
 */
static void
append_lua_frame(struct backtrace_lua *bt, const char *proc_name,
		 const char *src_name, int line_no)
{
	if (bt->frame_count < BACKTRACE_LUA_FRAME_COUNT_MAX) {
		struct backtrace_lua_frame *frame =
			&bt->frames[bt->frame_count++];
		frame->type = BACKTRACE_LUA_FRAME_LUA;
		strlcpy(frame->lua.proc_name, proc_name,
			BACKTRACE_LUA_PROC_NAME_LEN_MAX);
		strlcpy(frame->lua.src_name, src_name,
			BACKTRACE_LUA_PROC_NAME_LEN_MAX);
		frame->lua.line_no = line_no;
	}
}

/*
 * Collect Lua frames from `fiber`'s Lua stack, if it is present.
 *
 * `lua_stack_depth` must be maintained for multiple calls to this function
 * throughout the process of backtrace collection, so that Lua stack unwinding
 * can be continued from the placed it stopped previously: there can be
 * multiple entry points to the Lua stack interleaved with C/C++ frames.
 */
static void
collect_lua_frames(struct backtrace_lua *bt, struct fiber *fiber,
		   int *lua_stack_depth)
{
	struct lua_State *L = fiber->storage.lua.stack;
	if (L == NULL)
		return;
	lua_Debug ar;
	while (lua_getstack(L, *lua_stack_depth, &ar) > 0) {
		/* Skip all following C-frames. */
		lua_getinfo(L, "Sln", &ar);
		if (*ar.what != 'C') {
			break;
		}
		if (ar.name != NULL) {
			/* Dump frame if it is a C built-in call. */
			const char *proc_name =
				ar.name != NULL ? ar.name : "(unnamed)";
			append_lua_frame(bt, proc_name, ar.source,
					 ar.currentline);
		}
		++*lua_stack_depth;
	}
	while (lua_getstack(L, *lua_stack_depth, &ar) > 0) {
		/* Backtrace Lua frame. */
		lua_getinfo(L, "Sln", &ar);
		if (*ar.what == 'C') {
			break;
		}
		const char *proc_name = ar.name != NULL ? ar.name : "(unnamed)";
		append_lua_frame(bt, proc_name, ar.source, ar.currentline);
		++*lua_stack_depth;
	}
}

void
backtrace_lua_collect(struct backtrace_lua *bt_lua, struct fiber *fiber,
		      int skip_frames)
{
	assert(skip_frames >= 0);
	/* The user should not see the frame of `backtrace_collect`. */
	++skip_frames;

	struct backtrace core_bt;
	backtrace_collect(&core_bt, fiber, skip_frames);
	bt_lua->frame_count = 0;
	int lua_stack_depth = 0;
	for (struct backtrace_frame *frame = core_bt.frames;
	     frame != &core_bt.frames[core_bt.frame_count]; ++frame) {
		if (lua_stack_entry_start_ip < frame->ip &&
		    frame->ip < lua_stack_entry_end_ip) {
			collect_lua_frames(bt_lua, fiber, &lua_stack_depth);
		} else {
			append_c_frame(bt_lua, frame);
		}
	}
}

void
backtrace_lua_stack_push(const struct backtrace_lua *bt, struct lua_State *L)
{
	int frame_no = 1;
	for (const struct backtrace_lua_frame *frame = bt->frames;
	     frame != &bt->frames[bt->frame_count];
	     ++frame, ++frame_no) {
		const char *frame_str;
		switch (frame->type) {
		case BACKTRACE_LUA_FRAME_C: {
			unw_word_t offset = 0;
			const char *proc_name =
				backtrace_frame_resolve(&frame->c, &offset);
			proc_name = proc_name != NULL ? proc_name : "??";
			frame_str = tt_sprintf(C_FRAME_STR_FMT,
					       frame_no, frame->c.ip,
					       proc_name, offset);
			lua_pushnumber(L, frame_no);
			lua_newtable(L);
			lua_pushstring(L, "C");
			break;
		}
		case BACKTRACE_LUA_FRAME_LUA: {
			frame_str = tt_sprintf(lua_frame_str_fmt, frame_no,
					       frame->lua.proc_name,
					       frame->lua.src_name,
					       frame->lua.line_no);
			lua_pushnumber(L, frame_no);
			lua_newtable(L);
			lua_pushstring(L, "L");
			break;
		}
		default:
			unreachable();
		}
		lua_pushstring(L, frame_str);
		lua_settable(L, -3);
		lua_settable(L, -3);
	}
}
#endif /* ENABLE_BACKTRACE */
