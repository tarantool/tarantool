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
#include <libunwind.h>

#ifdef TARGET_OS_DARWIN
#include <dlfcn.h>
#endif

#include "small/region.h"
#include "small/static.h"
/*
 * We use a static buffer interface because it is too late to do any
 * allocation when we are printing backtrace and fiber stack is
 * small.
 */

#define BACKTRACE_NAME_MAX 200

static __thread struct region cache_region;
static __thread struct mh_i64ptr_t *proc_cache = NULL;

static void *backtrace_lua_entry_start_rip = NULL;
static void *backtrace_lua_entry_end_rip = NULL;
static backtrace_collect_lua_cb backtrace_collect_lua
	= backtrace_collect_lua_default;

#ifndef NDEBUG
#ifndef TARGET_OS_DARWIN
static void *backtrace_collect_start_rip = NULL;
static void *backtrace_collect_end_rip = NULL;
#endif // TARGET_OS_DARWIN
#endif // NDEBUG

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

static int
backtrace_proc_cache_find(unw_word_t ip, const char **name, unw_word_t *offset)
{
	struct proc_cache_entry *entry;
	mh_int_t k;

	if (proc_cache != NULL) {
		k = mh_i64ptr_find(proc_cache, ip, NULL);
		if (k != mh_end(proc_cache)) {
			entry = (struct proc_cache_entry *)
				mh_i64ptr_node(proc_cache, k)->val;
			*offset = entry->offset;
			*name = entry->name;
			return 0;
		}
	}

	return -1;
}

static int
backtrace_proc_cache_put(unw_word_t ip, const char *name, unw_word_t offset)
{
	struct proc_cache_entry *entry;
	struct mh_i64ptr_node_t node;
	mh_int_t k;

	if (proc_cache == NULL) {
		region_create(&cache_region, &cord()->slabc);
		proc_cache = mh_i64ptr_new();
	}

	size_t size;
	entry = region_alloc_object(&cache_region, typeof(*entry), &size);
	if (entry == NULL)
		return -1;

	node.key = ip;
	node.val = entry;
	entry->offset = offset;
	snprintf(entry->name, BACKTRACE_NAME_MAX - 1, "%s", name);
	entry->name[BACKTRACE_NAME_MAX - 1] = 0;

	k = mh_i64ptr_put(proc_cache, &node, NULL, NULL);
	if (k == mh_end(proc_cache)) {
		size_t used = region_used(&cache_region);
		region_truncate(&cache_region, used - size);
		return -1;
	}

	return 0;
}

