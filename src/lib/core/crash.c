/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>

#include "trivia/util.h"

#include "backtrace.h"
#include "crash.h"
#include "say.h"

#define pr_fmt(fmt)		"crash: " fmt
#define pr_syserr(fmt, ...)	say_syserror(pr_fmt(fmt), ##__VA_ARGS__)
#define pr_panic(fmt, ...)	panic(pr_fmt(fmt), ##__VA_ARGS__)

#ifdef TARGET_OS_LINUX
#ifndef __x86_64__
# error "Non x86-64 architectures are not supported"
#endif
struct crash_greg {
	uint64_t	r8;
	uint64_t	r9;
	uint64_t	r10;
	uint64_t	r11;
	uint64_t	r12;
	uint64_t	r13;
	uint64_t	r14;
	uint64_t	r15;
	uint64_t	di;
	uint64_t	si;
	uint64_t	bp;
	uint64_t	bx;
	uint64_t	dx;
	uint64_t	ax;
	uint64_t	cx;
	uint64_t	sp;
	uint64_t	ip;
	uint64_t	flags;
	uint16_t	cs;
	uint16_t	gs;
	uint16_t	fs;
	uint16_t	ss;
	uint64_t	err;
	uint64_t	trapno;
	uint64_t	oldmask;
	uint64_t	cr2;
	uint64_t	fpstate;
	uint64_t	reserved1[8];
};
#endif /* TARGET_OS_LINUX */

static struct crash_info {
	/**
	 * These two are mostly useless as being
	 * plain addresses and without real binary
	 * crash dump file we can't use them for
	 * anything suitable (in terms of analysis sake)
	 * but keep for backward compatibility.
	 */
	void *context_addr;
	void *siginfo_addr;
#ifdef TARGET_OS_LINUX
	/**
	 * Registers contents.
	 */
	struct crash_greg greg;
#endif
	/**
	 * Faulting address.
	 */
	void *siaddr;
	/**
	 * Crash signal number.
	 */
	int signo;
	/**
	 * Crash signal code.
	 */
	int sicode;
#ifdef ENABLE_BACKTRACE
	/**
	 * 1K of memory should be enough to keep the backtrace.
	 * In worst case it gonna be simply trimmed.
	 */
	char backtrace_buf[1024];
#endif
} crash_info;

/**
 * The routine is called inside crash signal handler so
 * be careful to not cause additional signals inside.
 */
static struct crash_info *
crash_collect(int signo, siginfo_t *siginfo, void *ucontext)
{
	struct crash_info *cinfo = &crash_info;

	cinfo->signo = signo;
	cinfo->sicode = siginfo->si_code;
	cinfo->siaddr = siginfo->si_addr;
	cinfo->context_addr = ucontext;
	cinfo->siginfo_addr = siginfo;

#ifdef ENABLE_BACKTRACE
	char *start = cinfo->backtrace_buf;
	backtrace(start, sizeof(cinfo->backtrace_buf));
#endif

#ifdef TARGET_OS_LINUX
	/*
	 * uc_mcontext on libc level looks somehow strange,
	 * they define an array of uint64_t where each register
	 * defined by REG_x macro.
	 *
	 * In turn the kernel is quite explicit about the context.
	 * Moreover it is a part of user ABI, thus won't be changed.
	 *
	 * Lets use memcpy here to make a copy in a fast way.
	 */
	ucontext_t *uc = ucontext;
	memcpy(&cinfo->greg, &uc->uc_mcontext, sizeof(cinfo->greg));
#endif

	return cinfo;
}

/**
 * Report crash information to the stderr
 * (usually a current console).
 */
