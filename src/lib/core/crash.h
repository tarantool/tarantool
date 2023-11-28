/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#include "trivia/config.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** If reset, do not produce coredump on crash. */
extern bool crash_produce_coredump;

#if TARGET_OS_LINUX && defined(__x86_64__)
# define HAS_GREG
#endif

#ifdef HAS_GREG
/**
 * Values of x86 registers.
 */
struct crash_greg {
	/** r8 register value */
	uint64_t r8;
	/** r9 register value */
	uint64_t r9;
	/** r10 register value */
	uint64_t r10;
	/** r11 register value */
	uint64_t r11;
	/** r12 register value */
	uint64_t r12;
	/** r13 register value */
	uint64_t r13;
	/** r14 register value */
	uint64_t r14;
	/** r15 register value */
	uint64_t r15;
	/** di register value */
	uint64_t di;
	/** si register value */
	uint64_t si;
	/** bp register value */
	uint64_t bp;
	/** bx register value */
	uint64_t bx;
	/** dx register value */
	uint64_t dx;
	/** ax register value */
	uint64_t ax;
	/** cx register value */
	uint64_t cx;
	/** sp register value */
	uint64_t sp;
	/** ip register value */
	uint64_t ip;
	/** flags register value */
	uint64_t flags;
	/** cs register value */
	uint16_t cs;
	/** gs register value */
	uint16_t gs;
	/** fs register value */
	uint16_t fs;
	/** ss register value */
	uint16_t ss;
	/** err register value */
	uint64_t err;
	/** trapno register value */
	uint64_t trapno;
	/** oldmask register value */
	uint64_t oldmask;
	/** cr2 register value */
	uint64_t cr2;
	/** fpstate register value */
	uint64_t fpstate;
	/** Reserved. */
	uint64_t reserved1[8];
};
#endif /* HAS_GREG */

/*
 * Crash information.
 */
struct crash_info {
	/**
	 * These two are mostly useless as being plain addresses and without
	 * real binary crash dump file we can't use them for anything suitable
	 * (in terms of analysis sake) but keep for backward compatibility.
	 */

	/** Nearly useless. */
	void *context_addr;
	/** Nearly useless. */
	void *siginfo_addr;
#ifdef HAS_GREG
	/**
	 * Registers contents.
	 */
	struct crash_greg greg;
#endif
	/**
	 * Timestamp in seconds (realtime).
	 */
	long timestamp_rt;
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
};

typedef void
(*crash_callback_f)(struct crash_info *cinfo);

/**
 * Callback to call on crash. Default value is crash_report_stderr. NULL value
 * is not allowed.
 */
extern crash_callback_f crash_callback;

/**
 * Initialize crash signal handlers.
 */
void
crash_signal_init(void);

/**
 * Report crash information to the stderr (usually a current console).
 */
void
crash_report_stderr(struct crash_info *cinfo);

/**
 * Reset crash signal handlers.
 */
void
crash_signal_reset(void);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
