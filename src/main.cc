/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "main.h"
#include "trivia/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <pwd.h>
#include <unistd.h>
#include <getopt.h>
#include <locale.h>
#include <libgen.h>
#include <sysexits.h>
#if defined(TARGET_OS_LINUX) && defined(HAVE_PRCTL_H)
# include <sys/prctl.h>
#endif
#include <fiber.h>
#include <coeio.h>
#include <crc32.h>
#include "memory.h"
#include <say.h>
#include <rmean.h>
#include <limits.h>
#include "trivia/util.h"
#include "tt_pthread.h"
#include "lua/init.h"
#include "box/box.h"
#include "scoped_guard.h"
#include "random.h"
#include "tt_uuid.h"
#include "iobuf.h"
#include <third_party/gopt/gopt.h>
#include "cfg.h"
#include "version.h"
#include <readline/history.h>

static pid_t master_pid = getpid();
char *script = NULL;
char *pid_file = NULL;
char *custom_proc_title;
char status[64] = "unknown";
char **main_argv;
int main_argc;
/** Signals handled after start as part of the event loop. */
static ev_signal ev_sigs[4];
static const int ev_sig_count = sizeof(ev_sigs)/sizeof(*ev_sigs);

extern const void *opt_def;

/* defined in third_party/proctitle.c */
extern "C" {
char **init_set_proc_title(int argc, char **argv);
void free_proc_title(int argc, char **argv);
void set_proc_title(const char *format, ...);
} /* extern "C" */

void
title(const char *role, const char *fmt, ...)
{
	(void) role;

	va_list ap;
	char buf[256], *bufptr = buf, *bufend = buf + sizeof(buf);
	char *statusptr = status, *statusend = status + sizeof(status);
	statusptr += snprintf(statusptr, statusend - statusptr, "%s", role);
	bufptr += snprintf(bufptr, bufend - bufptr, "%s%s", role,
			    custom_proc_title);

	if (fmt != NULL) {
		const char *s = statusptr;
		statusptr += snprintf(statusptr, statusend - statusptr, "/");
		va_start(ap, fmt);
		statusptr += vsnprintf(statusptr, statusend - statusptr,
				       fmt, ap);
		va_end(ap);
		bufptr += snprintf(bufptr, bufend - bufptr, "%s", s);
	}

	set_proc_title(buf);
}

const char *
tarantool_version(void)
{
	return PACKAGE_VERSION;
}

uint32_t
tarantool_version_id()
{
	return version_id(PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR,
			  PACKAGE_VERSION_PATCH);
}

static double start_time;

double
tarantool_uptime(void)
{
	return ev_now(loop()) - start_time;
}

/**
* Create snapshot from signal handler (SIGUSR1)
*/
static void
sig_snapshot(ev_loop * /* loop */, struct ev_signal * /* w */,
	     int /* revents */)
{
	if (snapshot_in_progress) {
		say_warn("Snapshot process is already running,"
			" the signal is ignored");
		return;
	}
	fiber_start(fiber_new("snapshot", (fiber_func)box_snapshot));
}

static void
signal_cb(ev_loop *loop, struct ev_signal *w, int revents)
{
	(void) w;
	(void) revents;

	/**
	 * If running in daemon mode, complain about possibly
	 * sudden and unexpected death.
	 * Real case: an ops A kills the server and ops B files
	 * a bug that the server suddenly died. Make such case
	 * explicit in the log.
	 */
	if (pid_file)
		say_crit("got signal %d - %s", w->signum, strsignal(w->signum));
	start_loop = false;
	/* Terminate the main event loop */
	ev_break(loop, EVBREAK_ALL);
}

/** Try to log as much as possible before dumping a core.
 *
 * Core files are not aways allowed and it takes an effort to
 * extract useful information from them.
 *
 * *Recursive invocation*
 *
 * Unless SIGSEGV is sent by kill(), Linux
 * resets the signal a default value before invoking
 * the handler.
 *
 * Despite that, as an extra precaution to avoid infinite
 * recursion, we count invocations of the handler, and
 * quietly _exit() when called for a second time.
 */
