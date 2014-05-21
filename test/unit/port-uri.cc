#include "test.h"
#include <port-uri.h>
#include <string.h>

#define PLAN	42


int
main(void)
{
	plan(PLAN);
	note("Errors");
	{
		struct port_uri uri;

		is(port_uri_parse(&uri, NULL), 0, "parse NULL string");
		is(uri.error.schema, 1, "error.schema = true");
		port_uri_destroy(&uri);

		is(port_uri_parse(NULL, "test"), 0, "NULL structure");

		is(port_uri_parse(&uri, ""), 0, "empty string");
		is(uri.error.schema, 1, "error.schema = true");
		port_uri_destroy(&uri);

		is(port_uri_parse(&uri, "schema"), 0, "no host");
		is(uri.error.schema, 1, "error.schema = true");
		port_uri_destroy(&uri);

		is(port_uri_parse(&uri, "schema:"), 0, "invalid schema sep");
		is(uri.error.schema, 1, "error.schema = true");
		port_uri_destroy(&uri);

		is(port_uri_parse(&uri, "schema:/"), 0, "invalid schema sep");
		is(uri.error.schema, 1, "error.schema = true");
		port_uri_destroy(&uri);

		is(port_uri_parse(&uri, "schema://"), 0, "zero host len");
		is(uri.error.schema, 0, "error.schema = false");
		is(uri.error.host, 1, "error.host = true");
		port_uri_destroy(&uri);

		is(port_uri_parse(&uri, "://abc"), 0, "zero schema len");
		is(uri.error.schema, 1, "error.schema = true");
		port_uri_destroy(&uri);

		is(port_uri_parse(&uri, "schema://host:1a"), 0, "wrong port");
		is(uri.error.schema, 0, "error.schema = false");
		is(uri.error.host, 0, "error.host = false");
		is(uri.error.port, 1, "error.port = true");
		port_uri_destroy(&uri);
	}

	note("Parser");
	{
		struct port_uri uri;
		isnt(port_uri_parse(&uri, "schema://host"), 0, "valid uri");
		is(strcmp(uri.schema, "schema"), 0, "schema");
		is(strcmp(uri.host, "host"), 0, "host");
		is(uri.port, 0, "port");
		is(uri.is.tcp, 0, "tcp flag");
		is(uri.is.unix, 0, "unix flag");
		port_uri_destroy(&uri);


		isnt(port_uri_parse(&uri, "tcp://host:"), 0, "full uri");
		is(strcmp(uri.schema, "tcp"), 0, "schema");
		is(strcmp(uri.host, "host"), 0, "host");
		is(uri.port, 0, "port");
		is(uri.is.tcp, 1, "tcp flag");
		is(uri.is.unix, 0, "unix flag");
		port_uri_destroy(&uri);

		isnt(port_uri_parse(&uri, "schema://host:123"), 0, "full uri");
		is(strcmp(uri.schema, "schema"), 0, "schema");
		is(strcmp(uri.host, "host"), 0, "host");
		is(uri.port, 123, "port");
		port_uri_destroy(&uri);

		isnt(port_uri_parse(&uri, "unix:///path/to"), 0, "unix socket");
		is(strcmp(uri.schema, "unix"), 0, "schema");
		is(strcmp(uri.host, "/path/to"), 0, "host");
		is(uri.port, 0, "port");
		is(uri.is.tcp, 0, "tcp flag");
		is(uri.is.unix, 1, "unix flag");
		port_uri_destroy(&uri);
	}

	return check_plan();
}
