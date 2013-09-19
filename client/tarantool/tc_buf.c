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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>

#include "client/tarantool/tc_buf.h"

/* Strip trailing ws from (char*) */
size_t strip_end_ws(char *str) {
	size_t last = 0;
	for (size_t i = 0; str[i] != 0; ++i) {
		if (!isspace(str[i]))
			last = i + 1;
	}
	str[last] = '\0';
	return last;
}

/* Init membuf */
int tc_buf(struct tc_buf *buf) {
	buf->size = TC_BUF_INIT_SIZE;
	buf->used = 0;
	buf->data = (char *)malloc(buf->size);
	if (buf->data == NULL) {
		return -1;
	}
	return 0;
}

/* Append len bytes of memory from str pointed memory */
int tc_buf_append(struct tc_buf *buf, void *str, size_t len) {
	if (buf->size - buf->used < len) {
		if (buf->size < len) {
			buf->size = len;
		}
		buf->size *= TC_BUF_MULTIPLIER;
		char *nd = (char *)realloc(buf->data, buf->size);
		if (nd == NULL)
			return -1;
		buf->data = nd;
	}
	memcpy(buf->data + buf->used, str, len);
	buf->used += len;
	return 0;
}

/* Remove last "num" symbols */
size_t tc_buf_delete(struct tc_buf *buf, size_t num) {
	if (buf->used > num) {
		buf->used -= num;
	} else {
		num = buf->used;
		buf->used = 0;
	}
	return num;
}

inline int tc_buf_isempty(struct tc_buf *buf) {
	return (buf->used == 0);
}

inline void tc_buf_clear(struct tc_buf *buf) {
	buf->used = 0;
}

/* Free membuffer */
void tc_buf_free(struct tc_buf *buf) {
	if (buf->data)
		free(buf->data);
}

/* Init buffer as STR */
int tc_buf_str(struct tc_buf *buf) {
	if (tc_buf(buf))
		return -1;
	return tc_buf_append(buf, (void *)"\0", 1);
}

/* Append str to STR */
int tc_buf_str_append(struct tc_buf *buf, char *str, size_t len) {
	tc_buf_delete(buf, 1);
	if (tc_buf_append(buf, (void *)str, len))
		return -1;
	if (tc_buf_append(buf, (void *)"\0", 1))
		return -1;
	return 0;
}

/* Remove last num symbols from STR */
size_t tc_buf_str_delete(struct tc_buf *buf, size_t len) {
	size_t ret = tc_buf_delete(buf, len + 1); /* Remove '\0' + len */
	if (tc_buf_append(buf, (void *)"\0", 1))
		return 0;
	return ret;
}

/*
 * Make admin command from multiline command
 * and delete delimiter (last num bytes)
 */
void tc_buf_cmdfy(struct tc_buf *buf, size_t num) {
	tc_buf_str_delete(buf, num);
	for (int i = 0; i < buf->used; ++i) {
		if (buf->data[i] == '\n')
			buf->data[i] = ' ';
	}
}

/* Remove trailing ws from STR */
int tc_buf_str_stripws(struct tc_buf *buf) {
	if (buf->data) {
		buf->used = 1 + strip_end_ws(buf->data);
		return 0;
	}
	return -1;
}

inline int tc_buf_str_isempty(struct tc_buf *buf) {
	return (buf->used == 1 ? 1 : 0) || (tc_buf_isempty(buf));
}