static void
sig_fatal_cb(int signo)
{
	static volatile sig_atomic_t in_cb = 0;
	int fd = STDERR_FILENO;
	struct sigaction sa;

	/* Got a signal while running the handler. */
	if (in_cb) {
		fdprintf(fd, "Fatal %d while backtracing", signo);
		goto end;
	}

	in_cb = 1;

	if (signo == SIGSEGV)
		fdprintf(fd, "Segmentation fault\n");
	else
		fdprintf(fd, "Got a fatal signal %d\n", signo);

	fdprintf(fd, "Current time: %u\n", (unsigned) time(0));
	fdprintf(fd,
		 "Please file a bug at http://github.com/tarantool/tarantool/issues\n"
		 "Attempting backtrace... Note: since the server has "
		 "already crashed, \nthis may fail as well\n");
#ifdef ENABLE_BACKTRACE
	print_backtrace();
#endif
end:
	/* Try to dump core. */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_DFL;
	sigaction(SIGABRT, &sa, NULL);

	abort();
}

static void
signal_free(void)
{
	int i;
	for (i = 0; i < ev_sig_count; i++)
		ev_signal_stop(loop(), &ev_sigs[i]);
}

/** Make sure the child has a default signal disposition. */
static void
signal_reset()
{
	for (int i = 0; i < ev_sig_count; i++)
		ev_signal_stop(loop(), &ev_sigs[i]);

	struct sigaction sa;

	/* Reset all signals to their defaults. */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_DFL;

	if (sigaction(SIGUSR1, &sa, NULL) == -1 ||
	    sigaction(SIGINT, &sa, NULL) == -1 ||
	    sigaction(SIGTERM, &sa, NULL) == -1 ||
	    sigaction(SIGHUP, &sa, NULL) == -1 ||
	    sigaction(SIGSEGV, &sa, NULL) == -1 ||
	    sigaction(SIGFPE, &sa, NULL) == -1)
		say_syserror("sigaction");

	/* Unblock any signals blocked by libev. */
	sigset_t sigset;
	sigfillset(&sigset);
	if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) == -1)
		say_syserror("sigprocmask");
}

static void
tarantool_atfork()
{
	signal_reset();
	box_atfork();
}

/**
 * Adjust the process signal mask and add handlers for signals.
 */
static void
signal_init(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));

	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGPIPE, &sa, 0) == -1)
		panic_syserror("sigaction");

	/*
	 * SA_RESETHAND resets handler action to the default
	 * one when entering handler.
	 * SA_NODEFER allows receiving the same signal during handler.
	 */
	sa.sa_flags = SA_RESETHAND | SA_NODEFER;
	sa.sa_handler = sig_fatal_cb;

	if (sigaction(SIGSEGV, &sa, 0) == -1 ||
	    sigaction(SIGFPE, &sa, 0) == -1) {
		panic_syserror("sigaction");
	}

	ev_signal_init(&ev_sigs[0], sig_snapshot, SIGUSR1);
	ev_signal_init(&ev_sigs[1], signal_cb, SIGINT);
	ev_signal_init(&ev_sigs[2], signal_cb, SIGTERM);
	ev_signal_init(&ev_sigs[3], signal_cb, SIGHUP);
	for (int i = 0; i < ev_sig_count; i++)
		ev_signal_start(loop(), &ev_sigs[i]);

	(void) tt_pthread_atfork(NULL, NULL, tarantool_atfork);
}

static void
create_pid(void)
{
	FILE *f;
	char buf[16] = { 0 };
	pid_t pid;

	pid_file = (char *) cfg_gets("pid_file");
	if (pid_file == NULL)
		return;

	pid_file = strdup(pid_file);

	f = fopen(pid_file, "a+");
	if (f == NULL)
		panic_syserror("can't open pid file");
	/*
	 * fopen() is not guaranteed to set the seek position to
	 * the beginning of file according to ANSI C (and, e.g.,
	 * on FreeBSD).
	 */
	if (fseeko(f, 0, SEEK_SET) != 0)
		panic_syserror("can't fseek to the beginning of pid file");

	if (fgets(buf, sizeof(buf), f) != NULL && strlen(buf) > 0) {
		pid = strtol(buf, NULL, 10);
		if (pid > 0 && kill(pid, 0) == 0)
			panic("the daemon is already running");
		else
			say_info("updating a stale pid file");
		if (fseeko(f, 0, SEEK_SET) != 0)
			panic_syserror("can't fseek to the beginning of pid file");
		if (ftruncate(fileno(f), 0) == -1)
			panic_syserror("ftruncate(`%s')", pid_file);
	}

	master_pid = getpid();
	fprintf(f, "%i\n", master_pid);
	fclose(f);
}