static void
crash_report_stderr(struct crash_info *cinfo)
{
	if (cinfo->signo == SIGSEGV) {
		fprintf(stderr, "Segmentation fault\n");
		const char *signal_code_repr = NULL;

		switch (cinfo->sicode) {
		case SEGV_MAPERR:
			signal_code_repr = "SEGV_MAPERR";
			break;
		case SEGV_ACCERR:
			signal_code_repr = "SEGV_ACCERR";
			break;
		}

		if (signal_code_repr != NULL)
			fprintf(stderr, "  code: %s\n", signal_code_repr);
		else
			fprintf(stderr, "  code: %d\n", cinfo->sicode);
		/*
		 * fprintf is used instead of fdprintf, because
		 * fdprintf does not understand %p
		 */
		fprintf(stderr, "  addr: %p\n", cinfo->siaddr);
	} else {
		fprintf(stderr, "Got a fatal signal %d\n", cinfo->signo);
	}

	fprintf(stderr, "  context: %p\n", cinfo->context_addr);
	fprintf(stderr, "  siginfo: %p\n", cinfo->siginfo_addr);

#ifdef TARGET_OS_LINUX
# define fprintf_reg(__n, __v)				\
	fprintf(stderr, "  %-9s0x%-17llx%lld\n",	\
		__n, (long long)__v, (long long)__v)
	fprintf_reg("rax", cinfo->greg.ax);
	fprintf_reg("rbx", cinfo->greg.bx);
	fprintf_reg("rcx", cinfo->greg.cx);
	fprintf_reg("rdx", cinfo->greg.dx);
	fprintf_reg("rsi", cinfo->greg.si);
	fprintf_reg("rdi", cinfo->greg.di);
	fprintf_reg("rsp", cinfo->greg.sp);
	fprintf_reg("rbp", cinfo->greg.bp);
	fprintf_reg("r8", cinfo->greg.r8);
	fprintf_reg("r9", cinfo->greg.r9);
	fprintf_reg("r10", cinfo->greg.r10);
	fprintf_reg("r11", cinfo->greg.r11);
	fprintf_reg("r12", cinfo->greg.r12);
	fprintf_reg("r13", cinfo->greg.r13);
	fprintf_reg("r14", cinfo->greg.r14);
	fprintf_reg("r15", cinfo->greg.r15);
	fprintf_reg("rip", cinfo->greg.ip);
	fprintf_reg("eflags", cinfo->greg.flags);
	fprintf_reg("cs", cinfo->greg.cs);
	fprintf_reg("gs", cinfo->greg.gs);
	fprintf_reg("fs", cinfo->greg.fs);
	fprintf_reg("cr2", cinfo->greg.cr2);
	fprintf_reg("err", cinfo->greg.err);
	fprintf_reg("oldmask", cinfo->greg.oldmask);
	fprintf_reg("trapno", cinfo->greg.trapno);
# undef fprintf_reg
#endif /* TARGET_OS_LINUX */

	fprintf(stderr, "Current time: %u\n", (unsigned)time(0));
	fprintf(stderr, "Please file a bug at "
		"http://github.com/tarantool/tarantool/issues\n");

#ifdef ENABLE_BACKTRACE
	fprintf(stderr, "Attempting backtrace... Note: since the server has "
		"already crashed, \nthis may fail as well\n");
	fprintf(stderr, "%s", cinfo->backtrace_buf);
#endif
}

/**
 * Handle fatal (crashing) signal.
 *
 * Try to log as much as possible before dumping a core.
 *
 * Core files are not always allowed and it takes an effort to
 * extract useful information from them.
 *
 * *Recursive invocation*
 *
 * Unless SIGSEGV is sent by kill(), Linux resets the signal
 * a default value before invoking the handler.
 *
 * Despite that, as an extra precaution to avoid infinite
 * recursion, we count invocations of the handler, and
 * quietly _exit() when called for a second time.
 */
static void
crash_signal_cb(int signo, siginfo_t *siginfo, void *context)
{
	static volatile sig_atomic_t in_cb = 0;
	struct crash_info *cinfo;

	if (in_cb == 0) {
		in_cb = 1;
		cinfo = crash_collect(signo, siginfo, context);
		crash_report_stderr(cinfo);
	} else {
		/* Got a signal while running the handler. */
		fprintf(stderr, "Fatal %d while backtracing", signo);
	}

	/* Try to dump a core */
	struct sigaction sa = {
		.sa_handler = SIG_DFL,
	};
	sigemptyset(&sa.sa_mask);
	sigaction(SIGABRT, &sa, NULL);
	abort();
}

/**
 * Fatal signals we generate crash on.
 */
static const int crash_signals[] = { SIGSEGV, SIGFPE };

void
crash_signal_reset(void)
{
	struct sigaction sa = {
		.sa_handler = SIG_DFL,
	};
	sigemptyset(&sa.sa_mask);

	for (size_t i = 0; i < lengthof(crash_signals); i++) {
		if (sigaction(crash_signals[i], &sa, NULL) == 0)
			continue;
		pr_syserr("reset sigaction %d", crash_signals[i]);
	}
}

void
crash_signal_init(void)
{
	/*
	 * SA_RESETHAND resets handler action to the default
	 * one when entering handler.
	 *
	 * SA_NODEFER allows receiving the same signal
	 * during handler.
	 */
	struct sigaction sa = {
		.sa_flags = SA_RESETHAND | SA_NODEFER | SA_SIGINFO,
		.sa_sigaction = crash_signal_cb,
	};
	sigemptyset(&sa.sa_mask);

	for (size_t i = 0; i < lengthof(crash_signals); i++) {
		if (sigaction(crash_signals[i], &sa, NULL) == 0)
			continue;
		pr_panic("sigaction %d (%s)", crash_signals[i],
			 strerror(errno));
	}
}
