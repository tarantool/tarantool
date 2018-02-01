/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "uri.h"
#include <trivia/util.h> /* SNPRINT */
#include <string.h>
#include <stdio.h> /* snprintf */
int
uri_parse(struct uri *uri, const char *p)
{
	const char *pe = p + strlen(p);
	const char *eof = pe;
	int cs;
	memset(uri, 0, sizeof(*uri));

	if (p == pe)
		return -1;

	const char *s = NULL, *login = NULL, *scheme = NULL;
	size_t login_len = 0, scheme_len = 0;

	%%{
		machine uri;
		write data;

		#
		# Line by line translation of RFC3986
		# http://tools.ietf.org/html/rfc3986#appendix-A
		#

		gen_delims = (":" | "/" | "?" | "#" | "[" | "]" | "@");
		sub_delims = ("!" | "$" | "&" | "'" | "(" | ")"
				 | "*" | "+" | "," | ";" | "=");

		reserved = (gen_delims | sub_delims);
		unreserved = alpha | digit | "-" | "_" | "~" | ".";
		pct_encoded = ("%%" | ("%" xdigit xdigit?)
				   | ("%u" xdigit xdigit xdigit xdigit));

		pchar_nc = unreserved | pct_encoded | sub_delims | "@";
		pchar = pchar_nc | ":" | "|";

		query = (pchar | "/" | "?")*
			>{ s = p; }
			%{ uri->query = s; uri->query_len = p - s; };

		fragment = (pchar | "/" | "?")*
			>{ s = p; }
			%{ uri->fragment = s; uri->fragment_len = p - s; };

		segment = pchar*;
		segment_nz = pchar+;
		segment_nz_nc = pchar_nc+;

		path_abempty  = ( "/" segment )*;
		path_absolute = ("/" ( segment_nz ( "/" segment )* )?);
		path_noscheme = (segment_nz_nc ( "/" segment )*);
		path_rootless = (pchar_nc ( "/" segment )*);
		path_empty    = "";

		path = path_abempty    # begins with "/" or is empty
		     | path_absolute   # begins with "/" but not "//"
		     | path_noscheme   # begins with a non-colon segment
		     | path_rootless   # begins with a segment
		     | path_empty;     # zero characters

		socket_path_absolute = ("/" segment_nz_nc)+;
		socket_path_relative = ("." socket_path_absolute);
		socket_path = socket_path_absolute
		     | socket_path_relative;

		reg_name = (unreserved | pct_encoded | sub_delims)+
			>{ s = p; }
			%{ uri->host = s; uri->host_len = p - s;};

		hex1_4 = ([0-9a-fa-f]{1,4});

		ip4addr = ((digit{1,3}) (("." digit{1,3}){3}));
		ip4 = ip4addr
			>{ s = p; }
			%{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; };

		ip6	= ("[" (
				((hex1_4?) ((":" (hex1_4?)){1,8})) |
				("::" [ff][ff][ff][ff] ":" ip4addr))
			>{ s = p; }
			%{ uri->host = s; uri->host_len = p - s;
				   uri->host_hint = 2; }
			   "]");

		action unix{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
		# Non-standard: "unix/" support
		unix = ("unix/:" %{ s = p;} socket_path) %unix;

		service = (digit+ | alpha*)
			>{ s = p; }
			%{ uri->service = s; uri->service_len = p - s; };

		host =  (ip4 | ip6 | reg_name);

		login = (unreserved | pct_encoded | sub_delims )+
			>{ s = p; }
			%{ login = s; login_len = p - s; };

		password = (unreserved | pct_encoded | sub_delims )*
			>{ s = p; }
			%{ uri->password = s; uri->password_len = p - s; };

		# Non-standard: split userinfo to login and password
		userinfo = login (":" password)?
			%{ uri->login = login; uri->login_len = login_len; };

		# Non-standard: use service instead of port here + support unix
		authority = (userinfo "@")? ((host (":" service)?) | (unix  ":"));

		scheme = alpha > { s = p; }
			 (alpha | digit | "+" | "-" | ".")*
			%{scheme = s; scheme_len = p - s; };

		# relative_part = "//" authority > { s  =  p } path_abempty |
		#	 path_absolute |
		#	 path_noscheme |
		#	 path_empty;

		# Non-standard: allow URI without scheme
		hier_part_noscheme = (((userinfo "@" unix) %{ s = p; }) |
				((authority %{ s = p; } path_abempty?
					  | path_absolute?
					  | path_rootless?
					  | path_empty?
				) %{ uri->path = s; uri->path_len = p - s; }) |
				unix);

		hier_part = "//"
			>{ uri->scheme = scheme; uri->scheme_len = scheme_len;}
			hier_part_noscheme;

		# relative_ref = relative_part ("?" >{ s = p; } query)?
		#	("#" >{ s = p; } fragment)?;

		# absolute_URI = scheme ":" hier_part ("?" >{ s = p; } query);

		PORT = digit+
			>{ uri->service = p; }
			%{ uri->service_len = p - uri->service;
			   uri->host = NULL; uri->host_len = 0; };

		PATH = ((userinfo "@")? %{ s = p; } path_absolute %unix);

		URI = ((scheme ":" hier_part) | hier_part_noscheme)
			("?" >{ s = p; } query)? ("#" >{ s = p; } fragment)?;

		# Non-RFC: support port and absolute path
		main := URI | PORT | PATH;

		write init;
		write exec;
	}%%

	if (uri->path_len == 0)
		uri->path = NULL;
	if (uri->service_len == 0)
		uri->service = NULL;
	if (uri->service_len >= URI_MAXSERVICE)
		return -1;
	if (uri->host_len >= URI_MAXHOST)
		return -1;

	(void)uri_first_final;
	(void)uri_error;
	(void)uri_en_main;
	(void)eof;

	return cs >= uri_first_final ? 0 : -1;
}

int
uri_format(char *str, int len, const struct uri *uri, bool write_password)
{
	int total = 0;
	if (uri->scheme_len > 0) {
		SNPRINT(total, snprintf, str, len, "%.*s://",
			 (int)uri->scheme_len, uri->scheme);
	}
	if (uri->host_len > 0) {
		if (uri->login_len > 0) {
			SNPRINT(total, snprintf, str, len, "%.*s",
				(int)uri->login_len, uri->login);
			if (uri->password_len > 0 && write_password) {
				SNPRINT(total, snprintf, str, len, ":%.*s",
				        (int)uri->password_len,
					uri->password);
			}
			SNPRINT(total, snprintf, str, len, "@");
		}
		SNPRINT(total, snprintf, str, len, "%.*s",
			 (int)uri->host_len, uri->host);
		if (uri->service_len > 0) {
			SNPRINT(total, snprintf, str, len, ":%.*s",
				(int)uri->service_len, uri->service);
		}
	}
	if (uri->path_len > 0) {
		SNPRINT(total, snprintf, str, len, "%.*s",
			(int)uri->path_len, uri->path);
	}
	if (uri->query_len > 0) {
		SNPRINT(total, snprintf, str, len, "?%.*s",
			(int)uri->query_len, uri->query);
	}
	if (uri->fragment_len > 0) {
		SNPRINT(total, snprintf, str, len, "#%.*s",
			(int)uri->fragment_len, uri->fragment);
	}
	return total;
}

/* vim: set ft=ragel: */
