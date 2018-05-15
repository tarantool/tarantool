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
#include "main.h"
#include "trivia/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <grp.h>
#include <getopt.h>
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
#include "fiber.h"
#include "cbus.h"
#include "coio_task.h"
#include <crc32.h>
#include "memory.h"
#include <say.h>
#include <rmean.h>
#include <limits.h>
#include "coll.h"
#include "trivia/util.h"
#include "backtrace.h"
#include "tt_pthread.h"
#include "lua/init.h"
#include "box/box.h"
#include "box/error.h"
#include "scoped_guard.h"
#include "random.h"
#include "tt_uuid.h"
#include "cfg.h"
#include "version.h"
#include <readline/readline.h>
#include "title.h"
#include <libutil.h>
#include "box/lua/init.h" /* box_lua_init() */
#include "box/session.h"
#include "systemd.h"

static pid_t master_pid = getpid();
static struct pidfh *pid_file_handle;
static char *script = NULL;
static char *pid_file = NULL;
static char **main_argv;
static int main_argc;
/** Signals handled after start as part of the event loop. */
static ev_signal ev_sigs[6];
static const int ev_sig_count = sizeof(ev_sigs)/sizeof(*ev_sigs);

static double start_time;

double
tarantool_uptime(void)
{
	return ev_monotonic_now(loop()) - start_time;
}

