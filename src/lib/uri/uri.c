/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "uri.h"
#include "uri_parser.h"
#include "trivia/util.h"

#define XSTRDUP(s)      (s != NULL ? xstrdup(s) : NULL)
#define XSTRNDUP(s, n)  (s != NULL ? xstrndup(s, n) : NULL)

/**
 * WARNING: this structure is exposed in Lua via FFI (see src/lua/uri.lua): any
 * change  must be reflected in `ffi.cdef`.
 */
struct uri_param {
	/** Name of URI parameter. */
	char *name;
	/**
	 * Capacity of URI parameter values dynamic array (used for exponential
	 * reallocation).
	 */
	int values_capacity;
	/** Count of values for this parameter. */
	int value_count;
	/** Array of values for this parameter. */
	char **values;
};

/**
 * A helper table to convert a hex character to its integer value.
 *
 * '0', ..., '9' ->  0, ...,  9
 * 'A', ..., 'F' -> 10, ..., 15
 * 'a', ..., 'f' -> 10, ..., 15
 * Otherwise     -> -1
 */
static int xdigit2int[] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

/**
 * Find URI parameter by its @a name in @a uri structure.
 */
static struct uri_param *
uri_find_param(const struct uri *uri, const char *name)
{
	for (int i = 0; i < uri->param_count; i++) {
		if (strcmp(uri->params[i].name, name) == 0)
			return &uri->params[i];
	}
	return NULL;
}

/**
 * Add new @a value to URI @a param.
 */
static void
uri_param_add_value(struct uri_param *param, const char *value)
{
	assert(param->value_count <= param->values_capacity);
	if (param->value_count == param->values_capacity) {
		param->values_capacity = param->values_capacity == 0 ? 16 :
					 param->values_capacity * 2;
		size_t size = param->values_capacity * sizeof(char *);
		param->values = xrealloc(param->values, size);
	}
	param->values[param->value_count++] = xstrdup(value);
}

/**
 * Destroy URI @a param and free all associated resources.
 */
static void
uri_param_destroy(struct uri_param *param)
{
	for (int i = 0; i < param->value_count; i++)
		free(param->values[i]);
	free(param->values);
	free(param->name);
	TRASH(param);
}

/**
 * Create new URI @a param, with given @a name.
 */
static void
uri_param_create(struct uri_param *param, const char *name)
{
	param->name = xstrdup(name);
	param->values = NULL;
	param->value_count = 0;
	param->values_capacity = 0;
}

/**
 * Copy URI parameter @a src to @a dst.
 */
static void
uri_param_copy(struct uri_param *dst, const struct uri_param *src)
{
	dst->name = xstrdup(src->name);
	dst->values_capacity = src->values_capacity;
	dst->value_count = src->value_count;
	dst->values = (src->values_capacity == 0 ? NULL :
		       xmalloc(src->values_capacity * sizeof(*dst->values)));
	for (int i = 0; i < src->value_count; i++)
		dst->values[i] = xstrdup(src->values[i]);
}

void
uri_remove_param(struct uri *uri, const char *name)
{
	struct uri_param *param = uri_find_param(uri, name);
	if (param == NULL)
		return;
	int idx = param - uri->params;
	uri_param_destroy(param);
	for (int i = idx; i < uri->param_count - 1; i++)
		uri->params[i] = uri->params[i + 1];
	uri->param_count--;
}

void
uri_add_param(struct uri *uri, const char *name, const char *value)
{
	struct uri_param *param = uri_find_param(uri, name);
	if (param == NULL) {
		assert(uri->param_count <= uri->params_capacity);
		if (uri->param_count == uri->params_capacity) {
			uri->params_capacity = uri->params_capacity == 0 ? 16 :
					       uri->params_capacity * 2;
			size_t size =
				uri->params_capacity * sizeof(struct uri_param);
			uri->params = xrealloc(uri->params, size);
		}
		param = &uri->params[uri->param_count++];
		uri_param_create(param, name);
	}
	if (value != NULL)
		uri_param_add_value(param, value);
}

/**
 * Destroy all @a uri parameters and free all resources associated
 * with them.
 */
static void
uri_destroy_params(struct uri *uri)
{
	for (int i = 0; i < uri->param_count; i++)
		uri_param_destroy(&uri->params[i]);
	free(uri->params);
}

/**
 * Create parameters for @a uri from @a query string. Expected @a
 * query format is a string which contains parameters separated by '&'.
 * For example: "backlog=10&transport=tls". Also @a query can contain
 * several values for one parameter separated by '&'. For example:
 * "backlog=10&backlog=30".
 */
