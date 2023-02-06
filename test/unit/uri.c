#include "uri/uri.h"
#include "trivia/util.h"

#include <stdio.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#define URI_MAX 10
#define URI_PARAM_MAX 10
#define URI_PARAM_VALUE_MAX 10

#define is_str(a, b, fmt, args...) ok(strcmp(a, b) == 0, fmt, ##args)

static void
sample_uri_create(struct uri *uri)
{
	int rc = uri_create(uri,
			    "scheme://login:password@127.0.0.1:3301/path?"
			    "q1=v1&q1=v2&q2=v3&q3#fragment");
	ok(rc == 0, "sample uri create");
}

static void
sample_uri_check(const struct uri *uri, const char *msg)
{
	plan(16);
	is_str(uri->scheme, "scheme", "%s scheme", msg);
	is_str(uri->login, "login", "%s login", msg);
	is_str(uri->password, "password", "%s password", msg);
	is_str(uri->host, "127.0.0.1", "%s host", msg);
	is_str(uri->service, "3301", "%s service", msg);
	is_str(uri->path, "/path", "%s path", msg);
	is_str(uri->query, "q1=v1&q1=v2&q2=v3&q3", "%s query", msg);
	is_str(uri->fragment, "fragment", "%s fragment", msg);
	is(uri->host_hint, 1, "%s hint", msg);
	is(uri->param_count, 3, "%s param count", msg);
	is(uri_param_count(uri, "q1"), 2, "%s param 1 value count", msg);
	is_str(uri_param(uri, "q1", 0), "v1", "%s param 1 value 1", msg);
	is_str(uri_param(uri, "q1", 1), "v2", "%s param 1 value 2", msg);
	is(uri_param_count(uri, "q2"), 1, "%s param 2 value count", msg);
	is_str(uri_param(uri, "q2", 0), "v3", "%s param 2 value", msg);
	is(uri_param_count(uri, "q3"), 0, "%s param 3 value count", msg);
	check_plan();
}

static void
empty_uri_create(struct uri *uri)
{
	int rc = uri_create(uri, NULL);
	ok(rc == 0, "empty uri create");
}

static void
empty_uri_check(struct uri *uri, const char *msg)
{
	plan(12);
	is(uri->scheme, NULL, "%s scheme", msg);
	is(uri->login, NULL, "%s login", msg);
	is(uri->password, NULL, "%s password", msg);
	is(uri->host, NULL, "%s host", msg);
	is(uri->service, NULL, "%s service", msg);
	is(uri->path, NULL, "%s path", msg);
	is(uri->query, NULL, "%s query", msg);
	is(uri->fragment, NULL, "%s fragment", msg);
	is(uri->host_hint, 0, "%s hint", msg);
	is(uri->param_count, 0, "%s param count", msg);
	is(uri->params, NULL, "%s params", msg);
	ok(uri_is_nil(uri), "%s is_nil()", msg);
	check_plan();
}

static void
test_copy_sample(void)
{
	header();
	plan(3);
	struct uri src;
	sample_uri_create(&src);
	struct uri dst;
	uri_copy(&dst, &src);
	sample_uri_check(&src, "src");
	sample_uri_check(&dst, "dst");
	uri_destroy(&src);
	uri_destroy(&dst);
	check_plan();
	footer();
}

static void
test_copy_empty(void)
{
	header();
	plan(3);
	struct uri src;
	empty_uri_create(&src);
	struct uri dst;
	uri_copy(&dst, &src);
	empty_uri_check(&src, "src");
	empty_uri_check(&dst, "dst");
	uri_destroy(&src);
	uri_destroy(&dst);
	check_plan();
	footer();
}

static void
test_move_sample(void)
{
	header();
	plan(3);
	struct uri src;
	sample_uri_create(&src);
	struct uri dst;
	uri_move(&dst, &src);
	empty_uri_check(&src, "src");
	sample_uri_check(&dst, "dst");
	uri_destroy(&src);
	uri_destroy(&dst);
	check_plan();
	footer();
}

static void
test_move_empty(void)
{
	header();
	plan(3);
	struct uri src;
	empty_uri_create(&src);
	struct uri dst;
	uri_move(&dst, &src);
	empty_uri_check(&src, "src");
	empty_uri_check(&dst, "dst");
	uri_destroy(&src);
	uri_destroy(&dst);
	check_plan();
	footer();
}

struct uri_equal_expected {
	/** Source string for the first uri. */
	const char *src_a;
	/** Source string for the second uri. */
	const char *src_b;
	/** Expected comparison result. */
	bool is_equal;
};

