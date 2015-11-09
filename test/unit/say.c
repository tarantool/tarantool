#include <string.h>
#include "unit.h"
#include "say.h"

int
parse_logger_type(const char *input)
{
	enum say_logger_type type;
	int rc = say_parse_logger_type(&input, &type);

	if (rc == 0)
		switch (type) {
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
	char *error;
	if (say_parse_syslog_opts(input, &opts, &error) == -1) {
		note("error: %s", error);
		free(error);
		return -1;
	}
	if (opts.identity)
		note("identity: %s", opts.identity);
	if (opts.facility)
		note("facility: %s", opts.facility);
	say_free_syslog_opts(&opts);
	return 0;
}

int main()
{
	say_init("");
	say_logger_init("/dev/null", S_INFO, 0, 0);

	plan(20);

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

	return check_plan();
}
