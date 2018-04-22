/*
 * Copyright 2010-2018 Tarantool AUTHORS: please see AUTHORS file.
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

#include "path.h"
#include <ctype.h>
#include <stdbool.h>
#include <unicode/uchar.h>
#include <unicode/utf8.h>
#include "trivia/util.h"

/**
 * Read a single symbol from a string starting from an offset.
 * @param parser JSON path parser.
 * @param[out] UChar32 Read symbol.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_read_symbol(struct json_path_parser *parser, UChar32 *out)
{
	if (parser->offset == parser->src_len) {
		*out = U_SENTINEL;
		return parser->symbol_count + 1;
	}
	U8_NEXT(parser->src, parser->offset, parser->src_len, *out);
	if (*out == U_SENTINEL)
		return parser->symbol_count + 1;
	++parser->symbol_count;
	return 0;
}

/**
 * Rollback one symbol offset.
 * @param parser JSON path parser.
 * @param offset Offset to the previous symbol.
 */
static inline void
json_revert_symbol(struct json_path_parser *parser, int offset)
{
	parser->offset = offset;
	--parser->symbol_count;
}

/** Fast forward when it is known that a symbol is 1-byte char. */
static inline void
json_skip_char(struct json_path_parser *parser)
{
	++parser->offset;
	++parser->symbol_count;
}

/** Get a current symbol as a 1-byte char. */
static inline char
json_current_char(const struct json_path_parser *parser)
{
	return *(parser->src + parser->offset);
}

/**
 * Parse string identifier in quotes. Parser either stops right
 * after the closing quote, or returns an error position.
 * @param parser JSON path parser.
 * @param[out] node JSON node to store result.
 * @param quote_type Quote by that a string must be terminated.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_parse_string(struct json_path_parser *parser, struct json_path_node *node,
		  UChar32 quote_type)
{
	assert(parser->offset < parser->src_len);
	assert(quote_type == json_current_char(parser));
	/* The first symbol is always char  - ' or ". */
	json_skip_char(parser);
	int str_offset = parser->offset;
	UChar32 c;
	int rc;
	while ((rc = json_read_symbol(parser, &c)) == 0) {
		if (c == quote_type) {
			int len = parser->offset - str_offset - 1;
			if (len == 0)
				return parser->symbol_count;
			node->type = JSON_PATH_STR;
			node->str = parser->src + str_offset;
			node->len = len;
			return 0;
		}
	}
	return rc;
}

/**
 * Parse digit sequence into integer until non-digit is met.
 * Parser stops right after the last digit.
 * @param parser JSON parser.
 * @param[out] node JSON node to store result.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_parse_integer(struct json_path_parser *parser, struct json_path_node *node)
{
	const char *end = parser->src + parser->src_len;
	const char *pos = parser->src + parser->offset;
	assert(pos < end);
	int len = 0;
	uint64_t value = 0;
	char c = *pos;
	if (! isdigit(c))
		return parser->symbol_count + 1;
	do {
		value = value * 10 + c - (int)'0';
		++len;
	} while (++pos < end && isdigit((c = *pos)));
	parser->offset += len;
	parser->symbol_count += len;
	node->type = JSON_PATH_NUM;
	node->num = value;
	return 0;
}

/**
 * Check that a symbol can be part of a JSON path not inside
 * ["..."].
 */
static inline bool
json_is_valid_identifier_symbol(UChar32 c)
{
	return u_isUAlphabetic(c) || c == (UChar32)'_' || u_isdigit(c);
}

/**
 * Parse identifier out of quotes. It can contain only alphas,
 * digits and underscores. And can not contain digit at the first
 * position. Parser is stoped right after the last non-digit,
 * non-alpha and non-underscore symbol.
 * @param parser JSON parser.
 * @param[out] node JSON node to store result.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_parse_identifier(struct json_path_parser *parser,
		      struct json_path_node *node)
{
	assert(parser->offset < parser->src_len);
	int str_offset = parser->offset;
	UChar32 c;
	int rc = json_read_symbol(parser, &c);
	if (rc != 0)
		return rc;
	/* First symbol can not be digit. */
	if (!u_isalpha(c) && c != (UChar32)'_')
		return parser->symbol_count;
	int last_offset = parser->offset;
	while ((rc = json_read_symbol(parser, &c)) == 0) {
		if (! json_is_valid_identifier_symbol(c)) {
			json_revert_symbol(parser, last_offset);
			break;
		}
		last_offset = parser->offset;
	}
	node->type = JSON_PATH_STR;
	node->str = parser->src + str_offset;
	node->len = parser->offset - str_offset;
	return 0;
}

int
json_path_next(struct json_path_parser *parser, struct json_path_node *node)
{
	if (parser->offset == parser->src_len) {
		node->type = JSON_PATH_END;
		return 0;
	}
	UChar32 c;
	int last_offset = parser->offset;
	int rc = json_read_symbol(parser, &c);
	if (rc != 0)
		return rc;
	switch(c) {
	case (UChar32)'[':
		/* Error for '[\0'. */
		if (parser->offset == parser->src_len)
			return parser->symbol_count;
		c = json_current_char(parser);
		if (c == '"' || c == '\'')
			rc = json_parse_string(parser, node, c);
		else
			rc = json_parse_integer(parser, node);
		if (rc != 0)
			return rc;
		/*
		 * Expression, started from [ must be finished
		 * with ] regardless of its type.
		 */
		if (parser->offset == parser->src_len ||
		    json_current_char(parser) != ']')
			return parser->symbol_count + 1;
		/* Skip ] - one byte char. */
		json_skip_char(parser);
		return 0;
	case (UChar32)'.':
		if (parser->offset == parser->src_len)
			return parser->symbol_count + 1;
		return json_parse_identifier(parser, node);
	default:
		json_revert_symbol(parser, last_offset);
		return json_parse_identifier(parser, node);
	}
}