static void
uri_create_params(struct uri *uri, const char *query)
{
	char *copy = xstrdup(query);
	char *saveptr, *optstr = strtok_r(copy, "&", &saveptr);
	while (optstr != NULL) {
		char *value = NULL, *name = optstr;
		char *delim = strchr(optstr, '=');
		if (delim != NULL) {
			*delim = '\0';
			value = delim + 1;
		}
		optstr = strtok_r(NULL, "&", &saveptr);
		/* Ignore params with empty name */
		if (*name == 0)
			continue;
		uri_add_param(uri, name, value);
	}
	free(copy);
}

void
uri_copy(struct uri *dst, const struct uri *src)
{
	dst->scheme = XSTRDUP(src->scheme);
	dst->login = XSTRDUP(src->login);
	dst->password = XSTRDUP(src->password);
	dst->host = XSTRDUP(src->host);
	dst->service = XSTRDUP(src->service);
	dst->path = XSTRDUP(src->path);
	dst->query = XSTRDUP(src->query);
	dst->fragment = XSTRDUP(src->fragment);
	dst->host_hint = src->host_hint;
	dst->params_capacity = src->params_capacity;
	dst->param_count = src->param_count;
	dst->params = (src->params_capacity == 0 ? NULL :
		       xmalloc(src->params_capacity * sizeof(*dst->params)));
	for (int i = 0; i < src->param_count; i++)
		uri_param_copy(&dst->params[i], &src->params[i]);
}

void
uri_move(struct uri *dst, struct uri *src)
{
	*dst = *src;
	uri_create(src, NULL);
}

void
uri_destroy(struct uri *uri)
{
	uri_destroy_params(uri);
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
	if (uri->query != NULL)
		uri_create_params(uri, uri->query);
	return 0;
}

static int
uri_format_param(char *str, int len, const struct uri_param *param)
{
	int total = 0;
	if (param->value_count == 0) {
		SNPRINT(total, snprintf, str, len, "%s", param->name);
		return total;
	}
	for (int i = 0; i < param->value_count; i++) {
		SNPRINT(total, snprintf, str, len, "%s=%s",
			param->name, param->values[i]);
		if (i != param->value_count - 1)
			SNPRINT(total, snprintf, str, len, "&");
	}
	return total;
}

static int
uri_format_params(char *str, int len, const struct uri *uri)
{
	if (uri->param_count == 0)
		return 0;
	int total = 0;
	SNPRINT(total, snprintf, str, len, "?");
	for (int i = 0; i < uri->param_count; i++) {
		SNPRINT(total, uri_format_param, str, len, &uri->params[i]);
		if (i != uri->param_count - 1)
			SNPRINT(total, snprintf, str, len, "&");
	}
	return total;
}

int
uri_format(char *str, int len, const struct uri *uri, bool write_password)
{
	int total = 0;
	if (uri->scheme != NULL)
		SNPRINT(total, snprintf, str, len, "%s://", uri->scheme);
	if (uri->login != NULL) {
		SNPRINT(total, snprintf, str, len, "%s", uri->login);
		if (uri->password != NULL && write_password) {
			SNPRINT(total, snprintf, str, len, ":%s",
				uri->password);
		}
		SNPRINT(total, snprintf, str, len, "@");
	}
	if (uri->host != NULL) {
		SNPRINT(total, snprintf, str, len, "%s", uri->host);
	}
	if (uri->service != NULL) {
		if (uri->host != NULL) {
			SNPRINT(total, snprintf, str, len, ":%s", uri->service);
		} else {
			SNPRINT(total, snprintf, str, len, "%s", uri->service);
		}
	}
	if (uri->path != NULL) {
		SNPRINT(total, snprintf, str, len, "%s", uri->path);
	}
	if (uri->params != NULL) {
		SNPRINT(total, uri_format_params, str, len, uri);
	}
	if (uri->fragment != NULL) {
		SNPRINT(total, snprintf, str, len, "#%s", uri->fragment);
	}
	return total;
}

const char *
uri_param(const struct uri *uri, const char *name, int idx)
{
	struct uri_param *param = uri_find_param(uri, name);
	assert(idx >= 0);
	if (param == NULL || idx >= param->value_count)
		return NULL;
	return param->values[idx];
}

int
uri_param_count(const struct uri *uri, const char *name)
{
	struct uri_param *param = uri_find_param(uri, name);
	return (param != NULL ? param->value_count : 0);
}

void
uri_set_add(struct uri_set *uri_set, struct uri *uri)
{
	size_t size = (uri_set->uri_count + 1) * sizeof(struct uri);
	uri_set->uris = xrealloc(uri_set->uris, size);
	uri_move(&uri_set->uris[uri_set->uri_count++], uri);
}

void
uri_set_destroy(struct uri_set *uri_set)
{
	for (int i = 0; i < uri_set->uri_count; i++)
		uri_destroy(&uri_set->uris[i]);
	free(uri_set->uris);
	TRASH(uri_set);
}

