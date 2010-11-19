/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

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
#include <libgen.h>
#include <sysexits.h>
#ifdef Linux
# include <sys/prctl.h>
#endif
#include <admin.h>
#include <fiber.h>
#include <iproto.h>
#include <log_io.h>
#include <palloc.h>
#include <salloc.h>
#include <say.h>
#include <stat.h>
#include <tarantool.h>
#include <util.h>
#include <tarantool_version.h>

static pid_t master_pid;
char *cfg_filename = "tarantool.cfg";
struct tarantool_cfg cfg;

bool init_storage;

enum tarantool_role role = def;

extern int daemonize(int nochdir, int noclose);

const char *
tarantool_version(void)
{
	return tarantool_version_string;
}

static double start_time;
double
tarantool_uptime(void)
{
	return ev_now() - start_time;
}

#ifdef STORAGE
void
snapshot(void *ev __unused__, int events __unused__)
{
	pid_t p = fork();
	if (p < 0) {
		say_syserror("fork");
		return;
	}
	if (p > 0)
		return;

	fiber->name = "dumper";
	set_proc_title("dumper (%" PRIu32 ")", getppid());
	close_all_xcpt(1, sayfd);
	snapshot_save(recovery_state, mod_snapshot);
#ifdef COVERAGE
	__gcov_flush();
#endif
	_exit(EXIT_SUCCESS);
}
#endif

static void
sig_child(int signal __unused__)
{
	int child_status;
	/* TODO: watch child status & destroy corresponding fibers */
	while (waitpid(-1, &child_status, WNOHANG) > 0) ;
}

static void
sig_int(int signal)
{
	say_info("SIGINT or SIGTERM recieved, terminating");

	if (recovery_state != NULL) {
		struct child *writer = recovery_state->wal_writer;
		if (writer && writer->out && writer->out->fd > 0) {
			close(writer->out->fd);
			usleep(1000);
		}
	}
#ifdef COVERAGE
	__gcov_flush();
#endif

	if (master_pid == getpid()) {
		kill(0, signal);
		exit(EXIT_SUCCESS);
	} else
		_exit(EXIT_SUCCESS);
}

static void
signal_init(void)
{
	struct sigaction sa;

	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGPIPE, &sa, 0) == -1)
		goto error;

	sa.sa_handler = sig_child;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		goto error;

	sa.sa_handler = sig_int;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, 0) == -1)
		goto error;

	sa.sa_handler = sig_int;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGTERM, &sa, 0) == -1)
		goto error;

	sa.sa_handler = sig_int;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGHUP, &sa, 0) == -1)
		goto error;

	return;
      error:
	say_syserror("sigaction");
	exit(EX_OSERR);
}

static void
create_pid(void)
{
	FILE *f;
	char buf[16] = { 0 };
	pid_t pid;

	if (cfg.pid_file == NULL)
		panic("no pid file is specified in config");

	f = fopen(cfg.pid_file, "a+");
	if (f == NULL)
		panic_syserror("can't open pid file");

	if (fgets(buf, sizeof(buf), f) != NULL && strlen(buf) > 0) {
		pid = strtol(buf, NULL, 10);
		if (pid > 0 && kill(pid, 0) == 0)
			panic("deamon is running");
		else
			say_info("updating stale pid file");
		fseeko(f, 0, SEEK_SET);
		if (ftruncate(fileno(f), 0) == -1)
			panic_syserror("ftruncate");
	}

	fprintf(f, "%i\n", getpid());
	fclose(f);
}

static void
remove_pid(void)
{
	unlink(cfg.pid_file);
}

static void
initialize(double slab_alloc_arena, int slab_alloc_minimal, double slab_alloc_factor)
{

	if (!salloc_init(slab_alloc_arena * (1 << 30), slab_alloc_minimal, slab_alloc_factor))
		panic("can't initialize slab allocator");

	fiber_init();
}

static void
initialize_minimal()
{
	initialize(0.1, 4, 2);
}

