#ifndef TARANTOOL_LIB_CSV_CSV_H_INCLUDED
#define TARANTOOL_LIB_CSV_CSV_H_INCLUDED
/*
 * Copyright 2010-2016 Tarantool AUTHORS: please see AUTHORS file.
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
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef void (*csv_emit_row_t)(void *ctx);
typedef void (*csv_emit_field_t)(void *ctx, const char *field, const char *end);

struct csv
{
	void *emit_ctx;
	csv_emit_row_t emit_row;
	csv_emit_field_t emit_field;
	char delimiter;
	char quote_char;

	char prev_symbol;
	int error_status;
	int ending_spaces;

	void *(*realloc)(void*, size_t);

	int state;
	char *buf;
	char *bufp;
	size_t buf_len;
};

enum csv_parser_option {
	CSV_OPT_DELIMITER,
	CSV_OPT_QUOTE,
	CSV_OPT_REALLOC,
	CSV_OPT_EMIT_FIELD,
	CSV_OPT_EMIT_ROW,
	CSV_OPT_EMIT_CTX
};

enum csv_iteraion_state {
	CSV_IT_OK,
	CSV_IT_EOL,
	CSV_IT_NEEDMORE,
	CSV_IT_EOF,
	CSV_IT_ERROR
};

enum csv_parser_state {
	CSV_LEADING_SPACES,
	CSV_OUT_OF_QUOTES,
	CSV_IN_QUOTES,
	CSV_QUOTE_OPENING,
	CSV_QUOTE_CLOSING,
	CSV_LINE_BREAKING,
	CSV_NEWFIELD,
	CSV_END_OF_LAST_LINE
};

enum csv_error_status {
	CSV_ER_OK,
	CSV_ER_INVALID,
	CSV_ER_MEMORY_ERROR
};

void
csv_create(struct csv *csv);

void
csv_destroy(struct csv *csv);

/**
 * Set a parser option.
 */
void
csv_setopt(struct csv *csv, int opt, ...);

/**
 * Parse input and call emit_row/emit_line.
 * Save tail to inside buffer,
 * next call will concatenate tail and string from args
 */
void
csv_parse_chunk(struct csv *csv, const char *s, const char *end);

/**
 * emits all remaining symbols from buffer
 */
void
csv_finish_parsing(struct csv *csv);

/**
 * @return 0 is ok
 */
int
csv_get_error_status(struct csv *csv);

/**
 * @brief The csv_iterator struct allows iterate field by field through csv
 */
struct csv_iterator {
	struct csv *csv;

	const char *buf_begin; //input buffer
	const char *buf_end;

	const char *field;    //output buffer
	size_t field_len;
};

void
csv_iterator_create(struct csv_iterator *it, struct csv *csv);
/**
 * Receives next element from csv
 * element is field or end of line
 * @return iteration state
 */
int
csv_next(struct csv_iterator *);

/**
 * @brief csv_feed delivers buffer to iterator
 * empty buffer means end of iteration
 */
void
csv_feed(struct csv_iterator *it, const char *buf, size_t buf_len);

/**
 * @brief csv_escape_field prepares field to out in file.
 * Adds pair quote and if there is comma or linebreak in field, adds surrounding quotes.
 * At worst escaped field will 2 times more symbols than input field.
 * @return length of escaped field or -1 if not enough space in buffer.
 */
size_t
csv_escape_field(struct csv *csv, const char *field, size_t field_len, char *dst, size_t dst_size);


static inline const char *
csv_iterator_get_field(struct csv_iterator *it)
{
	return it->field;
}

static inline size_t
csv_iterator_get_field_len(struct csv_iterator *it)
{
	return it->field_len;
}
#if defined(__cplusplus)
}
#endif /* extern "C" */
#endif /* TARANTOOL_LIB_CSV_CSV_H_INCLUDED */

