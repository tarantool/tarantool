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

#include "small/region.h"
/*
 * We use a global static buffer because it is too late to do any
 * allocation when we are printing backtrace and fiber stack is
 * small.
 */

#define BACKTRACE_NAME_MAX 200

static char backtrace_buf[4096 * 4];

static __thread struct region cache_region;
static __thread struct mh_i64ptr_t *proc_cache = NULL;

struct proc_cache_entry {
	char name[BACKTRACE_NAME_MAX];
	unw_word_t offset;
};

void
backtrace_proc_cache_clear()
{
	if (proc_cache == NULL)
		return;
	region_destroy(&cache_region);
	mh_i64ptr_delete(proc_cache);
	proc_cache = NULL;
}

const char *
get_proc_name(unw_cursor_t *unw_cur, unw_word_t *offset, bool skip_cache)
{
	static __thread char proc_name[BACKTRACE_NAME_MAX];
	unw_word_t ip;
	unw_get_reg(unw_cur, UNW_REG_IP, &ip);

	if (skip_cache) {
		unw_get_proc_name(unw_cur, proc_name, sizeof(proc_name),
				  offset);
		return proc_name;
	}

	struct proc_cache_entry *entry;
	struct mh_i64ptr_node_t node;
	mh_int_t k;

	if (proc_cache == NULL) {
		region_create(&cache_region, &cord()->slabc);
		proc_cache = mh_i64ptr_new();
		if (proc_cache == NULL) {
			unw_get_proc_name(unw_cur, proc_name, sizeof(proc_name),
					  offset);
			goto error;
		}
	}

	k  = mh_i64ptr_find(proc_cache, ip, NULL);
	if (k != mh_end(proc_cache)) {
		entry = (struct proc_cache_entry *)
			mh_i64ptr_node(proc_cache, k)->val;
		snprintf(proc_name, BACKTRACE_NAME_MAX, "%s", entry->name);
		*offset = entry->offset;
	}  else {
		unw_get_proc_name(unw_cur, proc_name, sizeof(proc_name),
				  offset);

		entry = (struct proc_cache_entry *)
			region_alloc(&cache_region, sizeof(struct proc_cache_entry));
		if (entry == NULL)
			goto error;
		node.key = ip;
		node.val = entry;
		snprintf(entry->name, BACKTRACE_NAME_MAX, "%s", proc_name);
		entry->offset = *offset;

		k = mh_i64ptr_put(proc_cache, &node, NULL, NULL);
		if (k == mh_end(proc_cache)) {
			free(entry);
			goto error;
		}
	}
error:
	return proc_name;
}

char *
backtrace()
{
	int frame_no = 0;
	unw_word_t sp = 0, old_sp = 0, ip, offset;
	unw_context_t unw_context;
	unw_getcontext(&unw_context);
	unw_cursor_t unw_cur;
	unw_init_local(&unw_cur, &unw_context);
	char *p = backtrace_buf;
	char *end = p + sizeof(backtrace_buf) - 1;
	int unw_status;
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
	*p = '\0';
	return backtrace_buf;
}

/*
 * Libunwind unw_getcontext wrapper.
 * unw_getcontext can be a macros on some platform and can not be called
 * directly from asm code. Stack variable pass through the wrapper to
 * preserve a old stack pointer during the wrapper call.
 *
 * @param unw_context unwind context to store execution state
 * @param stack pointer to preserve.
 * @retval preserved stack pointer.
 */
static void *
unw_getcontext_f(unw_context_t *unw_context, void *stack)
{
	unw_getcontext(unw_context);
	return stack;
}

/*
 * Restore target coro context and call unw_getcontext over it.
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
 * @param @unw_context unwind context to store execution state.
 * @param @coro_ctx fiber context to unwind.
 */