/**
* Create a checkpoint from signal handler (SIGUSR1)
*/
static void
sig_checkpoint(ev_loop * /* loop */, struct ev_signal * /* w */,
	     int /* revents */)
{
	if (box_checkpoint_is_in_progress) {
		say_warn("Checkpoint is already in progress,"
			" the signal is ignored");
		return;
	}
	fiber_start(fiber_new_xc("checkpoint", (fiber_func)box_checkpoint));
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

static void
signal_sigwinch_cb(ev_loop *loop, struct ev_signal *w, int revents)
{
	(void) loop;
	(void) w;
	(void) revents;
	if (rl_instream)
		rl_resize_terminal();
}

#if defined(__linux__) && defined(__amd64)

inline void
dump_x86_64_register(const char *reg_name, unsigned long long val)
{
	fprintf(stderr, "  %-9s0x%-17llx%lld\n", reg_name, val, val);
}

void
dump_x86_64_registers(ucontext_t *uc)
{
	dump_x86_64_register("rax", uc->uc_mcontext.gregs[REG_RAX]);
	dump_x86_64_register("rbx", uc->uc_mcontext.gregs[REG_RBX]);
	dump_x86_64_register("rcx", uc->uc_mcontext.gregs[REG_RCX]);
	dump_x86_64_register("rdx", uc->uc_mcontext.gregs[REG_RDX]);
	dump_x86_64_register("rsi", uc->uc_mcontext.gregs[REG_RSI]);
	dump_x86_64_register("rdi", uc->uc_mcontext.gregs[REG_RDI]);
	dump_x86_64_register("rsp", uc->uc_mcontext.gregs[REG_RSP]);
	dump_x86_64_register("rbp", uc->uc_mcontext.gregs[REG_RBP]);
	dump_x86_64_register("r8", uc->uc_mcontext.gregs[REG_R8]);
	dump_x86_64_register("r9", uc->uc_mcontext.gregs[REG_R9]);
	dump_x86_64_register("r10", uc->uc_mcontext.gregs[REG_R10]);
	dump_x86_64_register("r11", uc->uc_mcontext.gregs[REG_R11]);
	dump_x86_64_register("r12", uc->uc_mcontext.gregs[REG_R12]);
	dump_x86_64_register("r13", uc->uc_mcontext.gregs[REG_R13]);
	dump_x86_64_register("r14", uc->uc_mcontext.gregs[REG_R14]);
	dump_x86_64_register("r15", uc->uc_mcontext.gregs[REG_R15]);
	dump_x86_64_register("rip", uc->uc_mcontext.gregs[REG_RIP]);
	dump_x86_64_register("eflags", uc->uc_mcontext.gregs[REG_EFL]);
	dump_x86_64_register("cs", (uc->uc_mcontext.gregs[REG_CSGSFS] >> 0) & 0xffff);
	dump_x86_64_register("gs", (uc->uc_mcontext.gregs[REG_CSGSFS] >> 16) & 0xffff);
	dump_x86_64_register("fs", (uc->uc_mcontext.gregs[REG_CSGSFS] >> 32) & 0xffff);
	dump_x86_64_register("cr2", uc->uc_mcontext.gregs[REG_CR2]);
	dump_x86_64_register("err", uc->uc_mcontext.gregs[REG_ERR]);
	dump_x86_64_register("oldmask", uc->uc_mcontext.gregs[REG_OLDMASK]);
	dump_x86_64_register("trapno", uc->uc_mcontext.gregs[REG_TRAPNO]);
}

#endif /* defined(__linux__) && defined(__amd64) */

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
sig_fatal_cb(int signo, siginfo_t *siginfo, void *context)
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

	if (signo == SIGSEGV) {
		fdprintf(fd, "Segmentation fault\n");
		const char *signal_code_repr = 0;
		switch (siginfo->si_code) {
		case SEGV_MAPERR:
			signal_code_repr = "SEGV_MAPERR";
			break;
		case SEGV_ACCERR:
			signal_code_repr = "SEGV_ACCERR";
			break;
		}
		if (signal_code_repr)
			fdprintf(fd, "  code: %s\n", signal_code_repr);
		else
			fdprintf(fd, "  code: %d\n", siginfo->si_code);
		/*
		 * fprintf is used insted of fdprintf, because
		 * fdprintf does not understand %p
		 */
		fprintf(stderr, "  addr: %p\n", siginfo->si_addr);
	} else
		fdprintf(fd, "Got a fatal signal %d\n", signo);
	fprintf(stderr, "  context: %p\n", context);
	fprintf(stderr, "  siginfo: %p\n", siginfo);

#if defined(__linux__) && defined(__amd64)
	dump_x86_64_registers((ucontext_t *)context);
#endif

	fdprintf(fd, "Current time: %u\n", (unsigned) time(0));
	fdprintf(fd,
		 "Please file a bug at http://github.com/tarantool/tarantool/issues\n");

#ifdef ENABLE_BACKTRACE
	fdprintf(fd, "Attempting backtrace... Note: since the server has "
		 "already crashed, \nthis may fail as well\n");
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
	    sigaction(SIGWINCH, &sa, NULL) == -1 ||
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
	sa.sa_flags = SA_RESETHAND | SA_NODEFER | SA_SIGINFO;
	sa.sa_sigaction = sig_fatal_cb;

	if (sigaction(SIGSEGV, &sa, 0) == -1 ||
	    sigaction(SIGFPE, &sa, 0) == -1) {
		panic_syserror("sigaction");
	}

	ev_signal_init(&ev_sigs[0], sig_checkpoint, SIGUSR1);
	ev_signal_init(&ev_sigs[1], signal_cb, SIGINT);
	ev_signal_init(&ev_sigs[2], signal_cb, SIGTERM);
	ev_signal_init(&ev_sigs[3], signal_cb, SIGHUP);
	ev_signal_init(&ev_sigs[4], signal_sigwinch_cb, SIGWINCH);
	ev_signal_init(&ev_sigs[5], say_logrotate, SIGHUP);
	for (int i = 0; i < ev_sig_count; i++)
		ev_signal_start(loop(), &ev_sigs[i]);

	(void) tt_pthread_atfork(NULL, NULL, tarantool_atfork);
}