static void
test_addr_is_equal(void)
{
	struct uri_equal_expected test_pairs[] = {
		{NULL, NULL, true},
		{"localhost", "localhost", true},
		{"user@localhost", "localhost", true},
		{"user:pass@localhost", "localhost", true},
		{"user:pass@localhost", "user@localhost", true},
		{"localhost:3301", "localhost:3302", false},
		{"host_a", "host_b", false},
		{"scheme://localhost", "localhost", true},
		{"scheme1://host:port", "scheme2://host:port", true},
		{"localhost/path/to/file", "localhost", false},
		{"/path/to/file", "/path/to/file", true},
		{"/path/to/file", "localhost/path/to/file", false},
		{"unix/path/to/file", "/path/to/file", false},
		{"unix/:/path/to/file", "/path/to/file", true},
	};
	header();
	plan(3 * lengthof(test_pairs));
	for (unsigned i = 0; i < lengthof(test_pairs); i++) {
		struct uri uri_a, uri_b;
		const char *src_a = test_pairs[i].src_a;
		const char *src_b = test_pairs[i].src_b;
		bool is_equal = test_pairs[i].is_equal;
		ok(uri_create(&uri_a, src_a) == 0, "uri_create(%s)",
		   src_a ? src_a : "NULL");
		ok(uri_create(&uri_b, src_b) == 0, "uri_create(%s)",
		   src_b ? src_b : "NULL");
		is(uri_addr_is_equal(&uri_a, &uri_b), is_equal,
		   "%s %s equal to %s", src_a ? src_a : "NULL",
		   is_equal ? "is" : "isn't", src_b ? src_b : "NULL");
	}
	check_plan();
	footer();
}

struct uri_param_expected {
	/** URI parameter name */
	const char *name;
	/** Count of URI parameter values */
	int value_count;
	/** Expected URI parameter values */
	const char *values[URI_PARAM_VALUE_MAX];
};

struct uri_expected {
	/** String URI passed for parse and validation */
	const char *string;
	/** Count of URI parameters */
	int param_count;
	/** Array of expected URI parameters */
	struct uri_param_expected params[URI_PARAM_MAX];
};

struct uri_set_expected {
	/** String with several URIs passed for parse and validation */
	const char *string;
	/** Count of URIs */
	int uri_count;
	/** Array of expected URIs */
	struct uri_expected uris[URI_MAX];
};

struct str_escape {
	const char *str;
	const char *escaped;
	const char *unreserved;
	bool plus;
};

static int
uri_param_expected_check(const struct uri_param_expected *param,
			       const struct uri *uri)
{
	plan(1 + param->value_count);
	int value_count = uri_param_count(uri, param->name);
	is(param->value_count, value_count, "value count");
	for (int idx = 0; idx < MIN(value_count, param->value_count); idx++) {
		const char *value = uri_param(uri, param->name, idx);
		is(strcmp(value, param->values[idx]), 0, "param value");
	}
	return check_plan();
}

static int
uri_expected_check(const struct uri_expected *uri_ex, const struct uri *uri)
{
	plan(1 + uri_ex->param_count);
	is(uri_ex->param_count, uri->param_count, "param count");
	for (int i = 0; i < MIN(uri_ex->param_count, uri->param_count); i++)
		uri_param_expected_check(&uri_ex->params[i], uri);
	return check_plan();
}

static int
uri_set_expected_check(const struct uri_set_expected *uri_set, bool parse_is_successful)
{
	struct uri_set u;
	int rc = uri_set_create(&u, uri_set->string);
	plan(1 + uri_set->uri_count);
	is(rc, parse_is_successful ? 0 : -1, "%s: parse %s", uri_set->string,
	   parse_is_successful ? "successful" : "unsuccessful");
	for (int i = 0; i < MIN(uri_set->uri_count, u.uri_count); i++) {
		uri_expected_check(&uri_set->uris[i], &u.uris[i]);
	}
	uri_set_destroy(&u);
	return check_plan();
}