/** Run in the background. */
static void
background()
{
	/* flush buffers to avoid multiple output */
	/* https://github.com/tarantool/tarantool/issues/366 */
	fflush(stdout);
	fflush(stderr);
	switch (fork()) {
	case -1:
		goto error;
	case 0:                                     /* child */
		break;
	default:                                    /* parent */
		exit(EXIT_SUCCESS);
	}

	if (setsid() == -1)
		goto error;

	/*
	 * tell libev we've just forked, this is necessary to re-initialize
	 * kqueue on FreeBSD.
	 */
	ev_loop_fork(cord()->loop);

	/*
	 * reinit signals after fork, because fork() implicitly calls
	 * signal_reset() via pthread_atfork() hook installed by signal_init().
	 */
	signal_init();

	/* reinit coeio after fork (because libeio required it) */
	coeio_reinit();
	/*
	 * Prints to stdout on failure, so got to be done before
	 * we close it.
	 */
	create_pid();

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	return;
error:
	exit(EXIT_FAILURE);
}

extern "C" void
check_cfg()
{
	box_check_config();
}

extern "C" void
load_cfg()
{
	const char *work_dir = cfg_gets("work_dir");
	if (work_dir != NULL && chdir(work_dir) == -1)
		panic_syserror("can't chdir to `%s'", work_dir);

	const char *username = cfg_gets("username");
	if (username != NULL) {
		if (getuid() == 0 || geteuid() == 0) {
			struct passwd *pw;
			errno = 0;
			if ((pw = getpwnam(username)) == 0) {
				if (errno) {
					say_syserror("getpwnam: %s",
						     username);
				} else {
					say_error("User not found: %s",
						  username);
				}
				exit(EX_NOUSER);
			}
			if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0 || seteuid(pw->pw_uid)) {
				say_syserror("setgid/setuid");
				exit(EX_OSERR);
			}
		} else {
			say_error("can't switch to %s: i'm not root",
				  username);
		}
	}

	if (cfg_geti("coredump")) {
		struct rlimit c = { 0, 0 };
		if (getrlimit(RLIMIT_CORE, &c) < 0) {
			say_syserror("getrlimit");
			exit(EX_OSERR);
		}
		c.rlim_cur = c.rlim_max;
		if (setrlimit(RLIMIT_CORE, &c) < 0) {
			say_syserror("setrlimit");
			exit(EX_OSERR);
		}
#if defined(TARGET_OS_LINUX) && defined(HAVE_PRCTL_H)
		if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0) {
			say_syserror("prctl");
			exit(EX_OSERR);
		}
#endif
	}

	if (cfg_geti("background")) {
		if (cfg_gets("logger") == NULL) {
			say_crit("'background' requires 'logger' configuration option to be set");
			exit(EXIT_FAILURE);
		}
		if (cfg_gets("pid_file") == NULL) {
			say_crit("'background' requires 'pid_file' configuration option to be set");
			exit(EXIT_FAILURE);
		}
		background();
	} else {
		create_pid();
	}

	const char *proc_title = cfg_gets("custom_proc_title");
	/* init process title - used for logging */
	if (proc_title == NULL) {
		custom_proc_title = (char *) malloc(1);
		custom_proc_title[0] = '\0';
	} else {
		custom_proc_title = (char *) malloc(strlen(proc_title) + 2);
		strcpy(custom_proc_title, "@");
		strcat(custom_proc_title, proc_title);
	}
	say_logger_init(cfg_gets("logger"),
			cfg_geti("log_level"),
			cfg_geti("logger_nonblock"),
			cfg_geti("background"));

	say_crit("version %s", tarantool_version());
	say_crit("log level %i", cfg_geti("log_level"));
	box_load_cfg();
}

void
tarantool_free(void)
{
	/* Do nothing in a fork. */
	if (getpid() != master_pid)
		return;
	signal_free();
	tarantool_lua_free();
	box_free();

	if (script)
		free(script);
	else if (history) {
		write_history(history);
		clear_history();
		free(history);
	}
	free_proc_title(main_argc, main_argv);

	/* unlink pidfile. */
	if (pid_file != NULL) {
		unlink(pid_file);
		free(pid_file);
	}

	fiber_free();
	memory_free();
	random_free();
#ifdef ENABLE_GCOV
	__gcov_flush();
#endif
#ifdef HAVE_BFD
	symbols_free();
#endif
	/* A hack for cc/ld, see ffisyms.c */
	if (time(NULL) == 0) {
		/* never executed */
		extern void *ffi_symbols[];
		ssize_t res = write(0, ffi_symbols, 0);
		(void) res;
	}
}

