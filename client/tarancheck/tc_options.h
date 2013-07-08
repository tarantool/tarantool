#ifndef TC_OPTIONS_H_INCLUDED
#define TC_OPTIONS_H_INCLUDED

enum tc_options_mode {
	TC_MODE_USAGE,
	TC_MODE_VERSION,
	TC_MODE_GENERATE,
	TC_MODE_VERIFY
};

struct tc_options {
	enum tc_options_mode mode;
	const char *file;
	const char *file_config;
	struct tarantool_cfg cfg;
};

void tc_options_init(struct tc_options *opts);
void tc_options_free(struct tc_options *opts);

enum tc_options_mode tc_options_process(struct tc_options *opts, int argc, char **argv);
int tc_options_usage(void);

#endif