static void
test_string_uri_with_query_params_parse(void)
{
	const struct uri_expected uris[] = {
		/* One string URI without parameters. */
		[0] = {
			.string = "/unix.sock",
			.param_count = 0,
			.params = {},
		},
		/* One string URI without parameters with additional '?'. */
		[1] = {
			.string = "/unix.sock?",
			.param_count = 0,
			.params = {},
		},
		/* One string URI with one parameter and one parameter value. */
		[2] = {
			.string = "/unix.sock?q1=v1",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 1,
					.values = { "v1" },
				},
			},
		},
		/*
		 * Same as previous but with extra '&' at the end
		 * of the string.
		 */
		[3] = {
			.string = "/unix.sock?q1=v1&",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 1,
					.values = { "v1" },
				},
			},
		},
		/*
		 * Same as previos but with two extra '&' at the end
		 * of the string.
		 */
		[4] = {
			.string = "/unix.sock?q1=v1&&",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 1,
					.values = { "v1" },
				},
			},
		},
		/*
		 * One string URI with one parameter and two parameter values,
		 * separated by "&".
		 */
		[5] = {
			.string = "/unix.sock?q1=v1&q1=v2",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 2,
					.values = { "v1", "v2" },
				},
			},
		},
		/*
		 * Same as previous but with extra '&' between parameters.
		 */
		[6] = {
			.string = "/unix.sock?q1=v1&&q1=v2",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 2,
					.values = { "v1", "v2" },
				},
			},
		},
		/*
		 * On string uri with several parameters without values.
		 */
		[7] = {
			.string = "/unix.sock?q1&q2",
			.param_count = 2,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 0,
					.values = {},
				},
				[1] = {
					.name = "q2",
					.value_count = 0,
					.values = {},
				},
			}
		},
		/*
		 * One string URI with several parameters.
		 */
		[8] = {
			.string = "/unix.sock?q1=v11&q1=v12&q2=v21&q2=v22",
			.param_count = 2,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 2,
					.values = { "v11", "v12" },
				},
				[1] = {
					.name = "q2",
					.value_count = 2,
					.values = { "v21", "v22" },
				},
			},
		},
		/*
		 * One string URI with several parameters, at the same time,
		 * some of them have empty value or don't have values at all.
		 */
		[9] = {
			.string = "/unix.sock?q1=v1&q1=&q2&q3=",
			.param_count = 3,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 2,
					.values = { "v1", "" },
				},
				[1] = {
					.name = "q2",
					.value_count = 0,
					.values = {},
				},
				[2] = {
					.name = "q3",
					.value_count = 1,
					.values = { "" },
				},
			},
		},
		/*
		 * Single URI with query, that contains extra '=' between
		 * parameter and it's value. (All extra '=' is interpreted
		 * as a part of value).
		 */
		[10] = {
			.string = "/unix.sock?q1===v1&q2===v2",
			.param_count = 2,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 1,
					.values = { "==v1" },
				},
				[1] = {
					.name = "q2",
					.value_count = 1,
					.values = { "==v2" },
				},
			},
		},
		/*
		 * Single URI with strange query, that contains combination
		 * of delimiters.
		 */
		[11] = {
			.string = "/unix.sock?&=&=",
			.param_count = 0,
			.params = {},
		},
		/*
		 * Same as previous, but another sequence of delimiters.
		 */
		[12] = {
			.string = "/unix.sock?=&=&",
			.param_count = 0,
			.params = {},
		}
	};
	header();
	plan(2 * lengthof(uris));
	struct uri u;
	for (unsigned i = 0; i < lengthof(uris); i++) {
		int rc = uri_create(&u, uris[i].string);
		is(rc, 0, "%s: parse", uris[i].string);
		uri_expected_check(&uris[i], &u);
		uri_destroy(&u);
	}
	check_plan();
	footer();
}

