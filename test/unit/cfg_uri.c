#include "unit.h"
#include "cfg_uri.h"
#include "lua/utils.h"
#include "trivia/util.h"
#include "diag.h"
#include "memory.h"
#include "fiber.h"
#include "tt_static.h"

#include <stdio.h>

#define BASE_URI_NAME "/path_to_unix_socket"
#define BASE_URI_NAME_LEN sizeof(BASE_URI_NAME) - 1

struct cfg_uri_validator {
	/**
	 * Expected count of options for URI at the top of the
	 * lua stack. Should be same for all URIs.
	 */
	int opt_cnt;
	/**
	 * First option value, should be same for all URIs.
	 * Each next value should be twice as large as the
	 * previous one.
	 */
	int first_optval;
	/**
	 * Count of option values. Should be same for all
	 * options of all URIs.
	 */
	int optval_cnt;
};

/**
 * A structure that contains the information expected
 * after URI parsing
 */
struct cfg_uri_array_validator {
	/**
	 * Some meta information used for in the validation
	 * function (may be URI, some URI options or just
	 * log message).
	 */
	const char *meta;
	/** Expected count of URI at the top of the lua stack. */
	int uri_cnt;
	/** Expected settings for all URIs. */
	struct cfg_uri_validator uris_validator;
	/** Expected settings for common URIs options */
	struct cfg_uri_validator common_options_validator;
};

/**
 * Check count of values of the URI @a option. Each @a option
 * value should be a string, which contain number. Each next
 * option value should be twice as large as the previous one.
 */
static int
cfg_uri_option_validate(const struct cfg_uri_option *option,
			const struct cfg_uri_validator *validator)
{
	plan(1 + option->size);
	is(option->size, validator->optval_cnt,
	   tt_sprintf("'%s' count of values of the URI option is valid",
		      option->name));
	int optval  = validator->first_optval;
	for (int i = 0; i < option->size; i++, optval *= 2) {
		char string_optval[13];
		snprintf(string_optval, sizeof(string_optval), "%d", optval);
		is(strcmp(option->values[i], string_optval),
		   0, "value of URI option is valid")
	}
	return check_plan();
}

/**
 * Check single URI structure. All options which
 * are missing should be zeroed.
 */
static int
cfg_uri_validate(const struct cfg_uri *uri,
		 const struct cfg_uri_validator *validator)
{
	plan(CFG_URI_OPTION_MAX);
	for (int i = validator->opt_cnt; i < CFG_URI_OPTION_MAX; i++) {
		struct cfg_uri_option opt = {0};
		int rc = memcmp(&uri->options[i], &opt,
				sizeof(struct cfg_uri_option));
		is(rc, 0, "missing URI options are zeroed")
	}
	for (int i = 0; i < validator->opt_cnt; i++)
		cfg_uri_option_validate(&uri->options[i], validator);
	return check_plan();
}

/**
 * Check @a array, structure according to expected @a validator.
 */
static int
cfg_uri_array_validate(const struct cfg_uri_array *array,
		       const struct cfg_uri_array_validator *validator)
{
	plan(1 + 2 * validator->uri_cnt + 1);
	is (array->size, validator->uri_cnt, "count of URIs is valid");
	for (int i = 0; i < array->size; i++) {
		char uri[BASE_URI_NAME_LEN + 3];
		strcpy(uri, BASE_URI_NAME);
		strcat(uri, tt_sprintf("_%d", i + 1));
		is(strcmp(array->uris[i].uri, uri), 0, "URI is valid");
		cfg_uri_validate(&array->uris[i], &validator->uris_validator);
	}
	cfg_uri_validate(&array->common_options_storage,
			 &validator->common_options_validator);
	return check_plan();
}

/**
 * Check that URI located at the top position of the lua stack
 * parsed successfully and meets expectations. Pay attention,
 * this function relies on the several facts:
 * - first URI has a name `/path_to_unix_socket_1` and for each
 *   subsequent URI, the digit at the end of the name increases
 *   by 1.
 * - URI options are passed in order based on `cfg_uri_options`
 *   enuum. It means that if opt_cnt == 2, we pass `backlog` and
 *   `readahead` options.
 * - We check only such options that have the `int` type. Option
 *   values count is same for all passsed options. Each subsequent
 *   option value is twice as large as the previous one.
 */
