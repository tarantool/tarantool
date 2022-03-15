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

#ifndef __APPLE__
void
lua_backtrace_init(void)
{
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
}
#endif /* __APPLE__ */

/*
 * Append Lua frame to backtrace.
 */
static void
append_lua_frame(struct lua_backtrace *bt, int frame_no,
		 const char *proc_name, const char *src_name, int line_no)
{
	assert(bt != NULL);
	assert(proc_name != NULL);
	assert(src_name != NULL);

	if (bt->frame_count < LUA_BACKTRACE_FRAME_COUNT_MAX) {
		struct lua_frame *frame = &bt->frames[bt->frame_count++];
		frame->no = frame_no;
		strlcpy(frame->proc_name, proc_name,
			LUA_BACKTRACE_LUA_PROC_NAME_LEN_MAX);
		strlcpy(frame->src_name, src_name,
			LUA_BACKTRACE_LUA_PROC_NAME_LEN_MAX);
		frame->line_no = line_no;
	}
}

/*
 * Collect Lua frames from fiber's Lua stack.
 */
static void
collect_lua_frames(struct lua_backtrace *bt, struct fiber *fiber)
{
	assert(bt != NULL);
	assert(fiber != NULL);
	struct lua_State *L = fiber->storage.lua.stack;
	if (L == NULL) {
		append_lua_frame(bt, bt->first_lua_frame_no, "<Lua stack>",
				 "<Lua stack>", -1);
		return;
	}
	int lua_frame_count = 0;
	lua_Debug ar;
	while (lua_getstack(L, lua_frame_count, &ar) > 0) {
		/* Skip all following C-frames. */
		lua_getinfo(L, "Sln", &ar);
		if (*ar.what != 'C')
			break;
		if (ar.name != NULL) {
			/* Dump frame if it is a C built-in call. */
			const char *proc_name =
				ar.name != NULL ? ar.name : "(unnamed)";
			append_lua_frame(bt,
					 bt->first_lua_frame_no +
					 lua_frame_count,
					 proc_name, ar.source, ar.currentline);
		}
		++lua_frame_count;
	}
	while (lua_getstack(L, lua_frame_count, &ar) > 0) {
		/* Backtrace Lua frame. */
		lua_getinfo(L, "Sln", &ar);
		if (*ar.what == 'C')
			break;
		const char *proc_name = ar.name != NULL ? ar.name : "(unnamed)";
		append_lua_frame(bt, bt->first_lua_frame_no + lua_frame_count,
				 proc_name, ar.source, ar.currentline);
		++lua_frame_count;
	}
}

void
lua_backtrace_collect_frames(struct lua_backtrace *lua_bt, struct fiber *fiber)
{
	assert(lua_bt != NULL);
	assert(fiber != NULL);

	core_backtrace_collect_frames(&lua_bt->core_bt, fiber, 3);
	lua_bt->first_lua_frame_no = 1;
	lua_bt->frame_count = 0;
	for (struct core_frame *frame = lua_bt->core_bt.frames;
	     frame != &lua_bt->core_bt.frames[lua_bt->core_bt.frame_count];
	     ++frame) {
		if (lua_stack_entry_start_ip < frame->ip &&
		    frame->ip < lua_stack_entry_end_ip) {
			lua_bt->first_lua_frame_no =
				(int)(frame - lua_bt->core_bt.frames) + 1;
			collect_lua_frames(lua_bt, fiber);
			break;
		}
	}
}

void
lua_backtrace_foreach(const struct lua_backtrace *lua_bt,
		      core_resolved_frame_cb core_resolved_frame_cb,
		      lua_frame_cb lua_frame_cb,
		      void *ctx)
{
	assert(lua_bt != NULL);

	const struct core_backtrace *core_bt =
		(const struct core_backtrace *)lua_bt;
	core_backtrace_foreach(core_bt, 0,
			       lua_bt->first_lua_frame_no - 1,
			       core_resolved_frame_cb, ctx);
	for (const struct lua_frame *frame = lua_bt->frames;
	     frame != &lua_bt->frames[lua_bt->frame_count]; ++frame) {
		int rc = lua_frame_cb(frame, ctx);
		if (rc != 0)
			break;
	}
	core_backtrace_foreach(core_bt, lua_bt->first_lua_frame_no - 1,
			       core_bt->frame_count, core_resolved_frame_cb,
			       ctx);
}

/*
 * Context for frame output callbacks below.
 */
struct output_ctx {
	/*
	 * Whether Lua frames were processed: for fixing up frame numbers of
	 * Core frames following Lua frames.
	 */
	bool processed_lua_frames;
	/* Adjustment to frame numbers of Core frames following Lua frames. */
	int lua_frame_count;
};