int
uri_set_create(struct uri_set *uri_set, const char *str)
{
	uri_set->uris = NULL;
	uri_set->uri_count = 0;
	if (str == NULL || *str == 0)
		return 0;
	char *copy = xstrdup(str);
	char *ptr = copy;
	char *delim = NULL;
	do {
		char *uristr = ptr;
		delim = strchr(ptr, ',');
		if (delim != NULL) {
			ptr = delim + 1;
			*delim = '\0';
			while (*ptr == ' ')
				ptr++;
		}
		struct uri uri;
		if (uri_create(&uri, uristr) != 0)
			goto fail;
		uri_set_add(uri_set, &uri);
		uri_destroy(&uri);
	} while (delim != NULL);
	free(copy);
	return 0;
fail:
	free(copy);
	uri_set_destroy(uri_set);
	uri_set->uris = NULL;
	uri_set->uri_count = 0;
	return -1;
}

void
uri_set_copy(struct uri_set *dst, const struct uri_set *src)
{
	dst->uri_count = src->uri_count;
	if (src->uris == NULL) {
		dst->uris = NULL;
		return;
	}
	dst->uris = xmalloc(sizeof(*dst->uris) * dst->uri_count);
	for (int i = 0; i < dst->uri_count; i++)
		uri_copy(&dst->uris[i], &src->uris[i]);
}

bool
uri_set_is_equal(const struct uri_set *a, const struct uri_set *b)
{
	if (a->uri_count != b->uri_count)
		return false;
	for (int i = 0; i < a->uri_count; i++) {
		if (!uri_is_equal(&a->uris[i], &b->uris[i]))
			return false;
	}
	return true;
}

/**
 * String percent-encoding.
 */
size_t
uri_escape(const char *src, size_t src_size, char *dst,
	   const unsigned char unreserved[256], bool encode_plus)
{
	int pos = 0;
	const char *hex = "0123456789ABCDEF";
	while (src_size--) {
		unsigned char ch = (unsigned char)*src;
		if ((ch == ' ') && encode_plus) {
			dst[pos++] = '+';
		} else if (!unreserved[(int)ch]) {
			dst[pos++] = '%';
			dst[pos++] = hex[ch >> 4];
			dst[pos++] = hex[ch & 15];
		} else {
			dst[pos++] = *src;
		}
		src++;
	}
	return (size_t)pos;
};

/**
 * String percent-decoding.
 */
size_t
uri_unescape(const char *src, size_t src_size, char *dst, bool decode_plus)
{
	char *dst_buf = dst;
	const char *src_end = src + src_size;
	for (const char *p = src; p < src_end; ++p) {
		if (*p == '%') {
			int hex_1 = -1;
			int hex_2 = -1;

			if (p + 2 < src_end) {
				hex_1 = xdigit2int[(unsigned char)p[1]];
				hex_2 = xdigit2int[(unsigned char)p[2]];
			}

			if (hex_1 >= 0 && hex_2 >= 0) {
				*dst_buf++ = hex_1 << 4 | hex_2;
				p += 2;
			} else {
				*dst_buf++ = '%';
			}
		} else if (decode_plus && *p == '+') {
			*dst_buf++ = ' ';
		} else {
			*dst_buf++ = *p;
		}
	}
	return (size_t)(dst_buf - dst);
}

/* Compare two strings which might be NULL. */
static bool
fields_are_equal(const char *a, const char *b)
{
	if ((a == NULL) != (b == NULL))
		return false;
	if (a == NULL)
		return true;
	return strcmp(a, b) == 0;
}

bool
uri_addr_is_equal(const struct uri *a, const struct uri *b)
{
	/*
	 * Either service or path will be NULL depending on whether this is a
	 * unix socket or not.
	 */
	return fields_are_equal(a->host, b->host) &&
	       fields_are_equal(a->path, b->path) &&
	       fields_are_equal(a->service, b->service);
}

bool
uri_is_equal(const struct uri *a, const struct uri *b)
{
	if (!fields_are_equal(a->scheme, b->scheme) ||
	    !fields_are_equal(a->login, b->login) ||
	    !fields_are_equal(a->password, b->password) ||
	    !fields_are_equal(a->fragment, b->fragment) ||
	    !uri_addr_is_equal(a, b))
		return false;
	if (a->param_count != b->param_count)
		return false;
	for (int i = 0; i < a->param_count; i++) {
		struct uri_param *a_param = &a->params[i];
		struct uri_param *b_param = uri_find_param(b, a_param->name);
		if (b_param == NULL ||
		    a_param->value_count != b_param->value_count)
			return false;
		for (int j = 0; j < a_param->value_count; j++) {
			if (strcmp(a_param->values[j], b_param->values[j]) != 0)
				return false;
		}
	}
	return true;
}

bool
uri_is_nil(const struct uri *uri)
{
	/*
	 * Check only these 3 fields, because without them a uri doesn't make
	 * sense. But technically this will give some false positives. For
	 * example, for uris with non-empty fragment or query.
	 */
	return uri->host == NULL && uri->path == NULL && uri->service == NULL;
}
