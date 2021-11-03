/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "uri.h"
#include "uri_parser.h"
#include "trivia/util.h"

#define XSTRNDUP(s, n)  (s != NULL ? xstrndup(s, n) : NULL)

void
uri_destroy(struct uri *uri)
{
	free(uri->scheme);
	free(uri->login);
	free(uri->password);
	free(uri->host);
	free(uri->service);
	free(uri->path);
	free(uri->query);
	free(uri->fragment);
	TRASH(uri);
}

int
uri_create(struct uri *uri, const char *str)
{
	memset(uri, 0, sizeof(struct uri));
	if (str == NULL)
		return 0;
	struct uri_raw uri_raw;
	if (uri_raw_parse(&uri_raw, str) != 0)
		return -1;
	uri->scheme = XSTRNDUP(uri_raw.scheme, uri_raw.scheme_len);
	uri->login = XSTRNDUP(uri_raw.login, uri_raw.login_len);
	uri->password = XSTRNDUP(uri_raw.password, uri_raw.password_len);
	uri->host = XSTRNDUP(uri_raw.host, uri_raw.host_len);
	uri->service = XSTRNDUP(uri_raw.service, uri_raw.service_len);
	uri->path = XSTRNDUP(uri_raw.path, uri_raw.path_len);
	uri->query = XSTRNDUP(uri_raw.query, uri_raw.query_len);
	uri->fragment = XSTRNDUP(uri_raw.fragment, uri_raw.fragment_len);
	uri->host_hint = uri_raw.host_hint;
	return 0;
}

int
uri_format(char *str, int len, const struct uri *uri, bool write_password)
{
	int total = 0;
	if (uri->scheme != NULL)
		SNPRINT(total, snprintf, str, len, "%s://", uri->scheme);
	if (uri->host != NULL) {
		if (uri->login != NULL) {
			SNPRINT(total, snprintf, str, len, "%s", uri->login);
			if (uri->password != NULL && write_password) {
				SNPRINT(total, snprintf, str, len, ":%s",
					uri->password);
			}
			SNPRINT(total, snprintf, str, len, "@");
		}
		SNPRINT(total, snprintf, str, len, "%s", uri->host);
		if (uri->service != 0) {
			SNPRINT(total, snprintf, str, len, ":%s", uri->service);
		}
	}
	if (uri->path != NULL) {
		SNPRINT(total, snprintf, str, len, "%s", uri->path);
	}
	if (uri->query != NULL) {
		SNPRINT(total, snprintf, str, len, "?%s", uri->query);
	}
	if (uri->fragment != NULL) {
		SNPRINT(total, snprintf, str, len, "#%s", uri->fragment);
	}
	return total;
}
