/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include "small/static.h"
#include "trivia/util.h"
#include "uuid/tt_uuid.h"

#include "box/replication.h"
#include "backtrace.h"
#include "crash.h"
#include "say.h"

#define pr_fmt(fmt)		"crash: " fmt
#define pr_debug(fmt, ...)	say_debug(pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...)	say_info(pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...)	say_error(pr_fmt(fmt), ##__VA_ARGS__)
#define pr_syserr(fmt, ...)	say_syserror(pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...)	fprintf(stderr, pr_fmt(fmt) "\n", ##__VA_ARGS__)
#define pr_panic(fmt, ...)	panic(pr_fmt(fmt), ##__VA_ARGS__)

/** Use strlcpy with destination as an array */
#define strlcpy_a(dst, src) strlcpy(dst, src, sizeof(dst))

#if defined(TARGET_OS_LINUX) && defined(__x86_64__)
# define HAS_GREG
#endif

#ifdef HAS_GREG
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
#endif /* HAS_GREG */

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
#ifdef HAS_GREG
	/**
	 * Registers contents.
	 */
	struct crash_greg greg;
#endif
	/**
	 * Timestamp in nanoseconds (realtime).
	 */
	uint64_t timestamp_rt;
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

static char tarantool_path[PATH_MAX];
static char feedback_host[CRASH_FEEDBACK_HOST_MAX];
static bool send_crashinfo = false;
static uint64_t timestamp_mono = 0;

static inline uint64_t
timespec_to_ns(struct timespec *ts)
{
	return (uint64_t)ts->tv_sec * 1000000000 + (uint64_t)ts->tv_nsec;
}

static char *
ns_to_localtime(uint64_t timestamp, char *buf, ssize_t len)
{
	time_t sec = timestamp / 1000000000;
	char *start = buf;
	struct tm tm;

	/*
	 * Use similar format as say_x logger. Except plain
	 * seconds should be enough.
	 */
	localtime_r(&sec, &tm);
	ssize_t total = strftime(start, len, "%F %T %Z", &tm);
	start += total;
	if (total < len)
		return buf;
	buf[len - 1] = '\0';
	return buf;
}

void
crash_init(const char *tarantool_bin)
{
	strlcpy_a(tarantool_path, tarantool_bin);
	if (strlen(tarantool_path) < strlen(tarantool_bin))
		pr_panic("executable path is trimmed");

	/*
	 * We need to keep clock data locally to
	 * report uptime without binding to libev
	 * and etc. Because we're reporting information
	 * at the moment when crash happens and we are to
	 * be independent as much as we can.
	 */
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
		timestamp_mono = timespec_to_ns(&ts);
	else
		pr_syserr("Can't fetch monotonic clock, ignore");
}

void
crash_cfg(const char *host, bool is_enabled)
{
	if (host == NULL || !is_enabled) {
		if (send_crashinfo) {
			pr_debug("disable sending crashinfo feedback");
			send_crashinfo = false;
			feedback_host[0] = '\0';
		}
		return;
	}

	if (strcmp(feedback_host, host) != 0) {
		strlcpy_a(feedback_host, host);
		/*
		 * The caller should have tested already
		 * that there is enough space to keep
		 * the host address.
		 */
		assert(strlen(feedback_host) == strlen(host));
	}

	if (!send_crashinfo) {
		pr_debug("enable sending crashinfo feedback");
		send_crashinfo = true;
	}
}

/**
 * The routine is called inside crash signal handler so
 * be careful to not cause additional signals inside.
 */