void
coro_unwcontext(unw_context_t *unw_context, struct coro_context *coro_ctx)
{
#if __amd64
__asm__(
	/* Preserve current context */
	"\tpushq %%rbp\n"
	"\tpushq %%rbx\n"
	"\tpushq %%r12\n"
	"\tpushq %%r13\n"
	"\tpushq %%r14\n"
	"\tpushq %%r15\n"
	/* Setup second arg as old sp */
	"\tmovq %%rsp, %%rsi\n"
	/* Restore target context, but not increment sp to preserve it */
	"\tmovq 0(%1), %%rsp\n"
	"\tmovq 0(%%rsp), %%r15\n"
	"\tmovq 8(%%rsp), %%r14\n"
	"\tmovq 16(%%rsp), %%r13\n"
	"\tmovq 24(%%rsp), %%r12\n"
	"\tmovq 32(%%rsp), %%rbx\n"
	"\tmovq 40(%%rsp), %%rbp\n"
	/* Set first arg and call */
	"\tmovq %0, %%rdi\n"
#ifdef TARGET_OS_DARWIN
	"\tandq $0xfffffffffffffff0, %%rsp\n"
#endif
	"\tleaq %P2(%%rip), %%rax\n"
	"\tcall *%%rax\n"
	/* Restore old sp and context */
	"\tmov %%rax, %%rsp\n"
	"\tpopq %%r15\n"
	"\tpopq %%r14\n"
	"\tpopq %%r13\n"
	"\tpopq %%r12\n"
	"\tpopq %%rbx\n"
	"\tpopq %%rbp\n"
	:
	: "r" (unw_context), "r" (coro_ctx), "i" (unw_getcontext_f)
	: "rdi", "rsi", "rax"
	);

#elif __i386
__asm__(
	/* Save current context */
	"\tpushl %%ebp\n"
	"\tpushl %%ebx\n"
	"\tpushl %%esi\n"
	"\tpushl %%edi\n"
	/* Setup second arg as old sp */
	"\tmovl %%esp, %%ecx\n"
	/* Restore target context ,but not increment sp to preserve it */
	"\tmovl (%1), %%esp\n"
	"\tmovl 0(%%esp), %%edi\n"
	"\tmovl 4(%%esp), %%esi\n"
	"\tmovl 8(%%esp), %%ebx\n"
	"\tmovl 12(%%esp), %%ebp\n"
	/* Setup first arg and call */
	"\tpushl %%ecx\n"
	"\tpushl %0\n"
	"\tmovl %2, %%ecx\n"
	"\tcall *%%ecx\n"
	/* Restore old sp and context */
	"\tmovl %%eax, %%esp\n"
	"\tpopl %%edi\n"
	"\tpopl %%esi\n"
	"\tpopl %%ebx\n"
	"\tpopl %%ebp\n"
	:
	: "r" (unw_context), "r" (coro_ctx), "i" (unw_getcontext_f)
	: "ecx", "eax"
	);

#elif __ARM_ARCH==7
__asm__(
	/* Save current context */
	".syntax unified\n"
	"\tvpush {d8-d15}\n"
	"\tpush {r4-r11,lr}\n"
	/* Save sp */
	"\tmov r1, sp\n"
	/* Restore target context, but not increment sp to preserve it */
	"\tldr sp, [%1]\n"
	"\tldmia sp, {r4-r11,lr}\n"
	"\tvldmia sp, {d8-d15}\n"
	/* Setup first arg */
	"\tmov r0, %0\n"
	/* Setup stack frame */
	"\tpush {r7, lr}\n"
	"\tsub sp, #8\n"
	"\tstr r0, [sp, #4]\n"
	"\tstr r1, [sp, #0]\n"
	"\tmov r7, sp\n"
	"\tbl %2\n"
	/* Old sp is returned via r0 */
	"\tmov sp, r0\n"
	"\tpop {r4-r11,lr}\n"
	"\tvpop {d8-d15}\n"
	:
	: "r" (unw_context), "r" (coro_ctx), "i" (unw_getcontext_f)
	: "lr", "r0", "r1", "ip"
	);

#elif __aarch64__
__asm__(
	/* Save current context */
	"\tsub x1, sp, #8 * 20\n"
	"\tstp x19, x20, [x1, #16 * 0]\n"
	"\tstp x21, x22, [x1, #16 * 1]\n"
	"\tstp x23, x24, [x1, #16 * 2]\n"
	"\tstp x25, x26, [x1, #16 * 3]\n"
	"\tstp x27, x28, [x1, #16 * 4]\n"
	"\tstp x29, x30, [x1, #16 * 5]\n"
	"\tstp d8,  d9,  [x1, #16 * 6]\n"
	"\tstp d10, d11, [x1, #16 * 7]\n"
	"\tstp d12, d13, [x1, #16 * 8]\n"
	"\tstp d14, d15, [x1, #16 * 9]\n"
	/* Restore target context */
	"\tldr x2, [%1]\n"
	"\tldp x19, x20, [x2, #16 * 0]\n"
	"\tldp x21, x22, [x2, #16 * 1]\n"
	"\tldp x23, x24, [x2, #16 * 2]\n"
	"\tldp x25, x26, [x2, #16 * 3]\n"
	"\tldp x27, x28, [x2, #16 * 4]\n"
	"\tldp x29, x30, [x2, #16 * 5]\n"
	"\tldp d8,  d9,  [x2, #16 * 6]\n"
	"\tldp d10, d11, [x2, #16 * 7]\n"
	"\tldp d12, d13, [x2, #16 * 8]\n"
	"\tldp d14, d15, [x2, #16 * 9]\n"
	"\tmov sp, x2\n"
	/* Setup fisrst arg */
	"\tmov x0, %0\n"
	"\tbl %2\n"
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
	: "r" (unw_context), "r" (coro_ctx), "i" (unw_getcontext_f)
	: /*"lr", "r0", "r1", "ip" */
	 "x0", "x1", "x2", "x30"
	);
#endif
}

void
backtrace_foreach(backtrace_cb cb, coro_context *coro_ctx, void *cb_ctx)
{
	unw_cursor_t unw_cur;
	unw_context_t unw_ctx;
	coro_unwcontext(&unw_ctx, coro_ctx);
	unw_init_local(&unw_cur, &unw_ctx);
	int frame_no = 0;
	unw_word_t sp = 0, old_sp = 0, ip, offset;
	int unw_status, demangle_status;
	char *demangle_buf = NULL;
	size_t demangle_buf_len = 0;

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
		proc = get_proc_name(&unw_cur, &offset, false);

		char *cxxname = abi::__cxa_demangle(proc, demangle_buf,
						    &demangle_buf_len,
						    &demangle_status);
		if (cxxname != NULL)
			demangle_buf = cxxname;
		if (frame_no > 0 &&
		    (cb(frame_no - 1, (void *)ip, cxxname != NULL ? cxxname : proc,
			offset, cb_ctx) != 0))
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
	free(demangle_buf);
}

void
print_backtrace()
{
	fdprintf(STDERR_FILENO, "%s", backtrace());
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