static void
check_valid_parse(const struct cfg_uri_array_validator *validator,
		  struct lua_State *L)
{
	struct cfg_uri_array array;
	int rc = cfg_uri_array_create(&array, L, "listen");
	const char *errmsg = (rc != 0 ?
		diag_last_error(diag_get())->errmsg :
		tt_sprintf("%s: parsed successfully", validator->meta));
	is(rc, 0, "%s", errmsg);
	if (rc == 0) {
		cfg_uri_array_validate(&array, validator);
		cfg_uri_array_destroy(&array);
	}
}

/**
 * Check that URI located at the top of the stack is invalid.
 */
static void
check_invalid_parse(const char *listen, struct lua_State *L)
{
	struct cfg_uri_array array;
	int rc = cfg_uri_array_create(&array, L, "listen");
	const char *errmsg = (rc != 0 ?
		diag_last_error(diag_get())->errmsg :
		tt_sprintf("%s: is invalid", listen));
	isnt(rc, 0, "%s", errmsg);
	if (rc == 0)
		cfg_uri_array_destroy(&array);
}


static void
prepare_string_uri_array(const char **string_uri, unsigned size,
			 struct lua_State *L)
{
	lua_newtable(L);
	for (unsigned int i = 0; i < size; i++) {
		lua_pushstring(L, string_uri[i]);
		lua_rawseti(L, -2, i + 1);
	}
}

static int
test_valid_string_uri(struct lua_State *L)
{
	const struct cfg_uri_array_validator validator_array[] = {
		/* One string URI without options. */
		{
			/* meta = */ BASE_URI_NAME "_1",
			/* uri_count = */ 1,
			/* uris_validator = */ {
				/* opt_cnt = */ 0,
				/* first_optval = */ 0,
				/* optval_cnt = */ 0,
			},
			/* common_options_validator = */ { 0 },
		},
		/* One string URI with one option and one option value. */
		{
			/* meta = */ BASE_URI_NAME "_1"
				     "?" "backlog=10",
			/* uri_count = */ 1,
			/* uris_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 1,
			},
			/* common_options_validator = */ { 0 },
		},
		/*
		 * One string URI with one option and two option values,
		 * separated by ";".
		 */
		{
			/* meta = */ BASE_URI_NAME "_1"
				     "?" "backlog=10;20",
			/* uri_count = */ 1,
			/* uris_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 2,
			},
			/* common_options_validator = */ { 0 },
		},
		/*
		 * One string URI with one option and two option values,
		 * separated by "&".
		 */
		{
			/* meta = */ BASE_URI_NAME "_1"
				     "?" "backlog=10&backlog=20",
			/* uri_count = */ 1,
			/* uris_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 2,
			},
			/* common_options_validator = */ { 0 },
		},
		/*
		 * One string URI with one option and several option values,
		 * passed in different ways.
		 */
		{
			/* meta = */ BASE_URI_NAME "_1"
				     "?" "backlog=10;20&backlog=40;80",
			/* uri_count = */ 1,
			/* uris_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 4,
			},
			/* common_options_validator = */ { 0 },
		},
		/*
		 * One string URI with several options and several option
		 * values, passed in different ways.
		 */
		{
			/* meta = */ BASE_URI_NAME "_1"
				     "?" "backlog=2048;4096&backlog=8192;16384"
				     "&" "readahead=2048;4096"
				     "&" "readahead=8192;16384",
			/* uri_count = */ 1,
			/* uris_validator = */ {
				/* opt_cnt = */ 2,
				/* first_optval = */ 2048,
				/* optval_cnt = */ 4,
			},
			/* common_options_validator = */ { 0 },
		},
		/*
		 * Two string URIs separated by commas, with several options
		 * and several option values, passed in different ways.
		 */
		{
			/* meta = */ BASE_URI_NAME "_1"
				     "?" "backlog=2048;4096&backlog=8192;16384"
				     "&" "readahead=2048;4096"
				     "&" "readahead=8192;16384"
				     ", "
				     BASE_URI_NAME "_2"
				     "?" "backlog=2048;4096&backlog=8192;16384"
				     "&" "readahead=2048;4096"
				     "&" "readahead=8192;16384",
			/* uri_count = */ 2,
			/* uris_validator = */ {
				/* opt_cnt = */ 2,
				/* first_optval = */ 2048,
				/* optval_cnt = */ 4,
			},
			/* common_options_validator = */ { 0 },
		},
	};
	plan(2 * lengthof(validator_array));
	for (unsigned i = 0; i < lengthof(validator_array); i++) {
		lua_pushstring(L, validator_array[i].meta);
		check_valid_parse(&validator_array[i], L);
		lua_pop(L, 1);
	}
	return check_plan();
}

