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
#include "tarantool.h"
#include "config.h"

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
#ifdef TARGET_OS_LINUX
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
#include TARANTOOL_CONFIG
#include <util.h>
#include <third_party/gopt/gopt.h>


static pid_t master_pid;
#define DEFAULT_CFG_FILENAME "tarantool.cfg"
const char *cfg_filename = DEFAULT_CFG_FILENAME;
char *cfg_filename_fullpath = NULL;
struct tbuf *cfg_out = NULL;
char *binary_filename;
struct tarantool_cfg cfg;

bool init_storage, booting = true;

extern int daemonize(int nochdir, int noclose);
void out_warning(int v, char *format, ...);

static i32
load_cfg(struct tarantool_cfg *conf, i32 check_rdonly)
{
	FILE *f;
	i32 n_accepted, n_skipped;

	tbuf_reset(cfg_out);

	if (cfg_filename_fullpath != NULL)
		f = fopen(cfg_filename_fullpath, "r");
	else
		f = fopen(cfg_filename, "r");

	if (f == NULL) {
		out_warning(0, "can't open config `%s'", cfg_filename);

		return -1;
	}

	parse_cfg_file_tarantool_cfg(conf, f, check_rdonly, &n_accepted, &n_skipped);
	fclose(f);

	if (check_cfg_tarantool_cfg(conf) != 0)
		return -1;

	if (n_accepted == 0 || n_skipped != 0)
		return -1;

	return mod_check_config(conf);
}

i32
reload_cfg(struct tbuf *out)
{
	struct tarantool_cfg new_cfg1, new_cfg2;
	i32 ret;

	// Load with checking readonly params
	if (dup_tarantool_cfg(&new_cfg1, &cfg) != 0) {
		destroy_tarantool_cfg(&new_cfg1);

		return -1;
	}
	ret = load_cfg(&new_cfg1, 1);
	if (ret == -1) {
		tbuf_append(out, cfg_out->data, cfg_out->len);

		destroy_tarantool_cfg(&new_cfg1);

		return -1;
	}
	// Load without checking readonly params
	if (fill_default_tarantool_cfg(&new_cfg2) != 0) {
		destroy_tarantool_cfg(&new_cfg2);

		return -1;
	}
	ret = load_cfg(&new_cfg2, 0);
	if (ret == -1) {
		tbuf_append(out, cfg_out->data, cfg_out->len);

		destroy_tarantool_cfg(&new_cfg1);

		return -1;
	}
	// Compare only readonly params
	char *diff = cmp_tarantool_cfg(&new_cfg1, &new_cfg2, 1);
	if (diff != NULL) {
		destroy_tarantool_cfg(&new_cfg1);
		destroy_tarantool_cfg(&new_cfg2);

		out_warning(0, "Could not accept read only '%s' option", diff);
		tbuf_append(out, cfg_out->data, cfg_out->len);

		return -1;
	}
	destroy_tarantool_cfg(&new_cfg1);

	mod_reload_config(&cfg, &new_cfg2);

	destroy_tarantool_cfg(&cfg);

	cfg = new_cfg2;

	return 0;
}

const char *
tarantool_version(void)
{
	return TARANTOOL_VERSION;
}

static double start_time;

double
tarantool_uptime(void)
{
	return ev_now() - start_time;
}