int
main(int argc, char **argv)
{
#ifndef HAVE_LIBC_STACK_END
	/*
	 * GNU libc provides a way to get at the top of the stack. This
	 * is, of course, not-standard and doesn't work on non-GNU
	 * systems, such as FreeBSD. But as far as we're concerned, argv
	 * is at the top of the main thread's stack, so save the address
	 * of it.
	 */
	__libc_stack_end = (void*) &argv;
#endif
	start_time = ev_time();
	/* set locale to make iswXXXX function work */
	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL &&
	    setlocale(LC_CTYPE, "en_US.UTF-8") == NULL &&
	    setlocale(LC_CTYPE, "en_US.utf8") == NULL)
		fprintf(stderr, "Failed to set locale to C.UTF-8\n");

	if (argc > 1 && access(argv[1], R_OK) != 0) {
		if (argc == 2 && argv[1][0] != '-') {
			/*
			 * Somebody made a mistake in the file
			 * name. Be nice: open the file to set
			 * errno.
			 */
			int fd = open(argv[1], O_RDONLY);
			int save_errno = errno;
			if (fd >= 0)
				close(fd);
			printf("Can't open script %s: %s\n", argv[1], strerror(save_errno));
			return save_errno;
		}
		void *opt = gopt_sort(&argc, (const char **)argv, opt_def);
		if (gopt(opt, 'V')) {
			printf("Tarantool %s\n", tarantool_version());
			printf("Target: %s\n", BUILD_INFO);
			printf("Build options: %s\n", BUILD_OPTIONS);
			printf("Compiler: %s\n", COMPILER_INFO);
			printf("C_FLAGS:%s\n", TARANTOOL_C_FLAGS);
			printf("CXX_FLAGS:%s\n", TARANTOOL_CXX_FLAGS);
			return 0;
		}

		if (gopt(opt, 'h') || argc == 1) {
			puts("Tarantool - a Lua application server");
			puts("");
			printf("Usage: %s script.lua [OPTIONS]\n",
			       basename(argv[0]));
			puts("");
			puts("All command line options are passed to the interpreted script.");
			puts("When no script name is provided, the server responds to:");
			gopt_help(opt_def);
			puts("");
			puts("Please visit project home page at http://tarantool.org");
			puts("to see online documentation, submit bugs or contribute a patch.");
			return 0;
		}
		fprintf(stderr, "Can't parse command line: try --help or -h for help.\n");
		exit(EX_USAGE);
	}
#ifdef HAVE_BFD
	symbols_load(argv[0]);
#endif
	/*
	 * Support only #!/usr/bin/tarantol but not
	 * #!/usr/bin/tarantool -a -b because:
	 * - not all shells support it,
	 * - those shells that do support it, do not
	 *   split multiple options, so "-a -b" comes as
	 *   a single value in argv[1].
	 * - in case one uses #!/usr/bin/env tarantool
	 *   such options (in script line) don't work
	 */

	char *tarantool_bin = find_path(argv[0]);
	if (!tarantool_bin)
		tarantool_bin = argv[0];
	if (argc > 1) {
		argv++;
		argc--;
		script = abspath(argv[0]);
	} else if (isatty(STDIN_FILENO)) {
		/* load history file */
		char *home = getenv("HOME");
		history = (char *) malloc(PATH_MAX);
		snprintf(history, PATH_MAX, "%s/%s", home,
			 ".tarantool_history");
		read_history(history);
	}

	random_init();
	say_init(argv[0]);

	crc32_init();
	memory_init();

	argv = init_set_proc_title(argc, argv);
	main_argc = argc;
	main_argv = argv;

	fiber_init();
	/* Init iobuf library with default readahead */
	iobuf_init();
	coeio_init();
	signal_init();
	tarantool_lua_init(tarantool_bin, main_argc, main_argv);

	/* main core cleanup routine */
	atexit(tarantool_free);

	try {
		int events = ev_activecnt(loop());
		/*
		 * Load user init script.  The script should have access
		 * to Tarantool Lua API (box.cfg, box.fiber, etc...) that
		 * is why script must run only after the server was fully
		 * initialized.
		 */
		tarantool_lua_run_script(script, main_argc, main_argv);
		/*
		 * Start event loop after executing Lua script if signal_cb()
		 * wasn't triggered and there is some new events. Initial value
		 * of start_loop can be set to false by signal_cb().
		 */
		start_loop = start_loop && ev_activecnt(loop()) > events;
		region_free(&fiber()->gc);
		if (start_loop) {
			say_crit("entering the event loop");
			ev_now_update(loop());
			ev_run(loop(), 0);
		}
	} catch (Exception *e) {
		e->log();
		panic("%s", "fatal error, exiting the event loop");
	}

	if (start_loop)
		say_crit("exiting the event loop");
	/* freeing resources */
	return 0;
}