/*
 * Context for `push_*_frame` callbacks below.
 */
struct push_ctx {
	/* Context for frame output. */
	struct output_ctx output_ctx;
	/*
	 * Stack on which frame information is pushed.
	 */
	struct lua_State *L;
};

/*
 * Push resolved Core frame information onto Lua stack.
 */
static int
push_core_resolved_frame(const struct core_resolved_frame *frame, void *ctx)
{
	assert(frame != NULL);
	assert(ctx != NULL);

	struct output_ctx *output_ctx = (struct output_ctx *)ctx;
	struct push_ctx *push_ctx = (struct push_ctx *)ctx;
	struct lua_State *L = push_ctx->L;
	assert(L != NULL);
	const char *proc_name =
		frame->proc_name != NULL ? frame->proc_name : "??";
	int effective_frame_no = frame->no + (output_ctx->processed_lua_frames ?
					      output_ctx->lua_frame_count : 0);
	const char *frame_str = tt_sprintf(core_frame_str_fmt,
					   effective_frame_no,
					   frame->core_frame.ip, proc_name,
					   frame->offset);
	lua_pushnumber(L, effective_frame_no);
	lua_newtable(L);
	lua_pushstring(L, "C");
	lua_pushstring(L, frame_str);
	lua_settable(L, -3);
	lua_settable(L, -3);
	return 0;
}

/*
 * Push Lua frame information onto Lua stack.
 */
static int
push_lua_frame(const struct lua_frame *frame, void *ctx)
{
	assert(frame != NULL);
	assert(ctx != NULL);

	struct push_ctx *push_ctx = (struct push_ctx *)ctx;
	struct lua_State *L = push_ctx->L;
	assert(L != NULL);
	push_ctx->output_ctx.processed_lua_frames = true;
	const char *frame_str = tt_sprintf(lua_frame_str_fmt, frame->no,
					   frame->proc_name, frame->src_name,
					   frame->line_no);
	lua_pushnumber(L, frame->no);
	lua_newtable(L);
	lua_pushstring(L, "L");
	lua_pushstring(L, frame_str);
	lua_settable(L, -3);
	lua_settable(L, -3);
	return 0;
}

void
lua_backtrace_push_frames(const struct lua_backtrace *bt, struct lua_State *L)
{
	assert(bt != NULL);
	assert(L != NULL);

	struct push_ctx push_ctx = {
		.output_ctx = {
			.processed_lua_frames = false,
			.lua_frame_count = bt->frame_count,
		},
		.L = L,
	};

	lua_backtrace_foreach(bt, push_core_resolved_frame, push_lua_frame,
			      &push_ctx);
}

/*
 * Context for `print_*_frame` callbacks below.
 */
struct print_ctx {
	/* Context for frame output. */
	struct output_ctx output_ctx;
	/* Log level of `say` API. */
	enum say_level say_level;
};

/*
 * Print resolved Core frame information via `say` API.
 */
static int
print_core_resolved_frame(const struct core_resolved_frame *frame, void *ctx)
{
	assert(frame != NULL);
	assert(ctx != NULL);

	struct output_ctx *output_ctx = (struct output_ctx *)ctx;
	struct print_ctx *print_ctx = (struct print_ctx *)ctx;
	int effective_frame_no = frame->no + (output_ctx->processed_lua_frames ?
					      output_ctx->lua_frame_count : 0);
	say(print_ctx->say_level, NULL, core_frame_str_fmt, effective_frame_no,
	    frame->core_frame.ip, frame->proc_name, frame->offset);
	return 0;
}

/*
 * Print Lua frame information via `say` API.
 */
static int
print_lua_frame(const struct lua_frame *frame, void *ctx)
{
	assert(frame != NULL);
	assert(ctx != NULL);

	struct print_ctx *print_ctx = (struct print_ctx *)ctx;
	print_ctx->output_ctx.processed_lua_frames = true;
	say(print_ctx->say_level, NULL, lua_frame_str_fmt, frame->no,
	    frame->proc_name, frame->src_name, frame->line_no);
	return 0;
}

void
lua_backtrace_print_frames(const struct lua_backtrace *bt,
			   enum say_level say_level)
{
	assert(bt != NULL);

	struct print_ctx print_ctx = {
		.output_ctx = {
			.processed_lua_frames = false,
			.lua_frame_count = bt->frame_count,
		},
		.say_level = say_level,
	};

	lua_backtrace_foreach(bt, print_core_resolved_frame, print_lua_frame,
			      &print_ctx);
}
#endif /* ENABLE_BACKTRACE */
