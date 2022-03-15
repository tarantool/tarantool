/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "backtrace.h"
#include "trivia/util.h"

#include <stdlib.h>
#include <stdio.h>

#include <cxxabi.h>

#include "say.h"
#include "fiber.h"

#include "assoc.h"

#define CRLF "\n"

#ifdef ENABLE_BACKTRACE
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#undef UNW_LOCAL_ONLY

#include "lua/fiber.h"

#ifdef __APPLE__
#include <dlfcn.h>
#endif

/*
 * We use a static buffer interface because it is too late to do any
 * allocation when we are printing backtrace and fiber stack is
 * small.
 */
#include "small/region.h"
#include "small/static.h"

#include "tt_static.h"

#define BACKTRACE_NAME_MAX 200

static __thread struct region cache_region;
static __thread struct mh_i64ptr_t *proc_cache = NULL;

/* Lua stack beginning, used for detecting Lua frames. */
static void *backtrace_lua_stk_start_ip = (void *)lj_BC_FUNCC;
/* Lua stack ending, ditto. */
static void *backtrace_lua_stk_end_ip;

struct proc_cache_entry {
	char name[BACKTRACE_NAME_MAX];
	unw_word_t offset;
};

void
backtrace_proc_cache_clear(void)
{
	if (proc_cache == NULL)
		return;
	region_destroy(&cache_region);
	mh_i64ptr_delete(proc_cache);
	proc_cache = NULL;
}

/*
 * Find procedure name and offset identified by ip in the procedure cache.
 */
static const char *
backtrace_proc_cache_find(unw_word_t ip, unw_word_t *offset)
{
	if (proc_cache != NULL) {
		mh_int_t k = mh_i64ptr_find(proc_cache, ip, NULL);
		if (k != mh_end(proc_cache)) {
			struct proc_cache_entry *entry =
				(struct proc_cache_entry *)
					mh_i64ptr_node(proc_cache, k)->val;
			*offset = entry->offset;
			return entry->name;
		}
	}
	return NULL;
}

/*
 * Put procedure name and offset identified by ip into the procedure cache.
 */
static void
backtrace_proc_cache_put(unw_word_t ip, const char *name, unw_word_t offset)
{
	if (proc_cache == NULL) {
		region_create(&cache_region, &cord()->slabc);
		proc_cache = mh_i64ptr_new();
	}

	size_t size;
	struct proc_cache_entry *entry =
		region_alloc_object(&cache_region, typeof(*entry), &size);
	if (unlikely(entry == NULL))
		return;

	struct mh_i64ptr_node_t node;
	node.key = ip;
	node.val = entry;
	entry->offset = offset;
	strlcpy(entry->name, name, BACKTRACE_NAME_MAX);
	mh_i64ptr_put(proc_cache, &node, NULL, NULL);
}

static const char *
get_proc_name(unw_cursor_t *unw_cur, unw_word_t *offset, bool skip_cache)
{
	static __thread char proc_name[BACKTRACE_NAME_MAX];

	if (skip_cache) {
		unw_get_proc_name(unw_cur, proc_name, sizeof(proc_name),
				  offset);
		return proc_name;
	}

	unw_word_t ip;
	unw_get_reg(unw_cur, UNW_REG_IP, &ip);
	const char *cached_name = backtrace_proc_cache_find(ip, offset);
	if (cached_name != NULL)
		return cached_name;

	unw_get_proc_name(unw_cur, proc_name, sizeof(proc_name), offset);
	backtrace_proc_cache_put(ip, proc_name, *offset);
	return proc_name;
}