static int
test_invalid_string_uri(struct lua_State *L)
{
	const char *string_uri[] = {
		/** not found query for URI after '?' */
		"/path_to_unix_socket?",
		/** not found option value for URI */
		"/path_to_unix_socket??",
		/** not found option for URI after '&' */
		"/path_to_unix_socket?backlog=10&",
		/** Same as previous, but for URI with several options. */
		"/path_to_unix_socket?backlog=10&backlog=20&",
		/** not found option for URI after '&' */
		"/path_to_unix_socket?backlog=10&&backlog=20",
		/** not found option value for URI */
		"/path_to_unix_socket?backlog",
		/** Same as previous, but for URI with several options. */
		"/path_to_unix_socket?backlog=10&backlog",
		/** not found option value for URI after '=' */
		"/path_to_unix_socket?backlog=",
		/** Same as previous, but for URI with several options. */
		"/path_to_unix_socket?backlog=10&backlog=",
		/** invalid option name for URI */
		"/path_to_unix_socket?unexpected_option=10",
		/** not found option value for URI */
		"/path_to_unix_socket?backlog=10;",
		/** Same as previous, but for URI with several option values. */
		"/path_to_unix_socket?backlog=10;20;",
		/** not found option value for URI */
		"/path_to_unix_socket?backlog=10;;20",
	};
	plan(lengthof(string_uri));
	for (unsigned i = 0; i < lengthof(string_uri); i++) {
		lua_pushstring(L, string_uri[i]);
		check_invalid_parse(string_uri[i], L);
		lua_pop(L, 1);
	}
	return check_plan();
}

static int
test_common_options_string(struct lua_State *L)
{
	/*
	 * Common options passed in the string are passed
	 * as if they were part of a URI.
	 */
	const struct cfg_uri_array_validator validator_array[] = {
		{
			/* meta = */ "backlog=10",
			/* uri_count = */ 0,
			/* uris_validator = */ { 0 },
			/* common_options_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 1,
			},
		},
		{
			/* meta = */ "backlog=10;20",
			/* uri_count = */ 0,
			/* uris_validator = */ { 0 },
			/* common_options_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 2,
			},
		},
		{
			/* meta = */ "backlog=10&backlog=20",
			/* uri_count = */ 0,
			/* uris_validator = */ { 0 },
			/* common_options_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 2,
			},
		},
		{
			/* meta = */ "backlog=10;20&backlog=40;80",
			/* uri_count = */ 0,
			/* uris_validator = */ { 0 },
			/* common_options_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 4,
			},
		},
		{
			/* meta = */ "backlog=2048;4096&backlog=8192;16384"
				     "&" "readahead=2048;4096"
				     "&" "readahead=8192;16384",
			/* uri_count = */ 0,
			/* uris_validator = */ { 0 },
			/* common_options_validator = */ {
				/* opt_cnt = */ 2,
				/* first_optval = */ 2048,
				/* optval_cnt = */ 4,
			},
		},
	};
	const char *invalid_common_options[] = {
		"backlog=10&",
		"backlog=10&backlog=20&",
		"backlog=10&&backlog=20",
		"backlog",
		"backlog=10&backlog",
		"backlog=",
		"backlog=10&backlog=",
		"unexpected_option=10",
		"backlog=10;",
		"backlog=10;20;",
		"backlog=10;;20",
	};
	plan(2 * lengthof(validator_array) + lengthof(invalid_common_options));
	for (unsigned i = 0; i < lengthof(validator_array); i++) {
		lua_newtable(L);
		lua_pushstring(L, "options");
		lua_pushstring(L, validator_array[i].meta);
		lua_settable(L, -3);
		check_valid_parse(&validator_array[i], L);
		lua_pop(L, 1);
	}
	for (unsigned i = 0; i < lengthof(invalid_common_options); i++) {
		lua_newtable(L);
		lua_pushstring(L, "options");
		lua_pushstring(L, invalid_common_options[i]);
		lua_settable(L, -3);
		check_invalid_parse(invalid_common_options[i], L);
		lua_pop(L, 1);
	}
	return check_plan();
}