const char *
get_proc_name(unw_cursor_t *unw_cur, unw_word_t *offset, bool skip_cache)
{
	static __thread char proc_name[BACKTRACE_NAME_MAX];
	const char *cache_name;
	unw_word_t ip;

	if (skip_cache) {
		unw_get_proc_name(unw_cur, proc_name, sizeof(proc_name),
				  offset);
		return proc_name;
	}

	unw_get_reg(unw_cur, UNW_REG_IP, &ip);
	if (backtrace_proc_cache_find(ip, &cache_name, offset) == 0) {
		return cache_name;
	}

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

void NOINLINE
backtrace_foreach_current(backtrace_cxx_cb cxx_cb, backtrace_lua_cb lua_cb,
			  struct fiber *fiber, void *cb_ctx)
{
	struct backtrace bt;
	backtrace_collect(&bt, fiber);
	memmove(&bt.frames, &bt.frames[1],
		sizeof(bt.frames[0]) * (BACKTRACE_MAX_FRAMES - 1));
	--bt.frames_count;
	backtrace_foreach(&bt, cxx_cb, lua_cb, cb_ctx);
}

/**
 * Resolve frame procedure and offest based on `ip`.
 *
 * The implementation uses poorly documented `get_proc_name' callback
 * from the `unw_accessors_t' to get procedure names via `ip_buf' values.
 * Although `get_proc_name' is present on most architectures, it's an optional
 * field, so procedure name is allowed to be absent (NULL) in `cb' call.
 */
int
backtrace_resolve_from_ip(unw_word_t ip, const char **proc, unw_word_t *offset)
{
	if (backtrace_proc_cache_find(ip, proc, offset) == 0)
		return 0;

#ifndef TARGET_OS_DARWIN
	static __thread char proc_name[BACKTRACE_NAME_MAX];

	int ret = 0;
	unw_proc_info_t pi;
	unw_accessors_t *acc = unw_get_accessors(unw_local_addr_space);
	if (acc->get_proc_name == NULL) {
		ret = unw_get_proc_info_by_ip(unw_local_addr_space, ip, &pi,
					      NULL);
		if (ret != 0) {
			say_debug("unwinding error: %s", unw_strerror(ret));
			return -1;
		}

		*offset = ip - (unw_word_t)pi.start_ip;
		*proc = NULL;
	} else {
		ret = acc->get_proc_name(unw_local_addr_space, ip, proc_name,
					 sizeof(proc_name), offset, NULL);
		if (ret != 0) {
			say_debug("unwinding error: %s", unw_strerror(ret));
			return -1;
		}

		*proc = proc_name;
	}
#else
	Dl_info dli;
	if (dladdr((void *)ip, &dli) == 0) {
		say_debug("unwinding error: dladdr()");
		return -1;
	}

	*offset = ip - (unw_word_t)dli.dli_saddr;
	*proc = dli.dli_sname;
#endif /* TARGET_OS_DARWIN */

	backtrace_proc_cache_put(ip, *proc, *offset);
	return 0;
}

int
backtrace_collect_lua_default(struct backtrace *bt, struct fiber *fiber) {
	(void) fiber;
	backtrace_append_lua(bt, "<lua stack>", "<lua stack>", -1);

	return 0;
}

int
backtrace_init(void *lua_stack_entry_point,
	       backtrace_collect_lua_cb collect_lua_cb) {
	if (collect_lua_cb != NULL)
		backtrace_collect_lua = collect_lua_cb;
#ifndef TARGET_OS_DARWIN
	unw_proc_info_t pi;
	if (lua_stack_entry_point != NULL) {
		if (unw_get_proc_info_by_ip(unw_local_addr_space,
					    (unw_word_t) lua_stack_entry_point,
					    &pi, NULL) != 0)
			return -1;

		backtrace_lua_entry_start_rip = (void *)pi.start_ip;
		backtrace_lua_entry_end_rip = (void *)pi.end_ip;
	}

#ifndef NDEBUG
	if (unw_get_proc_info_by_ip(unw_local_addr_space,
				    (unw_word_t) backtrace_collect,
				    &pi, NULL) != 0)
		return -1;

	backtrace_collect_start_rip = (void *)pi.start_ip;;
	backtrace_collect_end_rip = (void *)pi.end_ip;
#endif // NDEBUG
#else // TARGET_OS_DARWIN
	backtrace_lua_entry_start_rip = lua_stack_entry_point;
	backtrace_lua_entry_end_rip = NULL;
#endif // TARGET_OS_DARWIN
	return 0;
}

/**
 * Add rip of a C/C++ frame and its corresponding Lua frames (if any) to @a bt.
 */
#ifndef TARGET_OS_DARWIN
static int
backtrace_add_rip(struct backtrace *bt, struct fiber *fiber, void *rip) {
	int prev_frames_count = bt->frames_count;
	if (rip > backtrace_lua_entry_start_rip &&
	    rip < backtrace_lua_entry_end_rip) {
		backtrace_collect_lua(bt, fiber);
	}
	backtrace_append_cxx(bt, rip);

	return bt->frames_count - prev_frames_count;
}
#else // TARGET_OS_DARWIN
static int
backtrace_add_rip(struct backtrace *bt, struct fiber *fiber,
		  unw_cursor_t *unw_cur) {
	int prev_frames_count = bt->frames_count;
	unw_word_t rip_word;
	unw_get_reg(unw_cur, UNW_REG_IP, &rip_word);
	void *rip = (void *)rip_word;

	/* Try to obtain a `backtrace_lua_entry_end_rip` value. */
	if (backtrace_lua_entry_start_rip != NULL &&
	    backtrace_lua_entry_end_rip == NULL) {
		unw_proc_info_t pi;
		unw_get_proc_info(unw_cur, &pi);
		if ((void *)pi.start_ip <= backtrace_lua_entry_start_rip &&
		    backtrace_lua_entry_start_rip <= (void *)pi.end_ip) {
			backtrace_lua_entry_start_rip = (void *)pi.start_ip;
			backtrace_lua_entry_end_rip = (void *)pi.end_ip;
		}
	}
	if (rip > backtrace_lua_entry_start_rip &&
	    rip < backtrace_lua_entry_end_rip) {
		backtrace_collect_lua(bt, fiber);
	}
	backtrace_append_cxx(bt, rip);

	return bt->frames_count - prev_frames_count;
}
#endif // TARGET_OS_DARWIN

/**
 * Helper for @a backtrace_collect.
 * Must be called only by backtrace_collect function.
 * Returns its second parameter to simplify its call on foreign stack.
 * @returns @a stack
 * @sa backtrace_collect
 */
static void * NOINLINE
backtrace_collect_local(struct backtrace *bt, struct fiber *fiber,
			void *stack) {
	bt->frames_count = 0;
#ifndef TARGET_OS_DARWIN
	void *rips[BACKTRACE_MAX_FRAMES + 2];
	int frames_count = unw_backtrace(rips, BACKTRACE_MAX_FRAMES + 2);
	assert(rips[1] > backtrace_collect_start_rip &&
	       rips[1] < backtrace_collect_end_rip);
	for (int frame_no = 2; frame_no < frames_count; ++frame_no) {
		if (backtrace_add_rip(bt, fiber, rips[frame_no]) == 0)
			break;
	}
#else
	/*
	 * This dumb implementation was chosen because the DARWIN
	 * lacks unw_backtrace() routine from libunwind and
	 * usual backtrace() from <execinfo.h> has less capabilities
	 * than the libunwind version which uses DWARF.
	 */
	unw_cursor_t unw_cur;
	unw_context_t unw_ctx;

	unw_getcontext(&unw_ctx);
	unw_init_local(&unw_cur, &unw_ctx);

	for (int frame_no = 0; frame_no < BACKTRACE_MAX_FRAMES + 2;
	     ++frame_no) {
		if (frame_no >= 2) {
			if (backtrace_add_rip(bt, fiber, &unw_cur) == 0)
				break;
		}
		if (unw_step(&unw_cur) <= 0) {
			++frame_no;
			break;
		}
	}
#endif /* TARGET_OS_DARWIN */

	return stack;
}

/*
 * Restore target coro context and call backtrace_collect_local over it.
 * Work is done in four parts:
 * 1. Save current fiber context to a stack and save a stack pointer
 * 2. Restore target fiber context, stack pointer is not incremented because
 *    all target stack context should be preserved across a call. No stack
 *    changes are allowed until unwinding is done.
 * 3. Setup new stack frame and call unw_getcontext wrapper. All callee-safe
 *    registers are used by target fiber context, so old stack pointer is
 *    passed as second arg to wrapper func.
 * 4. Restore old stack pointer from wrapper return and restore old fiber
 *    contex.
 *
 * @param @bt backtrace
 * @param @fiber fiber to unwind its context.
 *
 * Note, this function needs a separate stack frame and therefore
 * MUST NOT be inlined.
 */
int NOINLINE
backtrace_collect(struct backtrace *bt, struct fiber *fiber) {
	if (fiber == fiber()) {
		backtrace_collect_local(bt, fiber(), NULL);
		return bt->frames_count;
	} else if ((fiber->flags & FIBER_IS_CANCELLABLE) == 0) {
		/*
		* Fiber stacks can't be traced for non-cancellable fibers
		* due to the limited capabilities of libcoro in CORO_ASM mode.
		*/
		bt->frames_count = 0;
		return 0;
	}

	volatile coro_context *coro_ctx = &fiber->ctx;
#if __amd64
__asm__ volatile(
	/* Preserve current context */
	"\tpushq %%rbp\n"
	"\tpushq %%rbx\n"
	"\tpushq %%r12\n"
	"\tpushq %%r13\n"
	"\tpushq %%r14\n"
	"\tpushq %%r15\n"
	/* Set first arg */
	"\tmovq %1, %%rdi\n"
	/* Set second arg */
	"\tmovq %2, %%rsi\n"
	/* Setup third arg as old sp */
	"\tmovq %%rsp, %%rdx\n"
	/* Restore target context, but not increment sp to preserve it */
	"\tmovq 0(%3), %%rsp\n"
	"\tmovq 0(%%rsp), %%r15\n"
	"\tmovq 8(%%rsp), %%r14\n"
	"\tmovq 16(%%rsp), %%r13\n"
	"\tmovq 24(%%rsp), %%r12\n"
	"\tmovq 32(%%rsp), %%rbx\n"
	"\tmovq 40(%%rsp), %%rbp\n"
	"\tandq $0xfffffffffffffff0, %%rsp\n"
	"\tleaq %P4(%%rip), %%rax\n"
	"\tcall *%%rax\n"
	/* Restore old sp and context */
	"\tmov %%rax, %%rsp\n"
	"\tpopq %%r15\n"
	"\tpopq %%r14\n"
	"\tpopq %%r13\n"
	"\tpopq %%r12\n"
	"\tpopq %%rbx\n"
	"\tpopq %%rbp\n"
	: "=m" (*bt)
	: "r" (bt), "r" (fiber), "r" (coro_ctx), "i" (backtrace_collect_local)
	: "rdi", "rsi", "rdx", "rax", "memory"
	);

#elif __i386
__asm__ volatile(
	/* Save current context */
	"\tpushl %%ebp\n"
	"\tpushl %%ebx\n"
	"\tpushl %%esi\n"
	"\tpushl %%edi\n"
	/* Setup third arg as old sp */
	"\tmovl %%esp, %%ecx\n"
	/* Setup second arg */
	"\tmovl %2, %%edx\n"
	/* Restore target context ,but not increment sp to preserve it */
	"\tmovl (%3), %%esp\n"
	"\tmovl 0(%%esp), %%edi\n"
	"\tmovl 4(%%esp), %%esi\n"
	"\tmovl 8(%%esp), %%ebx\n"
	"\tmovl 12(%%esp), %%ebp\n"
	/* Align stack by 16 according to the i386 ABI v1.1 */
	"\tandl $0xfffffff0, %%rsp\n"
	"\tsubl $4, %%esp\n"
	/* Setup first arg and call */
	"\tpushl %%ecx\n"
	"\tpushl %%edx\n"
	"\tpushl %1\n"
	"\tmovl %4, %%ecx\n"
	"\tcall *%%ecx\n"
	/* Restore old sp and context */
	"\tmovl %%eax, %%esp\n"
	"\tpopl %%edi\n"
	"\tpopl %%esi\n"
	"\tpopl %%ebx\n"
	"\tpopl %%ebp\n"
	: "=m" (*bt)
	: "r" (bt), "r" (fiber), "r" (coro_ctx), "i" (backtrace_collect_local)
	: "ecx", "edx", "eax", "memory"
	);

#elif __ARM_ARCH==7
__asm__ volatile(
	/* Save current context */
	".syntax unified\n"
	"\tvpush {d8-d15}\n"
	"\tpush {r4-r11,lr}\n"
	/* Setup first arg */
	"\tmov r0, %1\n"
	/* Setup second arg */
	"\tmov r1, %2\n"
	/* Save sp */
	"\tmov r2, sp\n"
	/* Restore target context, but not increment sp to preserve it */
	"\tldr sp, [%3]\n"
	"\tldmia sp, {r4-r11,lr}\n"
	"\tvldmia sp, {d8-d15}\n"
	/* Setup stack frame */
	"\tpush {r7, lr}\n"
	/* Align sp as on some ARMv7 archs it has to be multiple of 8 */
	"\tsub sp, #16\n"
	"\tstr r0, [sp, #8]\n"
	"\tstr r1, [sp, #4]\n"
	"\tstr r2, [sp, #0]\n"
	"\tmov r7, sp\n"
	"\tbl %4\n"
	/* Old sp is returned via r0 */
	"\tmov sp, r0\n"
	"\tpop {r4-r11,lr}\n"
	"\tvpop {d8-d15}\n"
	: "=m" (*bt)
	: "r" (bt), "r" (fiber), "r" (coro_ctx), "i" (backtrace_collect_local)
	: "lr", "r0", "r1", "r2", "ip", "memory"
	);

#elif __aarch64__
__asm__ volatile(
	/* Setup first arg */
	"\tmov x0, %1\n"
	/* Setup second arg */
	"\tmov x1, %2\n"
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
	"\tldr x3, [%3]\n"
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
	"\tbl %4\n"
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
	: "=m" (*bt)
	: "r" (bt), "r" (fiber), "r" (coro_ctx), "S" (backtrace_collect_local)
	: /*"lr", "r0", "r1", "r2", "ip" */
	 "x0", "x1", "x2", "x3", "x30", "memory"
	);
#endif

	return bt->frames_count;
}

int
backtrace_foreach(const struct backtrace *bt, backtrace_cxx_cb cxx_cb,
		  backtrace_lua_cb lua_cb, void *cb_ctx) {
	int demangle_status;
	char *demangle_buf = NULL;
	size_t demangle_buf_len = 0;

	int frame_no = 0;
	const struct backtrace_frame *frame = bt->frames;
	const struct backtrace_frame *end_frame = bt->frames + bt->frames_count;
	for (; frame != end_frame; ++frame) {
		if (frame->is_lua) {
			if (lua_cb != NULL &&
			    lua_cb(frame_no++, frame->name, frame->source,
				   frame->line, cb_ctx) != 0)
				break;
		} else {
			unw_word_t offset = 0;
			const char *proc = NULL;
			if (backtrace_resolve_from_ip((unw_word_t)frame->rip,
						      &proc, &offset) != 0)
				break;

			if (proc != NULL) {
				char *cxxname
					= abi::__cxa_demangle(proc,
							      demangle_buf,
							      &demangle_buf_len,
							      &demangle_status);
				if (cxxname != NULL) {
					demangle_buf = cxxname;
					proc = cxxname;
				}
			}

			if (cxx_cb(frame_no++, frame->rip, proc, offset,
				   cb_ctx) != 0)
				break;
		}
	}

	free(demangle_buf);

	return frame_no;
}

int
backtrace_concat(struct backtrace *to, const struct backtrace *add) {
	int add_frames = MIN(BACKTRACE_MAX_FRAMES - to->frames_count,
			     add->frames_count);
	memcpy(to->frames + to->frames_count, add->frames,
	       sizeof(to->frames[0]) * add_frames);
	to->frames_count += add_frames;

	return add_frames;
}

int
backtrace_append_cxx(struct backtrace *bt, void *rip) {
	if (bt->frames_count < BACKTRACE_MAX_FRAMES) {
		struct backtrace_frame *frame = bt->frames + bt->frames_count;
		frame->is_lua = false;
		frame->rip = rip;
		++bt->frames_count;
	}

	return bt->frames_count;
}

int
backtrace_append_lua(struct backtrace *bt, const char *name, const char *source,
		     int line) {
	if (bt->frames_count < BACKTRACE_MAX_FRAMES) {
		struct backtrace_frame *frame = bt->frames + bt->frames_count;
		frame->is_lua = true;
		frame->line = line;
		strncpy(frame->name, name, BACKTRACE_LUA_MAX_NAME - 1);
		strncpy(frame->source, source, BACKTRACE_LUA_MAX_NAME - 1);
		frame->name[BACKTRACE_LUA_MAX_NAME - 1] = '\0';
		frame->source[BACKTRACE_LUA_MAX_NAME - 1] = '\0';
		++bt->frames_count;
	}

	return bt->frames_count;
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