char *
backtrace(char *start, size_t size)
{
	int frame_no = 0;
	unw_word_t sp = 0, old_sp = 0, ip, offset;
	unw_context_t unw_context;
	unw_getcontext(&unw_context);
	unw_cursor_t unw_cur;
	unw_init_local(&unw_cur, &unw_context);
	int unw_status;
	char *p = start;
	char *end = start + size - 1;
	*p = '\0';
	while ((unw_status = unw_step(&unw_cur)) > 0) {
		const char *proc;
		old_sp = sp;
		unw_get_reg(&unw_cur, UNW_REG_IP, &ip);
		unw_get_reg(&unw_cur, UNW_REG_SP, &sp);
		if (sp == old_sp) {
			say_debug("unwinding error: previous frame "
				  "identical to this frame (corrupt stack?)");
			goto out;
		}
		proc = get_proc_name(&unw_cur, &offset, true);
		p += snprintf(p, end - p, "#%-2d %p in ", frame_no, (void *)ip);
		if (p >= end)
			goto out;
		p += snprintf(p, end - p, "%s+%lx", proc, (long)offset);
		if (p >= end)
			goto out;
		p += snprintf(p, end - p, CRLF);
		if (p >= end)
			goto out;
		++frame_no;
	}
#ifndef TARGET_OS_DARWIN
	if (unw_status != 0)
		say_debug("unwinding error: %s", unw_strerror(unw_status));
#else
	if (unw_status != 0)
		say_debug("unwinding error: %i", unw_status);
#endif
out:
	return start;
}

void
backtrace_init(void)
{
#ifndef __APPLE__
	unw_proc_info_t proc_info;
	if (backtrace_lua_stk_start_ip != NULL) {
		int rc = unw_get_proc_info_by_ip(unw_local_addr_space,
					    (unw_word_t)lj_BC_FUNCC,
					    &proc_info, NULL);
		if (rc != 0) {
			say_error("unwinding error: unw_get_proc_info_by_ip "
				  "failed with: %s", unw_strerror(rc));
			return;
		}
		backtrace_lua_stk_start_ip = (void *)proc_info.start_ip;
		backtrace_lua_stk_end_ip = (void *)proc_info.end_ip;
	}
#else /* __APPLE__ */
	backtrace_lua_stk_start_ip = (void *)lj_BC_FUNCC;
	backtrace_lua_stk_end_ip = NULL;
#endif /* __APPLE__ */
}

/* Resolve function name and offset based on `ip`. */
int
backtrace_resolve_from_ip(unw_word_t ip, const char **proc_name,
			  unw_word_t *offs)
{
	*proc_name = backtrace_proc_cache_find(ip, offs);
	if (*proc_name != NULL)
		return 0;
#ifndef __APPLE__
	unw_accessors_t *acc = unw_get_accessors(unw_local_addr_space);
	assert(acc->get_proc_name != NULL);
	char *proc_name_buf = tt_static_buf();
	int rc = acc->get_proc_name(unw_local_addr_space, ip, proc_name_buf,
				    TT_STATIC_BUF_LEN, offs, NULL);
	if (rc != 0) {
		say_error("unwinding error: get_proc_name accessor failed "
			  "with: %s", unw_strerror(rc));
		return -1;
	}
	*proc_name = proc_name_buf;
#else /* __APPLE__ */
	Dl_info dli;
	if (dladdr((void *)ip, &dli) == 0) {
		say_error("unwinding error: dladdr failed");
		return -1;
	}

	*offs = ip - (unw_word_t)dli.dli_saddr;
	*proc_name = dli.dli_sname;
#endif /* __APPLE__ */
	backtrace_proc_cache_put(ip, *proc_name, *offs);
	return 0;
}

static void
backtrace_collect_lua_frames_cb(struct backtrace *bt, struct fiber *f)
{
	struct lua_State *L = f->storage.lua.stack;
	if (L != NULL)
		fiber_backtrace_collect_lua_frames_foreach(L, bt);
	else
		backtrace_append_lua_frame(bt, "<lua stack>", "<lua stack>",
					   -1);
}

#ifndef __APPLE__
/*
 * Append 'rip' of a C/C++ frame and its corresponding Lua frames (if any) to
 * 'bt'.
 */
static void
backtrace_append_ip(struct backtrace *bt, struct fiber *fiber, void *ip)
{
	if (ip > backtrace_lua_stk_start_ip &&
	    ip < backtrace_lua_stk_end_ip)
		backtrace_collect_lua_frames_cb(bt, fiber);
	backtrace_append_c_frame(bt, ip);
}
#else /* __APPLE__ */
/*
 * Ditto.
 */
