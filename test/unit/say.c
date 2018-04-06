#include <string.h>
#include <fiber.h>
#include <memory.h>
#include "unit.h"
#include "say.h"
#include <pthread.h>
#include <errinj.h>
#include <coio_task.h>
#include <sys/socket.h>
#include <sys/un.h>

int
parse_logger_type(const char *input)
{
	enum say_logger_type type;
	int rc = say_parse_logger_type(&input, &type);

	if (rc == 0)
		switch (type) {
		case SAY_LOGGER_BOOT:
			note("type: boot"); break;
		case SAY_LOGGER_STDERR:
			note("type: stderr"); break;
		case SAY_LOGGER_FILE:
			note("type: file"); break;
		case SAY_LOGGER_PIPE:
			note("type: pipe"); break;
		case SAY_LOGGER_SYSLOG:
			note("type: syslog"); break;
		}

	note("next: %s", input);
	return rc;
}

int
parse_syslog_opts(const char *input)
{
	struct say_syslog_opts opts;
	if (say_parse_syslog_opts(input, &opts) == -1) {
		return -1;
	}
	if (opts.identity)
		note("identity: %s", opts.identity);
	if (opts.facility)
		note("facility: %i", opts.facility);
	say_free_syslog_opts(&opts);
	return 0;
}

static int
format_func_custom(struct log *log, char *buf, int len, int level,
		   const char *filename, int line, const char *error,
		   const char *format, va_list ap)
{
	int total = 0;
	(void) log;
	(void) level;
	(void) filename;
	(void) line;
	(void) error;
	SNPRINT(total, snprintf, buf, len, "\"msg\" = \"");
	SNPRINT(total, vsnprintf, buf, len, format, ap);
	SNPRINT(total, snprintf, buf, len, "\"\n");
	return total;
}

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_sync = PTHREAD_COND_INITIALIZER;

bool is_raised = false;
int created_logs = 0;
const char *tmp_dir;

struct create_log {
	struct log logger;
	int id;
};

static void *
dummy_log(void *arg)
{
	struct create_log *create_log = (struct create_log *) arg;

	char tmp_filename[30];
	sprintf(tmp_filename, "%s/%i.log", tmp_dir, create_log->id);
	tt_pthread_mutex_lock(&mutex);
	log_create(&create_log->logger, tmp_filename, false);

	/* signal that log is created */
	created_logs++;
	tt_pthread_cond_signal(&cond_sync);

	/* wait until rotate signal is raised */
	while (!is_raised)
		tt_pthread_cond_wait(&cond, &mutex);
	created_logs--;
	if (created_logs == 0)
		pthread_cond_signal(&cond_sync);
	tt_pthread_mutex_unlock(&mutex);
	return NULL;
}

static void
test_log_rotate()
{
	char template[] = "/tmp/tmpdir.XXXXXX";
	tmp_dir = mkdtemp(template);
	const int NUMBER_LOGGERS = 10;
	struct create_log *loggers = (struct create_log *) calloc(NUMBER_LOGGERS,
						  sizeof(struct create_log));
	if (loggers == NULL) {
		return;
	}
	int running = 0;
	for (int i = 0; i < NUMBER_LOGGERS; i++) {
		pthread_t thread;
		loggers[i].id = i;
		if (tt_pthread_create(&thread, NULL, dummy_log,
				   (void *) &loggers[i]) >= 0)
			running++;
	}
	tt_pthread_mutex_lock(&mutex);
	/* wait loggers are created */
	while (created_logs < running)
		tt_pthread_cond_wait(&cond_sync, &mutex);
	tt_pthread_mutex_unlock(&mutex);
	say_logrotate(NULL, NULL, 0);

	for (int i = 0; i < created_logs; i++) {
		log_destroy(&loggers[i].logger);
	}
	memset(loggers, '#', NUMBER_LOGGERS * sizeof(struct create_log));
	free(loggers);

	is_raised = true;
	tt_pthread_cond_broadcast(&cond);

	tt_pthread_mutex_lock(&mutex);
	/* wait threads are finished */
	while (created_logs > 0)
		tt_pthread_cond_wait(&cond_sync, &mutex);
	tt_pthread_mutex_unlock(&mutex);
}