static void
test_string_uri_set_with_query_params_parse(void)
{
	const struct uri_set_expected uri_set_array[] = {
		/**
		 * One string URI with several query parameters, at the same
		 * time, some of them have empty value or don't have values at
		 * all. Most common example for the single URI.
		 */
		[0] = {
			.string = "/unix.sock?q1=v1&q1=&q2&q3=",
			.uri_count = 1,
			.uris = {
				[0] = {
					.string = NULL,
					.param_count = 3,
					.params = {
						[0] = {
							.name = "q1",
							.value_count = 2,
							.values = { "v1", "" },
						},
						[1] = {
							.name = "q2",
							.value_count = 0,
							.values = {},
						},
						[2] = {
							.name = "q3",
							.value_count = 1,
							.values = { "" },
						},
					},
				},
			},
		},
		/**
		 * Two URIs with different query parameters, separated
		 * by commas.
		 */
		[1] = {
			.string = "/unix.sock?q1=v1, unix.sock?q2=v2",
			.uri_count = 2,
			.uris = {
				[0] = {
					.string = NULL,
					.param_count = 1,
					.params = {
						[0] = {
							.name = "q1",
							.value_count = 1,
							.values = { "v1" },
						},
					},
				},
				[1] = {
					.string = NULL,
					.param_count = 1,
					.params = {
						[0] = {
							.name = "q2",
							.value_count = 1,
							.values = { "v2" },
						},
					},
				},
			},
		},
		/**
		 * Two URis with different parameters with differet values
		 * separated by commas. The most common case.
		 */
		[2] = {
			.string = "/unix.sock?q1=v1&q1=&q2&q3="
				  ","
				  "/unix.sock?q4=v4&q4=&q5&q6=",
			.uri_count = 2,
			.uris = {
				[0] = {
					.string = NULL,
					.param_count = 3,
					.params = {
						[0] = {
							.name = "q1",
							.value_count = 2,
							.values = { "v1", "" },
						},
						[1] = {
							.name = "q2",
							.value_count = 0,
							.values = {},
						},
						[2] = {
							.name = "q3",
							.value_count = 1,
							.values = { "" },
						},
					},
				},
				[1] = {
					.string = NULL,
					.param_count = 3,
					.params = {
						[0] = {
							.name = "q4",
							.value_count = 2,
							.values = { "v4", "" },
						},
						[1] = {
							.name = "q5",
							.value_count = 0,
							.values = {},
						},
						[2] = {
							.name = "q6",
							.value_count = 1,
							.values = { "" },
						},
					},
				},
			},
		},
		[3] = {
			.string = "",
			.uri_count = 0,
			.uris = {},
		}
	};
	header();
	plan(lengthof(uri_set_array));
	for (unsigned i = 0; i < lengthof(uri_set_array); i++)
		uri_set_expected_check(&uri_set_array[i], true);
	check_plan();
	footer();
}

static void
test_invalid_string_uri_set(void)
{
	const struct uri_set_expected uri_set_array[] = {
		/** Two URIs, second URIS is invalid. */
		[0] = {
			.string = "/unix.sock, ://",
			.uri_count = 0,
			.uris = {},
		},
		/**
		 * Extra ',' in different variants.
		 */
		[1] = {
			.string = "/unix.sock?q1=v1,, /unix.sock?q2=v2",
			.uri_count = 0,
			.uris = {}
		},
		[2] = {
			.string = "/unix.sock?q1=v1,,/unix.sock?q2=v2",
			.uri_count = 0,
			.uris = {}
		},
		[3] = {
			.string = "/unix.sock?q1=v1, ,/unix.sock?q2=v2",
			.uri_count = 0,
			.uris = {}
		},
		[4] = {
			.string = "/unix.sock?q1=v1 ,,/unix.sock?q2=v2",
			.uri_count = 0,
			.uris = {}
		}
	};
	header();
	plan(lengthof(uri_set_array));
	for (unsigned i = 0; i < lengthof(uri_set_array); i++)
		uri_set_expected_check(&uri_set_array[i], false);
	check_plan();
	footer();
}

#define RFC3986_unreserved "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-._~"

const struct str_escape escape_testcase[] = {
	[0] = {
		.str = "-._~",
		.escaped = "-._~",
		.unreserved = RFC3986_unreserved,
		.plus = false,
	},
	[1] = {
		.str = "0123456789",
		.escaped = "0123456789",
		.unreserved = RFC3986_unreserved,
		.plus = false,
	},
	[2] = {
		.str = "abcdefghijklm",
		.escaped = "abcdefghijklm",
		.unreserved = RFC3986_unreserved,
		.plus = false,
	},
	[3] = {
		.str = "nopqrstuvwxyz",
		.escaped = "nopqrstuvwxyz",
		.unreserved = RFC3986_unreserved,
		.plus = false,
	},
	[4] = {
		.str = "ABCDEFGHIJKLM",
		.escaped = "ABCDEFGHIJKLM",
		.unreserved = RFC3986_unreserved,
		.plus = false,
	},
	[5] = {
		.str = "NOPQRSTUVWXYZ",
		.escaped = "NOPQRSTUVWXYZ",
		.unreserved = RFC3986_unreserved,
		.plus = false,
	},
	[6] = {
		.str = "!$&'()*+,;=",
		.escaped = "%21%24%26%27%28%29%2A%2B%2C%3B%3D",
		.unreserved = RFC3986_unreserved,
		.plus = false,
	},
};

/**
 * Builds an array with unreserved characters.
 * uri.unreserved() implemented as a Lua function,
 * unreserved_tbl() replaces Lua implementation for testing purposes.
 */
static void
unreserved_tbl(const char *str, unsigned char unreserved[256])
{
	for (int i = 0; i < 256; i++)
		unreserved[i] = 0;

	for (; *str; str++) {
		unsigned char ch = (unsigned char)*str;
		unreserved[ch] = 1;
	}
}

