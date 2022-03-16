/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "core/backtrace.h"
#ifdef ENABLE_BACKTRACE
#include "core/fiber.h"
#include "core/tt_static.h"

#define UNW_LOCAL_ONLY
#include "libunwind.h"
#undef UNW_LOCAL_ONLY

#ifdef __APPLE__
#include <dlfcn.h>
#endif

#ifdef ENABLED_BRANCH_PROTECTION
#include <execinfo.h>
#endif /* ENABLED_BRANCH_PROTECTION */

#include <cxxabi.h>

#define C_FRAME_STR_FMT "#%-2d %p in %s+%zu"
#define CRLF "\n"

const char *const core_frame_str_fmt = C_FRAME_STR_FMT;

#ifdef __APPLE__
static void
append_frame(struct core_backtrace *bt, void *ip)
{
	assert(bt != nullptr);

	if (bt->frame_count < CORE_BACKTRACE_FRAME_COUNT_MAX) {
		struct core_frame *frame = &bt->frames[bt->frame_count++];
		frame->ip = ip;
	}
}
#endif /* __APPLE__ */

#ifdef __x86_64__
__attribute__ ((__force_align_arg_pointer__))
#endif /* __x86_64__ */
static NOINLINE void *
collect_current_stack(struct core_backtrace *bt, void *stack)
{
#ifndef __APPLE__
	int (*backtrace_alias)(void **, int);
#ifndef ENABLED_BRANCH_PROTECTION
	backtrace_alias = unw_backtrace;
#else /* ENABLED_BRANCH_PROTECTION */
	backtrace_alias = backtrace;
#endif /* ENABLED_BRANCH_PROTECTION */
	bt->frame_count = backtrace_alias((void **)bt->frames,
					  CORE_BACKTRACE_FRAME_COUNT_MAX);
#else /* __APPLE__ */
	/*
	 * Unfortunately, glibc `backtrace` does not work on macOS.
	 */
	bt->frame_count = 0;
	unw_context_t unw_ctx;
	int rc = unw_getcontext(&unw_ctx);
	if (rc != 0) {
		say_error("unwinding error: unw_getcontext failed");
		return stack;
	}
	unw_cursor_t unw_cur;
	rc = unw_init_local(&unw_cur, &unw_ctx);
	if (rc != 0) {
		say_error("unwinding error: unw_init_local failed");
		return stack;
	}
	for (unsigned frame_no = 0; frame_no < CORE_BACKTRACE_FRAME_COUNT_MAX;
	     ++frame_no) {
		unw_word_t ip;
		rc = unw_get_reg(&unw_cur, UNW_REG_IP, &ip);
		if (rc != 0) {
			say_error("unwinding error: unw_get_reg failed with "
				  "status: %d", rc);
			return stack;
		}
		append_frame(bt, (void *)ip);
		rc = unw_step(&unw_cur);
		if (rc < 0)
			say_error("unwinding error: unw_step failed with "
				  "status: %d", rc);
		if (rc == 0) {
			break;
		}
	}
#endif /* __APPLE__ */
	return stack;
}

/*
 * Restore target fiber context (if needed) and call
 * `backtrace_collect_curr_stk` over it.
 */
NOINLINE void
core_backtrace_collect_frames(struct core_backtrace *bt,
			      const struct fiber *fiber, int skip_frames)
{
	assert(bt != nullptr);
	assert(fiber != nullptr);

	if (fiber == fiber()) {
		collect_current_stack(bt, nullptr);
		goto skip_frames;
	}
	if ((fiber->flags & FIBER_IS_CANCELLABLE) == 0) {
		/*
		 * Fiber stacks can't be traced for non-cancellable fibers
		 * due to the limited capabilities of libcoro in CORO_ASM mode.
		 */
		bt->frame_count = 0;
		return;
	}
	/*
	 * 1. Save current fiber context on stack.
	 * 2. Setup arguments for `backtrace_collect_current_stack` call.
	 * 3. Restore target fiber context.
	 * 4. Setup current frame information (CFI) and call
	 *    `backtrace_collect_current_stack`.
	 * 5. Restore original stack pointer from
	 *    `backtrace_collect_current_stack`'s return value and restore
	 *    original fiber context.
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
	: "rm" (bt), "r" (&fiber->ctx), "i" (collect_current_stack)
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
	/* Setup second function argument and save current fiber context. */
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
	/* Setup first function argument. */
	"\tldr x0, %1\n"
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
	: "m" (bt), "r" (&fiber->ctx), "S" (collect_current_stack)
	: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
	  "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",
	  "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10",
	  "s11", "s12", "s13", "s14", "s15", "s16", "s17", "s18", "s19", "s20",
	  "s21", "s22", "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30",
	  "s31", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7");
#endif /* __x86_64__ */
skip_frames:
	bt->frame_count -= skip_frames;
	memmove(bt->frames, bt->frames + skip_frames,
		sizeof(bt->frames[0]) * bt->frame_count);
}