static int
main_f(va_list ap)
{
	struct errinj *inj = errinj_by_name("ERRINJ_LOG_ROTATE");
	inj->bparam = true;
	/* test on log_rotate signal handling */
	test_log_rotate();
	inj->bparam = false;
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int main()
{
	memory_init();
	fiber_init(fiber_c_invoke);
	say_logger_init("/dev/null", S_INFO, 0, "plain", 0);

	plan(33);

#define PARSE_LOGGER_TYPE(input, rc) \
	ok(parse_logger_type(input) == rc, "%s", input)

	PARSE_LOGGER_TYPE("", 0);
	PARSE_LOGGER_TYPE("/dev/null", 0);
	PARSE_LOGGER_TYPE("|", 0);
	PARSE_LOGGER_TYPE("|/usr/bin/cronolog", 0);
	PARSE_LOGGER_TYPE("file:", 0);
	PARSE_LOGGER_TYPE("file:instance.log", 0);
	PARSE_LOGGER_TYPE("pipe:", 0);
	PARSE_LOGGER_TYPE("pipe:gzip > instance.log.gz", 0);
	PARSE_LOGGER_TYPE("syslog:", 0);
	PARSE_LOGGER_TYPE("syslog:identity=", 0);
	PARSE_LOGGER_TYPE("unknown:", -1);
	PARSE_LOGGER_TYPE("unknown:example.org", -1);

#define PARSE_SYSLOG_OPTS(input, rc) \
	ok(parse_syslog_opts(input) == rc, "%s", input)

	PARSE_SYSLOG_OPTS("", 0);
	PARSE_SYSLOG_OPTS("identity=tarantool", 0);
	PARSE_SYSLOG_OPTS("facility=user", 0);
	PARSE_SYSLOG_OPTS("identity=xtarantoolx,facility=local1", 0);
	PARSE_SYSLOG_OPTS("identity=xtarantoolx,facility=kern", 0);
	PARSE_SYSLOG_OPTS("identity=xtarantoolx,facility=uucp", 0);
	PARSE_SYSLOG_OPTS("identity=xtarantoolx,facility=foo", -1);
	PARSE_SYSLOG_OPTS("facility=authpriv,identity=bar", 0);
	PARSE_SYSLOG_OPTS("invalid=", -1);
	PARSE_SYSLOG_OPTS("facility=local1,facility=local2", -1);
	PARSE_SYSLOG_OPTS("identity=foo,identity=bar", -1);
	char template[] = "/tmp/tmpdir.XXXXXX";
	const char *tmp_dir = mkdtemp(template);
	if (tmp_dir == NULL) {
		diag("unit/say: failed to create temp dir: %s", strerror(errno));
		return check_plan();
	}
	char tmp_filename[30];
	sprintf(tmp_filename, "%s/1.log", tmp_dir);
	struct log test_log;
	log_create(&test_log, tmp_filename, false);
	log_set_format(&test_log, say_format_plain);
	log_say(&test_log, 0, NULL, 0, NULL, "hello %s\n", "user");
	log_set_format(&test_log, say_format_json);
	log_say(&test_log, 0, NULL, 0, NULL, "hello %s", "user");
	log_set_format(&test_log, format_func_custom);
	log_say(&test_log, 0, NULL, 0, NULL, "hello %s", "user");

	FILE* fd = fopen(tmp_filename, "r+");
	const size_t len = 4096;
	char line[len];

	if (fgets(line, len, fd) != NULL) {
		ok(strstr(line, "hello user") != NULL, "plain");
		fgets(line, len, fd);
	}
	if (fgets(line, len, fd) != NULL) {
		ok(strstr(line, "\"message\": \"hello user\"") != NULL, "json");
	}

	if (fgets(line, len, fd) != NULL) {
		ok(strstr(line, "\"msg\" = \"hello user\"") != NULL, "custom");
	}
	log_destroy(&test_log);

	coio_init();
	coio_enable();

	struct fiber *test = fiber_new("loggers", main_f);
	if (test == NULL) {
		diag_log();
		return check_plan();
	}
	fiber_wakeup(test);
	ev_run(loop(), 0);

	/*
	 * Ignore possible failure of log_create(). It may fail
	 * connecting to /dev/log or its analogs. We need only
	 * the format function here, as we change log.fd to file
	 * descriptor.
	 */
	log_create(&test_log, "syslog:identity=tarantool,facility=local0", false);
	test_log.fd = fileno(fd);
	/*
	 * redirect stderr to /dev/null in order to filter
	 * it out from result file.
	 */
	ok(freopen("/dev/null", "w", stderr) != NULL, "freopen");
	ok(strncmp(test_log.syslog_ident, "tarantool", 9) == 0, "parsed identity");
	ok(test_log.syslog_facility == SYSLOG_LOCAL0, "parsed facility");
	long before = ftell(fd);
	ok(before >= 0, "ftell");
	ok(log_say(&test_log, 0, NULL, 0, NULL, "hello %s", "user") > 0, "log_say");
	ok(fseek(fd, before, SEEK_SET) >= 0, "fseek");

	if (fgets(line, len, fd) != NULL) {
		ok(strstr(line, "<131>") != NULL, "syslog line");
	}
	log_destroy(&test_log);
	fiber_free();
	memory_free();
	return check_plan();
}