#ifdef STORAGE
int
snapshot(void *ev __attribute__((unused)), int events __attribute__((unused)))
{
	pid_t p = fork();
	if (p < 0) {
		say_syserror("fork");
		return -1;
	}
	if (p > 0) {
		wait_for_child(p);

		assert(p == fiber->cw.rpid);

		return WEXITSTATUS(fiber->cw.rstatus);
	}

	fiber->name = "dumper";
	set_proc_title("dumper (%" PRIu32 ")", getppid());
	close_all_xcpt(1, sayfd);
	snapshot_save(recovery_state, mod_snapshot);
#ifdef ENABLE_GCOV
	__gcov_flush();
#endif
	_exit(EXIT_SUCCESS);

	return 0;
}
#endif

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
#ifdef ENABLE_GCOV
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

	f = fopen(cfg.pid_file, "a+");
	if (f == NULL)
		panic_syserror("can't open pid file");

	if (fgets(buf, sizeof(buf), f) != NULL && strlen(buf) > 0) {
		pid = strtol(buf, NULL, 10);
		if (pid > 0 && kill(pid, 0) == 0)
			panic("the daemon is already running");
		else
			say_info("updating a stale pid file");
		fseeko(f, 0, SEEK_SET);
		if (ftruncate(fileno(f), 0) == -1)
			panic_syserror("ftruncate(`%s')", cfg.pid_file);
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
		panic_syserror("can't initialize slab allocator");

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
#ifdef STORAGE
	const char *cat_filename = NULL;
#endif
	const char *cfg_paramname = NULL;

	master_pid = getpid();
	stat_init();
	palloc_init();

#ifdef RESOLVE_SYMBOLS
	load_symbols(argv[0]);
#endif
	argv = init_set_proc_title(argc, argv);

	const void *opt_def =
		gopt_start(gopt_option('g', GOPT_ARG, gopt_shorts(0),
				       gopt_longs("cfg-get", "cfg_get"),
				       "=KEY", "return a value from configuration file described by KEY"),
			   gopt_option('k', 0, gopt_shorts(0),
				       gopt_longs("check-config"),
				       NULL, "Check configuration file for errors"),
			   gopt_option('c', GOPT_ARG, gopt_shorts('c'),
				       gopt_longs("config"),
				       "=FILE", "path to configuration file (default: " DEFAULT_CFG_FILENAME ")"),
#ifdef STORAGE
			   gopt_option('C', GOPT_ARG, gopt_shorts(0), gopt_longs("cat"),
				       "=FILE", "cat snapshot file to stdout in readable format and exit"),
			   gopt_option('I', 0, gopt_shorts(0),
				       gopt_longs("init-storage", "init_storage"),
				       NULL, "initialize storage (an empty snapshot file) and exit"),
#endif
			   gopt_option('v', 0, gopt_shorts('v'), gopt_longs("verbose"),
				       NULL, "increase verbosity level in log messages"),
			   gopt_option('D', 0, gopt_shorts('D'), gopt_longs("daemonize"),
				       NULL, "redirect input/output streams to a log file and run as daemon"),
			   gopt_option('h', 0, gopt_shorts('h', '?'), gopt_longs("help"),
				       NULL, "display this help and exit"),
			   gopt_option('V', 0, gopt_shorts('V'), gopt_longs("version"),
				       NULL, "print program version and exit"));

	void *opt = gopt_sort(&argc, (const char **)argv, opt_def);
	binary_filename = argv[0];

	if (gopt(opt, 'V')) {
		puts(tarantool_version());
		return 0;
	}

	if (gopt(opt, 'h')) {
		puts("Tarantool -- an efficient in-memory data store.");
		printf("Usage: %s [OPTIONS]\n", basename(argv[0]));
		puts("");
		gopt_help(opt_def);
		puts("");
		puts("Please visit project home page at http://launchpad.net/tarantool");
		puts("to see online documentation, submit bugs or contribute a patch.");
		return 0;
	}

	/* If config filename given in command line it will override the default */
	gopt_arg(opt, 'c', &cfg_filename);
	cfg.log_level += gopt(opt, 'v');

	if (argc != 1) {
		fprintf(stderr, "Can't parse command line: try --help or -h for help.\n");
		exit(EX_USAGE);
	}

	if (cfg_filename[0] != '/') {
		cfg_filename_fullpath = malloc(PATH_MAX);
		if (getcwd(cfg_filename_fullpath, PATH_MAX - strlen(cfg_filename) - 1) == NULL) {
			say_syserror("getcwd");
			exit(EX_OSERR);
		}

		strcat(cfg_filename_fullpath, "/");
		strcat(cfg_filename_fullpath, cfg_filename);
	}

	cfg_out = tbuf_alloc(eter_pool);
	assert(cfg_out);

	if (gopt(opt, 'k')) {
		if (fill_default_tarantool_cfg(&cfg) != 0 || load_cfg(&cfg, 0) != 0) {
			say_error("check_config FAILED"
				  "%.*s", cfg_out->len, (char *)cfg_out->data);

			return 1;
		}

		return 0;
	}

	if (fill_default_tarantool_cfg(&cfg) != 0 || load_cfg(&cfg, 0) != 0)
		panic("can't load config:"
		      "%.*s", cfg_out->len, (char *)cfg_out->data);

#ifdef STORAGE
	if (gopt_arg(opt, 'C', &cat_filename)) {
		initialize_minimal();
		if (access(cat_filename, R_OK) == -1) {
			say_syserror("access(\"%s\")", cat_filename);
			exit(EX_OSFILE);
		}
		return mod_cat(cat_filename);
	}

	if (gopt(opt, 'I')) {
		init_storage = true;
		initialize_minimal();
		mod_init();
		next_lsn(recovery_state, 1);
		confirm_lsn(recovery_state, 1);
		snapshot_save(recovery_state, mod_snapshot);
		exit(EXIT_SUCCESS);
	}
#endif

	if (gopt_arg(opt, 'g', &cfg_paramname)) {
		tarantool_cfg_iterator_t *i;
		char *key, *value;

		i = tarantool_cfg_iterator_init();
		while ((key = tarantool_cfg_iterator_next(i, &cfg, &value)) != NULL) {
			if (strcmp(key, cfg_paramname) == 0) {
				printf("%s\n", value);
				free(value);

				return 0;
			}

			free(value);
		}

		return 1;
	}

	if (cfg.work_dir != NULL && chdir(cfg.work_dir) == -1)
		say_syserror("can't chdir to `%s'", cfg.work_dir);

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
#ifdef TARGET_OS_LINUX
		if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0) {
			say_syserror("prctl");
			exit(EX_OSERR);
		}
#endif
	}

	if (gopt(opt, 'D'))
		daemonize(1, 1);

	if (cfg.pid_file != NULL) {
		create_pid();
		atexit(remove_pid);
	}

	say_logger_init(cfg.logger_nonblock);
	booting = false;

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
