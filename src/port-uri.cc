/*
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


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "port-uri.h"


struct port_uri *
port_uri_parse(struct port_uri *uri, const char *p)
{
	enum {
		schema,
		schema_sep,
		host,
		port
	};
	int state = schema;
	const char *begin;

	if (uri) {
		memset(uri, 0, sizeof(*uri));
	} else {
		uri = (struct port_uri *)calloc(1, sizeof(*uri));
		if (!uri)
			return NULL;
		uri->is.alloc = 1;
	}
	if (!p) {
		uri->error.schema = 1;
		goto ERROR;
	}


	for (begin = p; *p; p++) {
		switch(state) {
			case schema:
				if (*p != ':')
					break;
				if (p == begin) {
					uri->error.schema = 1;
					goto ERROR;
				}
				state = schema_sep;
				uri->schema = strndup(begin, p - begin);
				begin = p;
				break;
			case schema_sep:
				switch(p - begin) {
					case 0:
						if (*p != ':')
							goto ERROR;
						break;
					case 1:
						if (*p != '/')
							goto ERROR;
						break;
					case 2:
						if (*p != '/')
							goto ERROR;
						state = host;
						begin = p + 1;
						break;
					default:
						uri->error.schema = 1;
						goto ERROR;
				}
			case host:
				if (*p != ':')
					break;
				uri->host = strndup(begin, p - begin);
				begin = p + 1;
				state = port;
				break;

			case port:
				if (*p < '0' || *p > '9') {
					uri->error.port = 1;
					goto ERROR;
				}
				uri->port *= 10;
				uri->port += (int)(*p - '0');
				break;

		}

	}

	switch(state) {
		case schema:
		case schema_sep:
			uri->error.schema = 1;
			goto ERROR;
		case host:
			if (p <= begin) {
				uri->error.host = 1;
				goto ERROR;
			}
			if (p - begin <= 1) {
				uri->error.host = 1;
				goto ERROR;
			}
			uri->host = strndup(begin, p - begin);
			break;
		case port:
			break;
		default:
			assert(false);
	}

	if (strcmp(uri->schema, "tcp") == 0)
		uri->is.tcp = 1;
	else if (strcmp(uri->schema, "unix") == 0)
		uri->is.unix = 1;

	return uri;

	ERROR:
		port_uri_destroy(uri);
		return NULL;
}


void
port_uri_destroy(struct port_uri *uri)
{
	if (!uri)
		return;
	free(uri->host);
	uri->host = NULL;

	free(uri->schema);
	uri->schema = NULL;


	if (uri->is.alloc) {
		free(uri);
	}
}