static void
test_escape(void)
{
	header();
	plan(lengthof(escape_testcase) * 3);
	unsigned char unreserved[256];
	for (unsigned i = 0; i < lengthof(escape_testcase); i++) {
		const char *unescaped = escape_testcase[i].str;
		const char *escaped = escape_testcase[i].escaped;
		bool plus = escape_testcase[i].plus;
		char *dst = xcalloc(strlen(unescaped) * 3 + 1, sizeof(char));
		unreserved_tbl(escape_testcase[i].unreserved, unreserved);
		size_t dst_size = uri_escape(unescaped, strlen(unescaped),
					     dst, unreserved, plus);
		is(dst_size, strlen(escaped),
		   "escaped string ('%s') length != %ld", dst, strlen(escaped));
		is(strlen(dst), strlen(escaped),
		   "escaped string ('%s') length != %ld", dst, strlen(escaped));
		is(memcmp(escaped, dst, dst_size), 0,
		   "escape: '%s' == '%s'", escaped, dst);
		free(dst);
	}
	check_plan();
	footer();
}

static void
test_unescape(void)
{
	header();
	plan(lengthof(escape_testcase) * 3);
	for (unsigned i = 0; i < lengthof(escape_testcase); i++) {
		const char *unescaped = escape_testcase[i].str;
		const char *escaped = escape_testcase[i].escaped;
		bool decode_plus = escape_testcase[i].plus;
		char *dst = xcalloc(strlen(unescaped) + 1, sizeof(char));
		size_t dst_size = uri_unescape(escaped, strlen(escaped),
					       dst, decode_plus);
		is(dst_size, strlen(unescaped),
		   "unescaped string ('%s') length != %ld", dst,
		   strlen(unescaped));
		is(strlen(dst), strlen(unescaped),
		   "unescaped string ('%s') length != %ld", dst,
		   strlen(unescaped));
		is(memcmp(dst, unescaped, dst_size), 0,
		   "unescape: '%s' == '%s'", unescaped, dst);
		free(dst);
	}
	check_plan();
	footer();
}

const struct str_escape unescape_testcase[] = {
	/* Special case: %<non-hex><non-hex> */
	[0] = {
		.str = "%##",
		.escaped = "%##",
		.unreserved = "%%#",
		.plus = false,
	},
	/* Special case: %<hex><non-hex> */
	[1] = {
		.str = "%A$",
		.escaped = "%A$",
		.unreserved = "%%A$",
		.plus = false,
	},
	/* Special case: %<non-hex><hex> */
	[2] = {
		.str = "%$A",
		.escaped = "%$A",
		.unreserved = "%%$A",
		.plus = false,
	},
	/* Special case: %<EOS> (<EOS> -- the end of a string) */
	[3] = {
		.str = "%",
		.escaped = "%",
		.unreserved = "%%",
		.plus = false,
	},
	/* Special case: %<hex><EOS> (<EOS> -- the end of a string) */
	[4] = {
		.str = "%A",
		.escaped = "%A",
		.unreserved = "%%A",
		.plus = false,
	},
	/* Special case: %<non-hex><EOS> (<EOS> -- the end of a string) */
	[5] = {
		.str = "%&",
		.escaped = "%&",
		.unreserved = "%%&",
		.plus = false,
	},
};

static void
test_unescape_special_cases(void)
{
	header();
	plan(lengthof(unescape_testcase) * 3);
	for (unsigned i = 0; i < lengthof(unescape_testcase); i++) {
		const char *unescaped = unescape_testcase[i].str;
		const char *escaped = unescape_testcase[i].escaped;
		bool decode_plus = escape_testcase[i].plus;
		char *dst = xcalloc(strlen(unescaped) + 1, sizeof(char));
		size_t dst_size = uri_unescape(escaped, strlen(escaped),
					       dst, decode_plus);
		is(dst_size, strlen(unescaped),
		   "unescaped string ('%s') length != %ld", dst,
		   strlen(unescaped));
		is(strlen(dst), strlen(unescaped),
		   "unescaped string ('%s') length != %ld", dst,
		   strlen(unescaped));
		is(memcmp(dst, unescaped, dst_size), 0,
		   "unescape: '%s' == '%s'", unescaped, dst);
		free(dst);
	}
	check_plan();
	footer();
}

int
main(void)
{
	plan(11);
	test_copy_sample();
	test_copy_empty();
	test_move_sample();
	test_move_empty();
	test_addr_is_equal();
	test_string_uri_with_query_params_parse();
	test_string_uri_set_with_query_params_parse();
	test_invalid_string_uri_set();
	test_escape();
	test_unescape();
	test_unescape_special_cases();
	return check_plan();
}
