#ifndef TS_OPTIONS_H_INCLUDED
#define TS_OPTIONS_H_INCLUDED

enum ts_options_mode {
	TS_MODE_USAGE,
	TS_MODE_VERSION,
	TS_MODE_CREATE
};

#define TT_VERSION_MINOR "1"
#define TT_VERSION_MAJOR "0"

struct ts_options {
	uint64_t limit;
	enum ts_options_mode mode;
	int to_lsn_set;
	int interval;
	uint64_t to_lsn;
	const char *file_config;
	struct tarantool_cfg cfg;
};

void ts_options_init(struct ts_options *opts);
void ts_options_free(struct ts_options *opts);

enum ts_options_mode
ts_options_process(struct ts_options *opts, int argc, char **argv);

int ts_options_usage(void);
int ts_options_version(void);

#endif