/** Run in the background. */
static void
daemonize()
{
	pid_t pid;
	int fd;

	/* flush buffers to avoid multiple output */
	/* https://github.com/tarantool/tarantool/issues/366 */
	fflush(stdin);
	fflush(stdout);
	fflush(stderr);
	pid = fork();
	switch (pid) {
	case -1:
		goto error;
	case 0:                                     /* child */
		master_pid = getpid();
		break;
	default:                                    /* parent */
		/* Tell systemd about new main program using */
		errno = 0;
		master_pid = pid;
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

	/* redirect stdin; stdout and stderr handled in say_logger_init */
	fd = open("/dev/null", O_RDONLY);
	if (fd < 0)
		goto error;
	dup2(fd, STDIN_FILENO);
	close(fd);

	return;
error:
	exit(EXIT_FAILURE);
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
			if (setgid(pw->pw_gid) < 0 || setgroups(0, NULL) < 0 ||
			    setuid(pw->pw_uid) < 0 || seteuid(pw->pw_uid)) {
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

	int background = cfg_geti("background");
	const char *log = cfg_gets("log");
	const char *log_format = cfg_gets("log_format");
	pid_file = (char *)cfg_gets("pid_file");
	if (pid_file != NULL) {
		pid_file = abspath(pid_file);
		if (pid_file == NULL)
			panic("out of memory");
	}

	if (background) {
		if (log == NULL) {
			say_crit(
				"'background' requires "
				"'log' configuration option to be set");
			exit(EXIT_FAILURE);
		}
		if (pid_file == NULL) {
			say_crit(
				"'background' requires "
				"'pid_file' configuration option to be set");
			exit(EXIT_FAILURE);
		}
	}

	/*
	 * pid file check must happen before logger init in order for the
	 * error message to show in stderr
	 */
	if (pid_file != NULL) {
		pid_t other_pid = -1;
		pid_file_handle = pidfile_open(pid_file, 0644, &other_pid);
		if (pid_file_handle == NULL) {
			if (errno == EEXIST) {
				say_crit(
					"the daemon is already running: PID %d",
					(int)other_pid);
			} else {
				say_syserror(
					"failed to create pid file '%s'",
					pid_file);
			}
			exit(EXIT_FAILURE);
		}
	}

	/*
	 * logger init must happen before daemonising in order for the error
	 * to show and for the process to exit with a failure status
	 */
	say_logger_init(log,
			cfg_geti("log_level"),
			cfg_geti("log_nonblock"),
			log_format,
			background);
	systemd_init();

	if (background)
		daemonize();

	/*
	 * after (optional) daemonising to avoid confusing messages with
	 * different pids
	 */
	say_crit("%s %s", tarantool_package(), tarantool_version());
	say_crit("log level %i", cfg_geti("log_level"));

	if (pid_file_handle != NULL) {
		if (pidfile_write(pid_file_handle) == -1)
			say_syserror("failed to update pid file '%s'", pid_file);
	}

	title_set_custom(cfg_gets("custom_proc_title"));
	title_update();
	box_cfg();
}

void
tarantool_free(void)
{
	/*
	 * Do nothing in a fork.
	 * Note: technically we should do pidfile_close(), however since our
	 * forks do exec immediately we can get away without it, thanks to
	 * the magic O_CLOEXEC
	 */
	if (getpid() != master_pid)
		return;

	/*
	 * It's better to do nothing and keep xlogs opened when
	 * we are called by exit() from a non-main thread.
	 */
	if (!cord_is_main())
		return;

	/* Shutdown worker pool. Waits until threads terminate. */
	coio_shutdown();

	box_free();

	title_free(main_argc, main_argv);

	/* unlink pidfile. */
	if (pid_file_handle != NULL && pidfile_remove(pid_file_handle) == -1)
		say_syserror("failed to remove pid file '%s'", pid_file);
	free(pid_file);
	signal_free();
#ifdef ENABLE_GCOV
	__gcov_flush();
#endif
	/* tarantool_lua_free() was formerly reponsible for terminal reset,
	 * but it is no longer called
	 */
	if (isatty(STDIN_FILENO)) {
		/*
		 * Restore terminal state. Doesn't hurt if exiting not
		 * due to a signal.
		 */
		rl_cleanup_after_signal();
	}
	cbus_free();
#if 0
	/*
	 * This doesn't work reliably since things
	 * are too interconnected.
	 */
	tarantool_lua_free();
	session_free();
	user_cache_free();
	fiber_free();
	memory_free();
	random_free();
#endif
	coll_free();
	systemd_free();
	say_logger_free();
}

static void
print_version(void)
{
	printf("%s %s\n", tarantool_package(), tarantool_version());
	printf("Target: %s\n", BUILD_INFO);
	printf("Build options: %s\n", BUILD_OPTIONS);
	printf("Compiler: %s\n", COMPILER_INFO);
	printf("C_FLAGS:%s\n", TARANTOOL_C_FLAGS);
	printf("CXX_FLAGS:%s\n", TARANTOOL_CXX_FLAGS);
}

static void
print_help(const char *program)
{
	puts("Tarantool - a Lua application server");
	puts("");
	printf("Usage: %s script.lua [OPTIONS] [SCRIPT [ARGS]]\n", program);
	puts("");
	puts("All command line options are passed to the interpreted script.");
	puts("When no script name is provided, the server responds to:");
	puts("  -h, --help\t\t\tdisplay this help and exit");
	puts("  -v, --version\t\t\tprint program version and exit");
	puts("  -e EXPR\t\t\texecute string 'EXPR'");
	puts("  -l NAME\t\t\trequire library 'NAME'");
	puts("  -i\t\t\t\tenter interactive mode after executing 'SCRIPT'");
	puts("  --\t\t\t\tstop handling options");
	puts("  -\t\t\t\texecute stdin and stop handling options");
	puts("");
	puts("Please visit project home page at http://tarantool.org");
	puts("to see online documentation, submit bugs or contribute a patch.");
}

int
main(int argc, char **argv)
{
	/* set locale to make iswXXXX function work */
	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL &&
	    setlocale(LC_CTYPE, "en_US.UTF-8") == NULL &&
	    setlocale(LC_CTYPE, "en_US.utf8") == NULL)
		fprintf(stderr, "Failed to set locale to C.UTF-8\n");
	fpconv_check();

	/* Enter interactive mode after executing 'script' */
	bool interactive = false;
	/* Lua interpeter options, e.g. -e and -l */
	int optc = 0;
	char **optv = NULL;
	auto guard = make_scoped_guard([=]{ if (optc) free(optv); });

	static struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'v'},
		{NULL, 0, 0, 0},
	};
	static const char *opts = "+hVvie:l:";

	int ch;
	while ((ch = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
		switch (ch) {
		case 'V':
		case 'v':
			print_version();
			return 0;
		case 'h':
			print_help(basename(argv[0]));
			return 0;
		case 'i':
			/* Force interactive mode */
			interactive = true;
			break;
		case 'l':
		case 'e':
			/* Save Lua interepter options to optv as is */
			if (optc == 0) {
				optv = (char **) calloc(argc, sizeof(char *));
				if (optv == NULL)
					panic_syserror("No enough memory for arguments");
			}
			/*
			 * The variable optind is the index of the next
			 * element to be processed in argv.
			 */
			optv[optc++] = argv[optind - 2];
			optv[optc++] = argv[optind - 1];
			break;
		default:
			/* "invalid option" is printed by getopt */
			return EX_USAGE;
		}
	}

	/* Shift arguments */
	argc = 1 + (argc - optind);
	for (int i = 1; i < argc; i++)
		argv[i] = argv[optind + i - 1];

	if (argc > 1 && strcmp(argv[1], "-") && access(argv[1], R_OK) != 0) {
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

	argv = title_init(argc, argv);
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
		script = argv[0];
		title_set_script_name(argv[0]);
	}

	random_init();

	crc32_init();
	memory_init();

	main_argc = argc;
	main_argv = argv;

	exception_init();

	fiber_init(fiber_cxx_invoke);
	coio_init();
	coio_enable();
	signal_init();
	cbus_init();
	coll_init();
	tarantool_lua_init(tarantool_bin, main_argc, main_argv);

	start_time = ev_monotonic_time();

	try {
		box_init();
		box_lua_init(tarantool_L);

		/* main core cleanup routine */
		atexit(tarantool_free);

		if (!loop())
			panic("%s", "can't init event loop");

		int events = ev_activecnt(loop());
		/*
		 * Load user init script.  The script should have access
		 * to Tarantool Lua API (box.cfg, box.fiber, etc...) that
		 * is why script must run only after the server was fully
		 * initialized.
		 */
		tarantool_lua_run_script(script, interactive, optc, optv,
					 main_argc, main_argv);
		/*
		 * Start event loop after executing Lua script if signal_cb()
		 * wasn't triggered and there is some new events. Initial value
		 * of start_loop can be set to false by signal_cb().
		 */
		start_loop = start_loop && ev_activecnt(loop()) > events;
		region_free(&fiber()->gc);
		if (start_loop) {
			say_crit("entering the event loop");
			systemd_snotify("READY=1");
			ev_now_update(loop());
			ev_run(loop(), 0);
		}
	} catch (struct error *e) {
		error_log(e);
		systemd_snotify("STATUS=Failed to startup: %s",
				box_error_message(e));
		panic("%s", "fatal error, exiting the event loop");
	} catch (...) {
		/* This can only happen in case of a server bug. */
		panic("unknown exception");
	}

	if (start_loop)
		say_crit("exiting the event loop");
	/* freeing resources */
	return 0;
}
