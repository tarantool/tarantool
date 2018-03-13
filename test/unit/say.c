#include <string.h>
#include <fiber.h>
#include <memory.h>
#include "unit.h"
#include "say.h"
#include <pthread.h>

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
		note("facility: %s", opts.facility);
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

static void *
dummy_log(void *arg)
{
	const char *tmp_dir = (const char *) arg;
	char tmp_filename[30];
	sprintf(tmp_filename, "%s/%i.log", tmp_dir, (int) pthread_self());
	pthread_mutex_lock(&mutex);
	struct log test_log;
	log_create(&test_log, tmp_filename, false);
	// signal that log is created
	created_logs++;
	pthread_cond_signal(&cond_sync);

	// wait until rotate signal is raised
	while (!is_raised)
		pthread_cond_wait(&cond, &mutex);

	log_destroy(&test_log);
	created_logs--;
	pthread_cond_signal(&cond_sync);
	pthread_mutex_unlock(&mutex);
	return NULL;
}

static void
test_log_rotate()
{
	char template[] = "/tmp/tmpdir.XXXXXX";
	const char *tmp_dir = mkdtemp(template);
	int running = 0;
	for (int i = 0; i < 10; i++) {
		pthread_t thread;
		if (pthread_create(&thread, NULL, dummy_log, (void *) tmp_dir) >= 0)
			running++;
	}
	pthread_mutex_lock(&mutex);
	// wait loggers are created
	while (created_logs < running) {
		pthread_cond_wait(&cond_sync, &mutex);
	}
	raise(SIGHUP);
	is_raised = true;
	pthread_cond_broadcast(&cond);

	// wait until loggers are closed
	while(created_logs != 0)
		pthread_cond_wait(&cond_sync, &mutex);
	pthread_mutex_unlock(&mutex);
}

int main()
{
	memory_init();
	fiber_init(fiber_c_invoke);
	say_logger_init("/dev/null", S_INFO, 0, "plain", 0);

	plan(23);

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
	PARSE_SYSLOG_OPTS("facility=foo,identity=bar", 0);
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

	FILE* fd = fopen(tmp_filename, "r");
	char *line = NULL;
	size_t len = 0;

	if (getline(&line, &len, fd) != -1) {
		ok(strstr(line, "hello user") != NULL, "plain");
		getline(&line, &len, fd);
	}
	if (getline(&line, &len, fd) != -1) {
		ok(strstr(line, "\"message\": \"hello user\"") != NULL, "json");
	}

	if (getline(&line, &len, fd) != -1) {
		ok(strstr(line, "\"msg\" = \"hello user\"") != NULL, "custom");
	}
	log_destroy(&test_log);

	// test on log_rotate signal handling
	struct ev_signal ev_sig;
	ev_signal_init(&ev_sig, say_logrotate, SIGHUP);
	ev_signal_start(loop(), &ev_sig);
	test_log_rotate();
	ev_signal_stop(loop(), &ev_sig);
	return check_plan();
}
