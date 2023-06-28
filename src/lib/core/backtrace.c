/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "core/backtrace.h"

#ifdef ENABLE_BACKTRACE
#include "core/fiber.h"
#include "core/tt_static.h"
#include "core/tt_strerror.h"

#ifdef __APPLE__
#include <dlfcn.h>
#endif /* __APPLE__ */

#include "libunwind.h"

#include "cxx_abi.h"
#include "proc_name_cache.h"

#ifdef __APPLE__
/*
 * Append frame to backtrace.
 */
static void
append_frame(struct backtrace *bt, void *ip)
{
	if (bt->frame_count < BACKTRACE_FRAME_COUNT_MAX) {
		struct backtrace_frame *frame = &bt->frames[bt->frame_count++];
		frame->ip = ip;
	}
}
#endif /* __APPLE__ */

/*
 * Collect backtrace from current stack.
 *
 * The `stack` argument is used to simplify context restore: by returning it, we
 * reuse the ABI corresponding return value register.
 */
#ifdef __x86_64__
__attribute__ ((__force_align_arg_pointer__))
#endif /* __x86_64__ */
static NOINLINE void *
collect_current_stack(struct backtrace *bt, void *stack)
{
#ifndef __APPLE__
	bt->frame_count = unw_backtrace((void **)bt->frames,
					BACKTRACE_FRAME_COUNT_MAX);
#else /* __APPLE__ */
	/*
	 * Unfortunately, glibc `backtrace` does not work on macOS.
	 */
	bt->frame_count = 0;
	unw_context_t unw_ctx;
	int rc = unw_getcontext(&unw_ctx);
	if (rc != 0) {
		say_debug("unwinding error: unw_getcontext failed");
		return stack;
	}
	unw_cursor_t unw_cur;
	rc = unw_init_local(&unw_cur, &unw_ctx);
	if (rc != 0) {
		say_debug("unwinding error: unw_init_local failed");
		return stack;
	}
	for (unsigned frame_no = 0; frame_no < BACKTRACE_FRAME_COUNT_MAX;
	     ++frame_no) {
		unw_word_t ip;
		rc = unw_get_reg(&unw_cur, UNW_REG_IP, &ip);
		if (rc != 0) {
			say_debug("unwinding error: unw_get_reg failed with "
				  "status: %d", rc);
			return stack;
		}
#ifdef __aarch64__
		/*
		 * Apple's libunwind for AArch64 returns the IP with the Pointer
		 * Authentication Codes (bits 47-63). Strip them out.
		 */
		ip &= 0x7fffffffffffull;
#endif /* __aarch64__ */
		append_frame(bt, (void *)ip);
		rc = unw_step(&unw_cur);
		if (rc <= 0) {
			if (rc < 0)
				say_debug("unwinding error: unw_step failed "
					  "with status: %d", rc);
			break;
		}
	}
#endif /* __APPLE__ */
	return stack;
}

/*
 * Restore target fiber context (if needed) and call `collect_current_stack`
 * over it.
 * It is guaranteed that if NULL passed as fiber argument, fiber
 * module will not be used.
 */