const char *
core_backtrace_resolve_frame(unw_word_t ip, unw_word_t *offset,
			     char **demangle_buf, size_t *demangle_buf_len)
{
	assert(offset != nullptr);
	assert(demangle_buf != nullptr);
	assert(demangle_buf_len != nullptr);

#ifndef __APPLE__
	unw_accessors_t *acc = unw_get_accessors(unw_local_addr_space);
	assert(acc->get_proc_name != nullptr);
	char *proc_name_buf = tt_static_buf();
	int rc = acc->get_proc_name(unw_local_addr_space, ip, proc_name_buf,
				    TT_STATIC_BUF_LEN, offset, nullptr);
	if (rc != 0) {
		say_error("unwinding error: `get_proc_name` accessor failed: "
			  "%s", unw_strerror(rc));
		return nullptr;
	}
#else /* __APPLE__ */
	Dl_info dli;
	if (dladdr((void *)ip, &dli) == 0) {
		say_error("unwinding error: `dladdr` failed");
		return nullptr;
	}

	*offset = ip - (unw_word_t)dli.dli_saddr;
	const char *proc_name_buf = dli.dli_sname;
#endif /* __APPLE__ */
	int demangle_status;
	char *demangled_proc_name =
		abi::__cxa_demangle(proc_name_buf, *demangle_buf,
				    demangle_buf_len, &demangle_status);
	if (demangle_status != 0 && demangle_status != -2) {
		say_error("unwinding error: "
			  "abi::__cxa_demangle failed with "
			  "status: %d", demangle_status);
		return proc_name_buf;
	}
	if (demangled_proc_name != nullptr)
		proc_name_buf = *demangle_buf = demangled_proc_name;
	return proc_name_buf;
}

void
core_backtrace_foreach(const struct core_backtrace *bt, int begin, int end,
		       core_resolved_frame_cb core_resolved_frame_cb, void *ctx)
{
	assert(bt != nullptr);
	assert(core_resolved_frame_cb != nullptr);

	int frame_no = begin + 1;
	char *demangle_buf = nullptr;
	size_t demangle_buf_len = 0;
	for (const struct core_frame *frame = &bt->frames[begin];
	     frame != &bt->frames[end];
	     ++frame, ++frame_no) {
		auto ip = (unw_word_t)frame->ip;
		unw_word_t offset = 0;
		const char *proc_name =
			core_backtrace_resolve_frame(ip, &offset,
						     &demangle_buf,
						     &demangle_buf_len);
		const struct core_resolved_frame resolved_frame{*frame,
								frame_no,
								proc_name,
								offset};
		int rc = core_resolved_frame_cb(&resolved_frame, ctx);
		if (rc != 0)
			break;
	}
	free(demangle_buf);
}

void
core_backtrace_dump_frames(const struct core_backtrace *bt, char *buf,
			   size_t buf_len)
{
	assert(bt != nullptr);

	char *buf_end = &buf[buf_len];
	struct dump_ctx {
		char *buf;
		char *buf_end;
	} dump_ctx{buf, buf_end};
	auto dump = [](const struct core_resolved_frame *frame, void *ctx) {
		assert(frame != NULL);
		assert(ctx != nullptr);

		auto dump_ctx = (struct dump_ctx *)ctx;
		int chars_written = snprintf(dump_ctx->buf,
					     dump_ctx->buf_end - dump_ctx->buf,
					     C_FRAME_STR_FMT CRLF, frame->no,
					     (void *)frame->core_frame.ip,
					     frame->proc_name, frame->offset);
		if (chars_written < 0) {
			say_error("unwinding error: snprintf failed: %s",
				  strerror(errno));
			return -1;
		}
		dump_ctx->buf += chars_written;
		if (dump_ctx->buf >= dump_ctx->buf_end) {
			return -1;
		}
		return 0;
	};
	core_backtrace_foreach(bt, 0, bt->frame_count, dump,
			       &dump_ctx);
}
#endif /* ENABLE_BACKTRACE */
