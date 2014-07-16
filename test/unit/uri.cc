#include "test.h"
#include <uri.h>
#include <string.h>

#define PLAN	40

int
main(void)
{
	plan(PLAN);

	struct uri uri;

	is(uri_parse(&uri, "/file"), 0, "/file");
	is(strcmp(uri_to_string(&uri), "unix:///file"), 0,
				"to_string");
	is(strcmp(uri.schema, "unix"), 0, "unix://");


	is(uri_parse(&uri, "unix://file"), 0, "unix://file");
	is(strcmp(uri_to_string(&uri), "unix://file"), 0,
							"to_string");
	is(strcmp(uri.schema, "unix"), 0, "unix://");


	is(uri_parse(&uri, "123"), 0, "123");
	is(strcmp(uri.schema, "tcp"), 0, "tcp://");
	is(strcmp(uri_to_string(&uri), "0.0.0.0:123"), 0,
		"to_string");



	is(uri_parse(&uri, "http://11.2.3.4:123"), 0,
		"http://11.2.3.4:123");
	is(strcmp(uri.schema, "http"), 0, "http://");
	is(strcmp(uri_to_string(&uri), "http://11.2.3.4:123"), 0,
		"to_string");

	is(uri_parse(&uri, "http://[::fFff:11.2.3.4]:123"), 0,
		"http://11.2.3.4:123");
	is(strcmp(uri.schema, "http"), 0, "http://");
	is(strcmp(uri_to_string(&uri), "http://11.2.3.4:123"), 0,
		"to_string");
	is(uri_parse(&uri, "http://user:pass@127.0.0.1:12345"), 0,
		"http://user:pass@127.0.0.1:12345");
	is(strcmp(uri.login, "user"), 0, "user");
	is(strcmp(uri.password, "pass"), 0, "pass");
	is(strcmp(uri.schema, "http"), 0, "http");


	is(uri_parse(&uri, "schema://[2001:0db8:11a3:09d7::1]"),
	   0, "schema://[2001:0db8:11a3:09d7::1]");
	is(strcmp(uri_to_string(&uri),
		"schema://[2001:db8:11a3:9d7::1]:0"), 0, "to_string");


	isnt(uri_parse(&uri, "schema://[2001::11a3:09d7::1]"),
	     0, "invalid schema://[2001::11a3:09d7::1]");




	is(uri_parse(&uri, "128.0.0.1"), 0, "127.0.0.1");
	is(strcmp(uri_to_string(&uri), "128.0.0.1:0"), 0,
		"to_string");

	is(uri_parse(&uri, "128.0.0.1:22"), 0, "127.0.0.1:22");
	is(strcmp(uri_to_string(&uri), "128.0.0.1:22"), 0,
		"to_string");

	is(uri_parse(&uri, "login:password@127.0.0.1"), 0,
	   "login:password@127.0.0.1");
	is(strcmp(uri.login, "login"), 0, "login");
	is(strcmp(uri.password, "password"), 0, "password");
	is(strcmp(uri.schema, "tcp"), 0, "default schema");

	is(uri_parse(&uri, "unix://login:password@/path/to"), 0,
	   "unix://login:password@/path/to");
	is(strcmp(uri.login, "login"), 0, "login");
	is(strcmp(uri.password, "password"), 0, "password");
	is(strcmp(uri.schema, "unix"), 0, "unix");
	is(strcmp(uri_to_string(&uri), "unix:///path/to"), 0,
		"to_string");

	isnt(uri_parse(&uri, "tcp://abc.cde:90"), 0, "invalid uri");

	is(uri_parse(&uri, "http://127.0.0.1:http"), 0,
	   "valid uri");
	is(strcmp(uri_to_string(&uri), "http://127.0.0.1:80"), 0,
	   "service to port number");

	is(uri_parse(&uri, "mail.ru:https"), 0, "valid uri");

	isnt(strstr(uri_to_string(&uri), ":443"), 0,
		"service converted");

	return check_plan();
}
