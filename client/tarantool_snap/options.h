#ifndef TS_OPTIONS_H_INCLUDED
#define TS_OPTIONS_H_INCLUDED

enum ts_options_mode {
	TS_MODE_USAGE,
	TS_MODE_VERSION,
	TS_MODE_CREATE
};

struct ts_options {
	enum ts_options_mode mode;
	const char *file_config;
	struct tarantool_cfg cfg;
};

void ts_options_init(struct ts_options *opts);
void ts_options_free(struct ts_options *opts);

enum ts_options_mode
ts_options_process(struct ts_options *opts, int argc, char **argv);

int ts_options_usage(void);

#endif