int
main(int argc, char **argv)
{
	int c, verbose = 0;
	char *cat_filename = NULL;
	bool be_daemon = false;
	int n_accepted, n_skipped;
	FILE *f;

#if CORO_ASM
	save_rbp(&main_stack_frame);
#endif
	master_pid = getpid();
	stat_init();
	palloc_init();

	const char *short_opt = "c:pvVD";
	const struct option long_opt[] = {
		{.name = "config",
		 .has_arg = 1,
		 .flag = NULL,
		 .val = 'c'},
#ifdef STORAGE
		{.name = "cat",
		 .has_arg = 1,
		 .flag = NULL,
		 .val = 'C'},
		{.name = "init_storage",
		 .has_arg = 0,
		 .flag = NULL,
		 .val = 'I'},
#endif
		{.name = "create_pid",
		 .has_arg = 0,
		 .flag = NULL,
		 .val = 'p'},
		{.name = "verbose",
		 .has_arg = 0,
		 .flag = NULL,
		 .val = 'v'},
		{.name = "version",
		 .has_arg = 0,
		 .flag = NULL,
		 .val = 'V'},
		{.name = "daemonize",
		 .has_arg = 0,
		 .flag = NULL,
		 .val = 'D'},
		{.name = NULL,
		 .has_arg = 0,
		 .flag = NULL,
		 .val = 0}
	};

	while ((c = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1) {
		switch (c) {
		case 'V':
			puts(tarantool_version());
			return 0;
		case 'c':
			if (optarg == NULL)
				panic("no arg given");
			cfg_filename = strdup(optarg);
			break;
		case 'C':
			if (optarg == NULL)
				panic("no arg given");
			cat_filename = strdup(optarg);
			role = cat;
		case 'v':
			verbose++;
			break;
		case 'p':
			cfg.pid_file = "tarantool.pid";
			break;
		case 'D':
			be_daemon = true;
			break;
		case 'I':
			init_storage = true;
			break;
		}
	}

	if (argc != optind)
		panic("not all args were parsed");

	if (role == usage) {
		fprintf(stderr, "usage:\n");
		fprintf(stderr, "	-c, --config=filename\n");
#ifdef STORAGE
		fprintf(stderr, "	--cat=filename\n");
		fprintf(stderr, "	--init_storage\n");
#endif
		fprintf(stderr, "	-V, --version\n");
		fprintf(stderr, "	-p, --create_pid\n");
		fprintf(stderr, "	-v, --verbose\n");
		fprintf(stderr, "	-D, --daemonize\n");

		return 0;
	}

	if (cfg_filename[0] != '/') {
		char *full_path = malloc(PATH_MAX);
		if (getcwd(full_path, PATH_MAX - strlen(cfg_filename) - 1) == NULL) {
			say_syserror("getcwd");
			exit(EX_OSERR);
		}

		strcat(full_path, "/");
		strcat(full_path, cfg_filename);
		cfg_filename = full_path;
	}

	f = fopen(cfg_filename, "r");
	if (f == NULL)
		panic("can't open config `%s'", cfg_filename);

	fill_default_tarantool_cfg(&cfg);
	parse_cfg_file_tarantool_cfg(&cfg, f, 0, &n_accepted, &n_skipped);
	check_cfg_tarantool_cfg(&cfg);
	fclose(f);

#ifdef STORAGE
	if (role == cat) {
		initialize_minimal();
		if (access(cat_filename, R_OK) == -1) {
			say_syserror("access(\"%s\")", cat_filename);
			exit(EX_OSFILE);
		}
		return mod_cat(cat_filename);
	}
#endif

	cfg.log_level += verbose;

	if (cfg.work_dir != NULL && chdir(cfg.work_dir) == -1)
		say_syserror("can't chdir to `%s'", cfg.work_dir);

	say_logger_init(cfg.logger_nonblock);

	if (cfg.username != NULL) {
		if (getuid() == 0 || geteuid() == 0) {
			struct passwd *pw;
			if ((pw = getpwnam(cfg.username)) == 0) {
				say_syserror("getpwnam: %s", cfg.username);
				exit(EX_NOUSER);
			}
			if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0 || seteuid(pw->pw_uid)) {
				say_syserror("setgit/setuid");
				exit(EX_OSERR);
			}
		} else {
			say_error("can't swith to %s: i'm not root", cfg.username);
		}
	}

	if (cfg.coredump) {
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
#ifdef Linux
		if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0) {
			say_syserror("prctl");
			exit(EX_OSERR);
		}
#endif
	}
#ifdef STORAGE
	if (init_storage) {
		initialize_minimal();
		mod_init();
		next_lsn(recovery_state, 1);
		confirm_lsn(recovery_state, 1);
		snapshot_save(recovery_state, mod_snapshot);
		exit(EXIT_SUCCESS);
	}
#endif
	if (be_daemon)
		daemonize(1, 1);

	if (cfg.pid_file != NULL) {
		create_pid();
		atexit(remove_pid);
	}

	argv = init_set_proc_title(argc, argv);

#if defined(UTILITY)
	initialize_minimal();
	signal_init();
	mod_init();
#elif defined(STORAGE)
	ev_signal *ev_sig;
	ev_sig = palloc(eter_pool, sizeof(ev_signal));
	ev_signal_init(ev_sig, (void *)snapshot, SIGUSR1);
	ev_signal_start(ev_sig);

	initialize(cfg.slab_alloc_arena, cfg.slab_alloc_minimal, cfg.slab_alloc_factor);
	signal_init();
	ev_default_loop(0);

	mod_init();
	admin_init();
	prelease(fiber->pool);
	say_crit("log level %i", cfg.log_level);
	say_crit("entering event loop");
	if (cfg.io_collect_interval > 0)
		ev_set_io_collect_interval(cfg.io_collect_interval);
	ev_now_update();
	start_time = ev_now();
	ev_loop(0);
	say_crit("exiting loop");
#else
#error UTILITY or STORAGE must be defined
#endif
	return 0;
}