static int
backtrace_append_ip(struct backtrace *bt, struct fiber *fiber,
		    unw_cursor_t *unw_cur)
{
	unw_word_t ip_word;
	int rc = unw_get_reg(unw_cur, UNW_REG_IP, &ip_word);
	if (rc != 0) {
		say_error("unwinding error: unw_get_reg failed with: %d", rc);
		return -1;
	}
	void *ip = (void *)ip_word;

	/* Try to obtain a value for `backtrace_lua_stk_end_ip` */
	if (backtrace_lua_stk_end_ip == NULL) {
		unw_proc_info_t proc_info;
		rc = unw_get_proc_info(unw_cur, &proc_info);
		if (rc != 0) {
			say_error("unwinding error: unw_get_proc_info failed "
				  "with: %d", rc);
			return -1;
		}
		if ((void *)proc_info.start_ip <= backtrace_lua_stk_start_ip &&
		    backtrace_lua_stk_start_ip <= (void *)proc_info.end_ip) {
			backtrace_lua_stk_start_ip = (void *)proc_info.start_ip;
			backtrace_lua_stk_end_ip = (void *)proc_info.end_ip;
		}
	}
	if (ip > backtrace_lua_stk_start_ip &&
	    ip < backtrace_lua_stk_end_ip)
		backtrace_collect_lua_frames_cb(bt, fiber);
	backtrace_append_c_frame(bt, ip);
	return 0;
}
#endif /* __APPLE__ */

/*
 * Performs backtrace collection on current stack.
 *
 * Returns the 'stk' parameter to simplify context switch.
 */
#ifdef __x86_64__
__attribute__ ((__force_align_arg_pointer__))
#endif /* __x86_64__ */
static NOINLINE void *
backtrace_collect_curr_stk(struct backtrace *bt, struct fiber *fiber, void *stk)
{
	bt->frames_cnt = 0;
#ifndef __APPLE__
	void *rips[BACKTRACE_FRAMES_CNT_MAX + 2];
	int frames_count = unw_backtrace(rips, BACKTRACE_FRAMES_CNT_MAX + 2);
	for (int frame_no = 2; frame_no < frames_count; ++frame_no) {
		backtrace_append_ip(bt, fiber, rips[frame_no]);
	}
#else /* __APPLE__ */
	unw_context_t unw_ctx;
	int rc = unw_getcontext(&unw_ctx);
	if (rc != 0) {
		say_error("unwinding error: unw_getcontext failed");
		return stk;
	}
	unw_cursor_t unw_cur;
	rc = unw_init_local(&unw_cur, &unw_ctx);
	if (rc != 0) {
		say_error("unwinding error: unw_init_local failed");
		return stk;
	}
	for (int frame_no = 0; frame_no < BACKTRACE_FRAMES_CNT_MAX + 2;
	     ++frame_no) {
		if (frame_no >= 2) {
			if (backtrace_append_ip(bt, fiber, &unw_cur) != 0)
				break;
		}
		int rc = unw_step(&unw_cur);
		if (rc < 0) {
#ifndef __APPLE__
			say_error("unwinding error: unw_step_failed with: %s",
				  unw_strerror(rc));
#else /* __APPLE__ */
			say_error("unwinding error: unw_step failed with: %d",
				  rc);
#endif /* __APPLE__ */
		}
		if (rc == 0) {
			++frame_no;
			break;
		}
	}
#endif /* __APPLE__ */
	return stk;
}

/*
 * Restore target fiber context (if needed) and call
 * `backtrace_collect_curr_stk` over it.
 */