static struct crash_info *
crash_collect(int signo, siginfo_t *siginfo, void *ucontext)
{
	struct crash_info *cinfo = &crash_info;
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
		cinfo->timestamp_rt = timespec_to_ns(&ts);
	else
		cinfo->timestamp_rt = 0;

	cinfo->signo = signo;
	cinfo->sicode = siginfo->si_code;
	cinfo->siaddr = siginfo->si_addr;
	cinfo->context_addr = ucontext;
	cinfo->siginfo_addr = siginfo;

#ifdef ENABLE_BACKTRACE
	char *start = cinfo->backtrace_buf;
	backtrace(start, sizeof(cinfo->backtrace_buf));
#endif

#ifdef HAS_GREG
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
 * Report crash information to the feedback daemon
 * (ie send it to feedback daemon).
 */
static int
crash_report_feedback_daemon(struct crash_info *cinfo)
{
	/*
	 * Update to a new number if the format get changed.
	 */
	const int crashinfo_version = 1;

	char *p = static_alloc(SMALL_STATIC_SIZE);
	char *tail = &p[SMALL_STATIC_SIZE];
	char *e = &p[SMALL_STATIC_SIZE];
	char *head = p;

	int total = 0;
	(void)total;
	int size = 0;

#define snprintf_safe(...) SNPRINT(total, snprintf, p, size, __VA_ARGS__)
#define jnprintf_safe(str) SNPRINT(total, json_escape, p, size, str)

	/*
	 * Lets reuse tail of the buffer as a temp space.
	 */
	struct utsname *uname_ptr = (void *)&tail[-sizeof(struct utsname)];
	if (p >= (char *)uname_ptr)
		return -1;

	if (uname(uname_ptr) != 0) {
		pr_syserr("uname call failed, ignore");
		memset(uname_ptr, 0, sizeof(struct utsname));
	}

	/*
	 * Start filling the script. The "data" key value is
	 * filled as a separate code block for easier
	 * modifications in future.
	 */
	size = (char *)uname_ptr - p;
	snprintf_safe("{");
	snprintf_safe("\"crashdump\":{");
	snprintf_safe("\"version\":\"%d\",", crashinfo_version);
	snprintf_safe("\"data\":");

	/* The "data" key value */
	snprintf_safe("{");
	snprintf_safe("\"uname\":{");
	snprintf_safe("\"sysname\":\"");
	jnprintf_safe(uname_ptr->sysname);
	snprintf_safe("\",");
	/*
	 * nodename might contain a sensitive information, skip.
	 */
	snprintf_safe("\"release\":\"");
	jnprintf_safe(uname_ptr->release);
	snprintf_safe("\",");

	snprintf_safe("\"version\":\"");
	jnprintf_safe(uname_ptr->version);
	snprintf_safe("\",");

	snprintf_safe("\"machine\":\"");
	jnprintf_safe(uname_ptr->machine);
	snprintf_safe("\"");
	snprintf_safe("},");

	/* Extend size, because now uname_ptr is not needed. */
	size = e - p;

	/*
	 * Instance block requires uuid encoding so take it
	 * from the tail of the buffer.
	 */
	snprintf_safe("\"instance\":{");
	char *uuid_buf = &tail[-(UUID_STR_LEN+1)];
	if (p >= uuid_buf)
		return -1;
	size = uuid_buf - p;

	tt_uuid_to_string(&INSTANCE_UUID, uuid_buf);
	snprintf_safe("\"server_id\":\"%s\",", uuid_buf);
	tt_uuid_to_string(&REPLICASET_UUID, uuid_buf);
	snprintf_safe("\"cluster_id\":\"%s\",", uuid_buf);

	/* No need for uuid_buf anymore. */
	size = e - p;

	struct timespec ts;
	uint64_t uptime_ns;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
		uptime_ns = timespec_to_ns(&ts) - timestamp_mono;
	else
		uptime_ns = 0;
	snprintf_safe("\"uptime\":\"%llu\"",
		      (unsigned long long)uptime_ns / 1000000000);
	snprintf_safe("},");

	snprintf_safe("\"build\":{");
	snprintf_safe("\"version\":\"%s\",", PACKAGE_VERSION);
	snprintf_safe("\"cmake_type\":\"%s\"", BUILD_INFO);
	snprintf_safe("},");

	snprintf_safe("\"signal\":{");
	snprintf_safe("\"signo\":%d,", cinfo->signo);
	snprintf_safe("\"si_code\":%d,", cinfo->sicode);
	if (cinfo->signo == SIGSEGV) {
		if (cinfo->sicode == SEGV_MAPERR) {
			snprintf_safe("\"si_code_str\":\"%s\",",
				      "SEGV_MAPERR");
		} else if (cinfo->sicode == SEGV_ACCERR) {
			snprintf_safe("\"si_code_str\":\"%s\",",
				      "SEGV_ACCERR");
		}
		snprintf_safe("\"si_addr\":\"0x%llx\",",
			      (long long)cinfo->siaddr);
	}

#ifdef ENABLE_BACKTRACE
	snprintf_safe("\"backtrace\":\"");
	jnprintf_safe(cinfo->backtrace_buf);
	snprintf_safe("\",");
#endif

	/* 64 bytes should be enough for longest localtime */
	const int ts_size = 64;
	char *timestamp_rt_str = &tail[-ts_size];
	if (p >= timestamp_rt_str)
		return -1;
	ns_to_localtime(cinfo->timestamp_rt, timestamp_rt_str, ts_size);

	size = timestamp_rt_str - p;
	snprintf_safe("\"timestamp\":\"");
	jnprintf_safe(timestamp_rt_str);
	snprintf_safe("\"");
	snprintf_safe("}");
	snprintf_safe("}");

	/* Finalize the "data" key and the whole dump. */
	size = e - p;
	snprintf_safe("}");
	snprintf_safe("}");

	pr_debug("crash dump: %s", head);

	char *exec_argv[7] = {
		[0] = tarantool_path,
		[1] = "-e",
		/* Timeout 1 sec is taken from the feedback daemon. */
		[2] = "require('http.client').post(arg[1],arg[2],{timeout=1});"
		      "os.exit(1);",
		[3] = "-",
		[4] = feedback_host,
		[5] = head,
		[6] = NULL,
	};

	extern char **environ;
	int rc = posix_spawn(NULL, exec_argv[0], NULL, NULL, exec_argv, environ);
	if (rc != 0) {
		pr_crit("posix_spawn with "
			"exec(%s,[%s,%s,%s,%s,%s,%s,%s]) failed: %s", exec_argv[0],
			exec_argv[0], exec_argv[1], exec_argv[2], exec_argv[3],
			exec_argv[4], exec_argv[5], exec_argv[6], strerror(rc));
		return -1;
	}

	return 0;
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

#ifdef HAS_GREG
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
#endif /* HAS_GREG */

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
		if (send_crashinfo &&
		    crash_report_feedback_daemon(cinfo) != 0) {
			pr_crit("unable to send a crash report");
		}
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
