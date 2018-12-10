#ifndef TARANTOOL_JSON_JSON_H_INCLUDED
#define TARANTOOL_JSON_JSON_H_INCLUDED
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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lexer for JSON paths:
 * <field>, <.field>, <[123]>, <['field']> and their combinations.
 */
struct json_lexer {
	/** Source string. */
	const char *src;
	/** Length of string. */
	int src_len;
	/** Current lexer's offset in bytes. */
	int offset;
	/** Current lexer's offset in symbols. */
	int symbol_count;
};

enum json_token_type {
	JSON_TOKEN_NUM,
	JSON_TOKEN_STR,
	/** Lexer reached end of path. */
	JSON_TOKEN_END,
};

/**
 * Element of a JSON path. It can be either string or number.
 * String idenfiers are in ["..."] and between dots. Numbers are
 * indexes in [...].
 */
struct json_token {
	enum json_token_type type;
	union {
		struct {
			/** String identifier. */
			const char *str;
			/** Length of @a str. */
			int len;
		};
		/** Index value. */
		int num;
	};
};

/**
 * Create @a lexer.
 * @param[out] lexer Lexer to create.
 * @param src Source string.
 * @param src_len Length of @a src.
 */
static inline void
json_lexer_create(struct json_lexer *lexer, const char *src, int src_len)
{
	lexer->src = src;
	lexer->src_len = src_len;
	lexer->offset = 0;
	lexer->symbol_count = 0;
}

/**
 * Get a next path token.
 * @param lexer Lexer.
 * @param[out] token Token to store parsed result.
 * @retval   0 Success. For result see @a token.str, token.len,
 *             token.num.
 * @retval > 0 Position of a syntax error. A position is 1-based
 *             and starts from a beginning of a source string.
 */
int
json_lexer_next_token(struct json_lexer *lexer, struct json_token *token);

#ifdef __cplusplus
}
#endif

#endif /* TARANTOOL_JSON_JSON_H_INCLUDED */