static int
test_common_options_table(struct lua_State *L)
{
	/*
	 * Common options passed in the string are passed
	 * as if they were part of a URI.
	 */
	const struct cfg_uri_array_validator validator_array[] = {
		{
			/* meta = */ "10",
			/* uri_count = */ 0,
			/* uris_validator = */ { 0 },
			/* common_options_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 1,
			},
		},
		{
			/* meta = */ "10;20;40;80",
			/* uri_count = */ 0,
			/* uris_validator = */ { 0 },
			/* common_options_validator = */ {
				/* opt_cnt = */ 1,
				/* first_optval = */ 10,
				/* optval_cnt = */ 4,
			},
		},
	};
	const char *invalid_common_options_values[] = {
		"10;",
		"10;;20",
	};
	plan(2 * lengthof(validator_array) +
	     lengthof(invalid_common_options_values));
	lua_newtable(L);
	lua_pushstring(L, "options");
	lua_newtable(L);
	lua_settable(L, -3);
	for (unsigned i = 0; i < lengthof(validator_array); i++) {
		lua_pushstring(L, "options");
		lua_gettable(L, -2);
		lua_pushstring(L, "backlog");
		lua_pushstring(L, validator_array[i].meta);
		lua_settable(L, -3);
		lua_pop(L, 1);
		check_valid_parse(&validator_array[i], L);
	}
	for (unsigned i = 0; i < lengthof(invalid_common_options_values); i++) {
		lua_pushstring(L, "options");
		lua_gettable(L, -2);
		lua_pushstring(L, "backlog");
		lua_pushstring(L, invalid_common_options_values[i]);
		lua_settable(L, -3);
		lua_pop(L, 1);
		check_invalid_parse(invalid_common_options_values[i], L);
	}
	lua_pop(L, 1);
	return check_plan();
}

static int
test_string_uri_array(struct lua_State *L)
{
	/**
	 * URI array with same options, but passed in different ways
	 */
	const char *string_uri[] = {
		BASE_URI_NAME "_1" "?" "backlog=2048&backlog=4096"
				   "&" "backlog=8192&backlog=16384"
				   "&" "readahead=2048&readahead=4096"
				   "&" "readahead=8192&readahead=16384",
		BASE_URI_NAME "_2" "?" "backlog=2048;4096&backlog=8192;16384"
				   "&" "readahead=2048;4096"
				   "&" "readahead=8192;16384",
		BASE_URI_NAME "_3" "?" "backlog=2048;4096;8192;16384"
				   "&" "readahead=2048;4096"
				   "&" "readahead=8192;16384"
				   ", "
		BASE_URI_NAME "_4" "?" "backlog=2048;4096;8192;16384"
				   "&" "readahead=2048;4096;8192;16384",
		/**
		 * Invalid URI: not found query for URI after '?'.
		 * Used to check graceful resource release in case when
		 * we try to parse invalid URI array.
		 */
		BASE_URI_NAME "?",
	};
	const struct cfg_uri_array_validator validator_array = {
			/* meta = */ "string URI array",
			/* uri_count = */ 4,
			/* uris_validator = */ {
				/* opt_cnt = */ 2,
				/* first_optval = */ 2048,
				/* optval_cnt = */ 4,
			},
			/* common_options_validator = */ { 0 },
	};
	plan(2 + 1);
	prepare_string_uri_array(string_uri, lengthof(string_uri) - 1, L);
	check_valid_parse(&validator_array, L);
	lua_pop(L, 1);
	prepare_string_uri_array(string_uri, lengthof(string_uri), L);
	check_invalid_parse("string URI array", L);
	lua_pop(L, 1);
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	lua_State* L = lua_open();
	plan(5);
	test_valid_string_uri(L);
	test_invalid_string_uri(L);
	test_common_options_string(L);
	test_common_options_table(L);
	test_string_uri_array(L);
	fiber_free();
	memory_free();
	return check_plan();
}