NOINLINE void
backtrace_collect(struct backtrace *bt, struct fiber *fiber)
{
	if (fiber == fiber()) {
		backtrace_collect_curr_stk(bt, fiber, NULL);
		return;
	} else if ((fiber->flags & FIBER_IS_CANCELLABLE) == 0) {
		/*
		 * Fiber stacks can't be traced for non-cancellable fibers
		 * due to the limited capabilities of libcoro in CORO_ASM mode.
		 */
		bt->frames_cnt = 0;
		return;
	}
	/*
	 * 1. Save current fiber context on stack.
	 * 2. Restore target fiber context.
	 * 3. Setup stack frame and call `backtrace_collect_curr_stk`.
	 * 4. Restore original stack pointer from `backtrace_collect_curr_stk`
	 *    return value and restore original fiber context.
	 */
#if __amd64__
__asm__ volatile(
	/* Preserve current context */
	"\tpushq %%rbp\n"
	"\tpushq %%rbx\n"
	"\tpushq %%r12\n"
	"\tpushq %%r13\n"
	"\tpushq %%r14\n"
	"\tpushq %%r15\n"
	/* Set first arg */
	"\tmovq %0, %%rdi\n"
	/* Set second arg */
	"\tmovq %1, %%rsi\n"
	/* Setup third arg as old sp */
	"\tmovq %%rsp, %%rdx\n"
	/* Restore target context, but not increment sp to preserve it */
	"\tmovq 0(%2), %%rsp\n"
	"\tmovq 0(%%rsp), %%r15\n"
	"\tmovq 8(%%rsp), %%r14\n"
	"\tmovq 16(%%rsp), %%r13\n"
	"\tmovq 24(%%rsp), %%r12\n"
	"\tmovq 32(%%rsp), %%rbx\n"
	"\tmovq 40(%%rsp), %%rbp\n"
	".cfi_remember_state\n"
	".cfi_def_cfa %%rsp, 8 * 7\n"
	"\tleaq %P3(%%rip), %%rax\n"
	"\tcall *%%rax\n"
	".cfi_restore_state\n"
	/* Restore old sp and context */
	"\tmov %%rax, %%rsp\n"
	"\tpopq %%r15\n"
	"\tpopq %%r14\n"
	"\tpopq %%r13\n"
	"\tpopq %%r12\n"
	"\tpopq %%rbx\n"
	"\tpopq %%rbp\n"
	:
	: "r" (bt), "r" (fiber), "r" (&fiber->ctx),
	  "i" (backtrace_collect_curr_stk)
	: "rdi", "rsi", "rdx", "rax", "memory");
#elif __aarch64__
__asm__ volatile(
	/* Setup first arg */
	"\tmov x0, %0\n"
	/* Setup second arg */
	"\tmov x1, %1\n"
	/* Save current context */
	"\tsub x2, sp, #8 * 20\n"
	"\tstp x19, x20, [x2, #16 * 0]\n"
	"\tstp x21, x22, [x2, #16 * 1]\n"
	"\tstp x23, x24, [x2, #16 * 2]\n"
	"\tstp x25, x26, [x2, #16 * 3]\n"
	"\tstp x27, x28, [x2, #16 * 4]\n"
	"\tstp x29, x30, [x2, #16 * 5]\n"
	"\tstp d8,  d9,  [x2, #16 * 6]\n"
	"\tstp d10, d11, [x2, #16 * 7]\n"
	"\tstp d12, d13, [x2, #16 * 8]\n"
	"\tstp d14, d15, [x2, #16 * 9]\n"
	/* Restore target context */
	"\tldr x3, [%2]\n"
	"\tldp x19, x20, [x3, #16 * 0]\n"
	"\tldp x21, x22, [x3, #16 * 1]\n"
	"\tldp x23, x24, [x3, #16 * 2]\n"
	"\tldp x25, x26, [x3, #16 * 3]\n"
	"\tldp x27, x28, [x3, #16 * 4]\n"
	"\tldp x29, x30, [x3, #16 * 5]\n"
	"\tldp d8,  d9,  [x3, #16 * 6]\n"
	"\tldp d10, d11, [x3, #16 * 7]\n"
	"\tldp d12, d13, [x3, #16 * 8]\n"
	"\tldp d14, d15, [x3, #16 * 9]\n"
	"\tmov sp, x3\n"
	".cfi_remember_state\n"
	".cfi_def_cfa sp, 16 * 10\n"
	".cfi_offset x29, -16 * 5\n"
	".cfi_offset x30, -16 * 5 + 8\n"
	"\tbl %3\n"
	".cfi_restore_state\n"
	/* Restore context (old sp in x0) */
	"\tldp x19, x20, [x0, #16 * 0]\n"
	"\tldp x21, x22, [x0, #16 * 1]\n"
	"\tldp x23, x24, [x0, #16 * 2]\n"
	"\tldp x25, x26, [x0, #16 * 3]\n"
	"\tldp x27, x28, [x0, #16 * 4]\n"
	"\tldp x29, x30, [x0, #16 * 5]\n"
	"\tldp d8,  d9,  [x0, #16 * 6]\n"
	"\tldp d10, d11, [x0, #16 * 7]\n"
	"\tldp d12, d13, [x0, #16 * 8]\n"
	"\tldp d14, d15, [x0, #16 * 9]\n"
	"\tadd sp, x0, #8 * 20\n"
	:
	: "r" (bt), "r" (fiber), "r" (&fiber->ctx),
	  "S" (backtrace_collect_curr_stk)
	: "x0", "x1", "x2", "x3", "x30", "memory");
#endif
}