NOINLINE void
backtrace_collect(struct backtrace *bt, const struct fiber *fiber,
		  int skip_frames)
{
	assert(skip_frames >= 0);
	/* The user should not see the frame of `collect_current_stack`. */
	++skip_frames;

	if (fiber == NULL || fiber == fiber()) {
		collect_current_stack(bt, NULL);
		goto skip_frames;
	}
	if (fiber->caller == NULL) {
		/*
		 * This condition implies that the fiber was never scheduled:
		 * in turn, this means that its stack was never set up —
		 * hence, we cannot collect its backtrace.
		 */
		bt->frame_count = 0;
		return;
	}
	/*
	 * 1. Save current fiber context on stack.
	 * 2. Setup arguments for `collect_current_stack` call.
	 * 3. Restore target fiber context.
	 * 4. Setup current frame information (CFI) and call
	 *    `collect_current_stack`.
	 * 5. Restore original stack pointer from `collect_current_stack`'s
	 *    return value and restore original fiber context.
	 */
#ifdef __x86_64__
__asm__ volatile(
	/* Save current fiber context. */
	"\tpushq %%rbp\n"
	"\tpushq %%rbx\n"
	"\tpushq %%r12\n"
	"\tpushq %%r13\n"
	"\tpushq %%r14\n"
	"\tpushq %%r15\n"
	/* Setup first function argument. */
	"\tmovq %1, %%rdi\n"
	/* Setup second function argument. */
	"\tmovq %%rsp, %%rsi\n"
	/* Restore target fiber context. */
	"\tmovq (%2), %%rsp\n"
	"\tmovq 0(%%rsp), %%r15\n"
	"\tmovq 8(%%rsp), %%r14\n"
	"\tmovq 16(%%rsp), %%r13\n"
	"\tmovq 24(%%rsp), %%r12\n"
	"\tmovq 32(%%rsp), %%rbx\n"
	"\tmovq 40(%%rsp), %%rbp\n"
	/* Setup CFI. */
	".cfi_remember_state\n"
	".cfi_def_cfa %%rsp, 8 * 7\n"
	"\tleaq %P3(%%rip), %%rax\n"
	"\tcall *%%rax\n"
	".cfi_restore_state\n"
	/* Restore original fiber context. */
	"\tmov %%rax, %%rsp\n"
	"\tpopq %%r15\n"
	"\tpopq %%r14\n"
	"\tpopq %%r13\n"
	"\tpopq %%r12\n"
	"\tpopq %%rbx\n"
	"\tpopq %%rbp\n"
	: "=m" (*bt)
	: "r" (bt), "r" (&fiber->ctx), "i" (collect_current_stack)
	: "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "xmm0",
	  "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8",
	  "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
#ifdef __AVX512F__
	  "xmm16", "xmm17", "xmm18", "xmm19", "xmm20", "xmm21", "xmm22",
	  "xmm23", "xmm24", "xmm25", "xmm26", "xmm27", "xmm28", "xmm29",
	  "xmm30", "xmm31", "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7",
#endif /* __AVX512F__ */
	  "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7", "st",
	  "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
#elif __aarch64__
__asm__ volatile(
	/* Save current fiber context. */
	"\tsub sp, sp, #8 * 20\n"
	"\tstp x19, x20, [sp, #16 * 0]\n"
	"\tstp x21, x22, [sp, #16 * 1]\n"
	"\tstp x23, x24, [sp, #16 * 2]\n"
	"\tstp x25, x26, [sp, #16 * 3]\n"
	"\tstp x27, x28, [sp, #16 * 4]\n"
	"\tstp x29, x30, [sp, #16 * 5]\n"
	"\tstp d8,  d9,  [sp, #16 * 6]\n"
	"\tstp d10, d11, [sp, #16 * 7]\n"
	"\tstp d12, d13, [sp, #16 * 8]\n"
	"\tstp d14, d15, [sp, #16 * 9]\n"
	/* Setup first function argument. */
	"\tmov x0, %1\n"
	/* Setup second function argument. */
	"\tmov x1, sp\n"
	/* Restore target fiber context. */
	"\tldr x2, [%2]\n"
	"\tmov sp, x2\n"
	"\tldp x19, x20, [sp, #16 * 0]\n"
	"\tldp x21, x22, [sp, #16 * 1]\n"
	"\tldp x23, x24, [sp, #16 * 2]\n"
	"\tldp x25, x26, [sp, #16 * 3]\n"
	"\tldp x27, x28, [sp, #16 * 4]\n"
	"\tldp x29, x30, [sp, #16 * 5]\n"
	"\tldp d8,  d9,  [sp, #16 * 6]\n"
	"\tldp d10, d11, [sp, #16 * 7]\n"
	"\tldp d12, d13, [sp, #16 * 8]\n"
	"\tldp d14, d15, [sp, #16 * 9]\n"
	/* Setup CFI. */
	".cfi_remember_state\n"
	".cfi_def_cfa sp, 16 * 10\n"
	".cfi_offset x29, -16 * 5\n"
	".cfi_offset x30, -16 * 5 + 8\n"
	"\tbl %3\n"
	".cfi_restore_state\n"
	/* Restore original fiber context. */
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
	: "r" (bt), "r" (&fiber->ctx), "S" (collect_current_stack)
	: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
	  "x11", "x12", "x13", "x14", "x15", "x16", "x17",
#ifndef __APPLE__
	  "x18",
#endif /* __APPLE__ */
	  "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10",
	  "s11", "s12", "s13", "s14", "s15", "s16", "s17", "s18", "s19", "s20",
	  "s21", "s22", "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30",
	  "s31", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7");
#endif /* __x86_64__ */
skip_frames:
	bt->frame_count -= MIN(skip_frames, bt->frame_count);
	memmove(bt->frames, bt->frames + skip_frames,
		sizeof(bt->frames[0]) * bt->frame_count);
}

const char *
backtrace_frame_resolve(const struct backtrace_frame *frame,
			uintptr_t *offset)
{
	const char *name = proc_name_cache_find(frame->ip, offset);
	if (name != NULL)
		return name;
#ifndef __APPLE__
	unw_accessors_t *acc = unw_get_accessors(unw_local_addr_space);
	assert(acc->get_proc_name != NULL);
	char proc_name_buf[128];
	unw_word_t unw_offset;
	int rc = acc->get_proc_name(unw_local_addr_space, (unw_word_t)frame->ip,
				    proc_name_buf, sizeof(proc_name_buf),
				    &unw_offset, NULL);
	/*
	 * In case result does not fit into buffer we got UNW_ENOMEM and
	 * result will be truncated.
	 */
	if (rc != 0 && rc != -UNW_ENOMEM) {
		say_debug("unwinding error: `get_proc_name` accessor failed: "
			  "%s", unw_strerror(rc));
		return NULL;
	}
	*offset = (uintptr_t)unw_offset;
#else /* __APPLE__ */
	Dl_info dli;
	if (dladdr(frame->ip, &dli) == 0) {
		say_debug("unwinding error: `dladdr` failed");
		return NULL;
	}

	*offset = (uintptr_t)frame->ip - (uintptr_t)dli.dli_saddr;
	const char *proc_name_buf = dli.dli_sname;
#endif /* __APPLE__ */
	const char *demangled_name = cxx_abi_demangle(proc_name_buf);
	proc_name_cache_insert(frame->ip, demangled_name, *offset);
	return demangled_name;
}

int
backtrace_snprint(char *buf, int buf_len, const struct backtrace *bt)
{
	int frame_no = 1;
	int total = 0;
	for (const struct backtrace_frame *frame = bt->frames;
	     frame != &bt->frames[bt->frame_count]; ++frame, ++frame_no) {
		uintptr_t offset = 0;
		const char *proc_name = backtrace_frame_resolve(frame, &offset);
		proc_name = proc_name != NULL ? proc_name : "??";
		SNPRINT(total, snprintf, buf, buf_len, C_FRAME_STR_FMT "\n",
			frame_no, frame->ip, proc_name, offset);
	}
	return total;
}

void
backtrace_print(const struct backtrace *bt, int fd)
{
	int frame_no = 1;
	for (const struct backtrace_frame *frame = bt->frames;
	     frame != &bt->frames[bt->frame_count]; ++frame, ++frame_no) {
		uintptr_t offset = 0;
		const char *proc_name = backtrace_frame_resolve(frame, &offset);
		proc_name = proc_name != NULL ? proc_name : "??";
		int chars_written = dprintf(fd, C_FRAME_STR_FMT "\n", frame_no,
					    frame->ip, proc_name, offset);
		if (chars_written < 0) {
			say_debug("unwinding error: dprintf failed: %s",
				  tt_strerror(errno));
			break;
		}
	}
}
#endif /* ENABLE_BACKTRACE */
