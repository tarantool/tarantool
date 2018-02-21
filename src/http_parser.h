/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef TARANTOOL_HTTP_PARSER_H
#define TARANTOOL_HTTP_PARSER_H

#define HEADER_LEN 32

enum {
	HTTP_PARSE_OK,
	HTTP_PARSE_DONE,
	HTTP_PARSE_INVALID
};

struct http_parser {
	char *header_value_start;
	char *header_value_end;

	int http_major;
	int http_minor;

	char header_name[HEADER_LEN];
	int header_name_idx;
};

/*
 * @brief Parse line containing http header info
 * @param parser object
 * @param bufp pointer to buffer with data
 * @param end_buf
 * @return	HTTP_DONE - line was parsed
 * 		HTTP_OK - header was read
 * 		HTTP_PARSE_INVALID - error during parsing
 */
int
http_parse_header_line(struct http_parser *parser, char **bufp, const char *end_buf);

#endif //TARANTOOL_HTTP_PARSER_H