void
backtrace_foreach(struct backtrace *bt)
{
	char *demangle_buf = NULL;
	size_t demangle_buf_len = 0;

	int frame_no = 0;
	const struct backtrace_frame *frame = bt->frames;
	const struct backtrace_frame *end_frame = bt->frames + bt->frames_cnt;
	for (; frame != end_frame; ++frame) {
		switch (frame->type) {
		case BACKTRACE_FRAME_TYPE_LUA:
			fiber_backtrace_foreach_lua_frame_cb(frame->proc_name,
							     frame->src_name,
							     frame->line, bt);
			break;
		case BACKTRACE_FRAME_TYPE_C: {
			unw_word_t offs;
			const char *proc_name = NULL;
			if (backtrace_resolve_from_ip((unw_word_t)frame->ip,
						      &proc_name, &offs) != 0) {
				goto out;
			}
			if (proc_name != NULL) {
				int status;
				char *demangled_name =
					abi::__cxa_demangle(proc_name,
							    demangle_buf,
							    &demangle_buf_len,
							    &status);
				if (status != 0 && status != -2) {
					say_error("unwinding error: "
						  "__cxa_demangle failed with "
						  "status: %d", status);
					goto out;
				}
				if (demangled_name != NULL) {
					demangle_buf = demangled_name;
					proc_name = demangled_name;
				}
			}
			fiber_backtrace_foreach_c_frame_cb(frame_no++,
							   frame->ip, proc_name,
							   offs, bt);
			break;
		}
		default:
			unreachable();
		}
	}
out:
	free(demangle_buf);
}

void
backtrace_append_c_frame(struct backtrace *bt, void *rip)
{
	if (bt->frames_cnt < BACKTRACE_FRAMES_CNT_MAX) {
		struct backtrace_frame *frame = bt->frames + bt->frames_cnt;
		frame->type = BACKTRACE_FRAME_TYPE_C;
		frame->ip = rip;
		++bt->frames_cnt;
	}
}

void
backtrace_append_lua_frame(struct backtrace *bt, const char *proc_name,
			   const char *src_name, int line_no)
{
	if (bt->frames_cnt < BACKTRACE_FRAMES_CNT_MAX) {
		struct backtrace_frame *frame = bt->frames + bt->frames_cnt;
		frame->type = BACKTRACE_FRAME_TYPE_LUA;
		frame->line = line_no;
		strncpy(frame->proc_name, proc_name,
			BACKTRACE_LUA_LEN_MAX - 1);
		frame->proc_name[BACKTRACE_LUA_LEN_MAX - 1] = '\0';
		strncpy(frame->src_name, src_name,
			BACKTRACE_LUA_LEN_MAX - 1);
		frame->src_name[BACKTRACE_LUA_LEN_MAX - 1] = '\0';
		++bt->frames_cnt;
	}
}

void
print_backtrace(void)
{
	char *start = (char *)static_alloc(SMALL_STATIC_SIZE);
	fdprintf(STDERR_FILENO, "%s", backtrace(start, SMALL_STATIC_SIZE));
}
#endif /* ENABLE_BACKTRACE */


NORETURN void
assert_fail(const char *assertion, const char *file, unsigned int line, const char *function)
{
	fprintf(stderr, "%s:%i: %s: assertion %s failed.\n", file, line, function, assertion);
#ifdef ENABLE_BACKTRACE
	print_backtrace();
#endif /* ENABLE_BACKTRACE */
	close_all_xcpt(0);
	abort();
}
