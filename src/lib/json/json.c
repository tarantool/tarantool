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

#include "json.h"
#include <ctype.h>
#include <stdbool.h>
#include <unicode/uchar.h>
#include <unicode/utf8.h>
#include "trivia/util.h"

/**
 * Read a single symbol from a string starting from an offset.
 * @param lexer JSON path lexer.
 * @param[out] UChar32 Read symbol.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_read_symbol(struct json_lexer *lexer, UChar32 *out)
{
	if (lexer->offset == lexer->src_len) {
		*out = U_SENTINEL;
		return lexer->symbol_count + 1;
	}
	U8_NEXT(lexer->src, lexer->offset, lexer->src_len, *out);
	if (*out == U_SENTINEL)
		return lexer->symbol_count + 1;
	++lexer->symbol_count;
	return 0;
}

/**
 * Rollback one symbol offset.
 * @param lexer JSON path lexer.
 * @param offset Offset to the previous symbol.
 */
static inline void
json_revert_symbol(struct json_lexer *lexer, int offset)
{
	lexer->offset = offset;
	--lexer->symbol_count;
}

/** Fast forward when it is known that a symbol is 1-byte char. */
static inline void
json_skip_char(struct json_lexer *lexer)
{
	++lexer->offset;
	++lexer->symbol_count;
}

/** Get a current symbol as a 1-byte char. */
static inline char
json_current_char(const struct json_lexer *lexer)
{
	return *(lexer->src + lexer->offset);
}

/**
 * Parse string identifier in quotes. Lexer either stops right
 * after the closing quote, or returns an error position.
 * @param lexer JSON path lexer.
 * @param[out] token JSON token to store result.
 * @param quote_type Quote by that a string must be terminated.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_parse_string(struct json_lexer *lexer, struct json_token *token,
		  UChar32 quote_type)
{
	assert(lexer->offset < lexer->src_len);
	assert(quote_type == json_current_char(lexer));
	/* The first symbol is always char  - ' or ". */
	json_skip_char(lexer);
	int str_offset = lexer->offset;
	UChar32 c;
	int rc;
	while ((rc = json_read_symbol(lexer, &c)) == 0) {
		if (c == quote_type) {
			int len = lexer->offset - str_offset - 1;
			if (len == 0)
				return lexer->symbol_count;
			token->type = JSON_TOKEN_STR;
			token->str = lexer->src + str_offset;
			token->len = len;
			return 0;
		}
	}
	return rc;
}

/**
 * Parse digit sequence into integer until non-digit is met.
 * Lexer stops right after the last digit.
 * @param lexer JSON lexer.
 * @param[out] token JSON token to store result.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_parse_integer(struct json_lexer *lexer, struct json_token *token)
{
	const char *end = lexer->src + lexer->src_len;
	const char *pos = lexer->src + lexer->offset;
	assert(pos < end);
	int len = 0;
	uint64_t value = 0;
	char c = *pos;
	if (! isdigit(c))
		return lexer->symbol_count + 1;
	do {
		value = value * 10 + c - (int)'0';
		++len;
	} while (++pos < end && isdigit((c = *pos)));
	lexer->offset += len;
	lexer->symbol_count += len;
	token->type = JSON_TOKEN_NUM;
	token->num = value;
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
 * position. Lexer is stoped right after the last non-digit,
 * non-alpha and non-underscore symbol.
 * @param lexer JSON lexer.
 * @param[out] token JSON token to store result.
 *
 * @retval   0 Success.
 * @retval > 0 1-based position of a syntax error.
 */
static inline int
json_parse_identifier(struct json_lexer *lexer, struct json_token *token)
{
	assert(lexer->offset < lexer->src_len);
	int str_offset = lexer->offset;
	UChar32 c;
	int rc = json_read_symbol(lexer, &c);
	if (rc != 0)
		return rc;
	/* First symbol can not be digit. */
	if (!u_isalpha(c) && c != (UChar32)'_')
		return lexer->symbol_count;
	int last_offset = lexer->offset;
	while ((rc = json_read_symbol(lexer, &c)) == 0) {
		if (! json_is_valid_identifier_symbol(c)) {
			json_revert_symbol(lexer, last_offset);
			break;
		}
		last_offset = lexer->offset;
	}
	token->type = JSON_TOKEN_STR;
	token->str = lexer->src + str_offset;
	token->len = lexer->offset - str_offset;
	return 0;
}

int
json_lexer_next_token(struct json_lexer *lexer, struct json_token *token)
{
	if (lexer->offset == lexer->src_len) {
		token->type = JSON_TOKEN_END;
		return 0;
	}
	UChar32 c;
	int last_offset = lexer->offset;
	int rc = json_read_symbol(lexer, &c);
	if (rc != 0)
		return rc;
	switch(c) {
	case (UChar32)'[':
		/* Error for '[\0'. */
		if (lexer->offset == lexer->src_len)
			return lexer->symbol_count;
		c = json_current_char(lexer);
		if (c == '"' || c == '\'')
			rc = json_parse_string(lexer, token, c);
		else
			rc = json_parse_integer(lexer, token);
		if (rc != 0)
			return rc;
		/*
		 * Expression, started from [ must be finished
		 * with ] regardless of its type.
		 */
		if (lexer->offset == lexer->src_len ||
		    json_current_char(lexer) != ']')
			return lexer->symbol_count + 1;
		/* Skip ] - one byte char. */
		json_skip_char(lexer);
		return 0;
	case (UChar32)'.':
		if (lexer->offset == lexer->src_len)
			return lexer->symbol_count + 1;
		return json_parse_identifier(lexer, token);
	default:
		json_revert_symbol(lexer, last_offset);
		return json_parse_identifier(lexer, token);
	}
}
