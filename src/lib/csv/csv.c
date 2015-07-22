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

#include "csv.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>

static const double csv_buf_expand_factor = 2.0;

void csv_emit_row_empty(void *ctx)
{
	(void)ctx;
}

void csv_emit_field_empty(void *ctx, const char *field, const char *end)
{
	(void)ctx;
	(void)field;
	(void)end;
}

void
csv_create(struct csv *csv)
{
	memset(csv, 0, sizeof(struct csv));
	csv->delimiter= ',';
	csv->quote_char = '\"';
	csv->realloc = realloc;
	csv->emit_field = csv_emit_field_empty;
	csv->emit_row = csv_emit_row_empty;
}

void
csv_destroy(struct csv *csv)
{
	if (csv->buf) {
		csv->realloc(csv->buf, 0);
		csv->buf = NULL;
	}
}

int
csv_isvalid(struct csv *csv)
{
	if (csv->prev_symbol == csv->quote_char) {
		if (csv->state == CSV_IN_QUOTES)
			csv->state = CSV_OUT_OF_QUOTES;
		else
			csv->state = CSV_IN_QUOTES;
		csv->prev_symbol = ' ';
	}
	if (csv->error_status == CSV_ER_OK &&
			csv->state == CSV_IN_QUOTES)
		csv->error_status = CSV_ER_INVALID;
	return !csv->error_status;
}

int
csv_get_error_status(struct csv *csv)
{
	return csv->error_status;
}

void
csv_setopt(struct csv *csv, int opt, ...)
{
	va_list args;
	va_start(args, opt);
	switch(opt) {
	case CSV_OPT_DELIMITER:
		csv->delimiter = va_arg(args, int);
		break;
	case CSV_OPT_QUOTE:
		csv->quote_char = va_arg(args, int);
		break;
	case CSV_OPT_REALLOC:
		csv->realloc = va_arg(args, void* (*)(void*, size_t));
		break;
	case CSV_OPT_EMIT_FIELD:
		csv->emit_field = va_arg(args, csv_emit_field_t);
	case CSV_OPT_EMIT_ROW:
		csv->emit_row = va_arg(args, csv_emit_row_t);
	case CSV_OPT_EMIT_CTX:
		csv->emit_ctx = va_arg(args, void*);
	}
	va_end(args);
}

/**
  * both of methods (emitting and iterating) are implemening by one function
  * firstonly == true means iteration method.
  **/
const char *
csv_parse_impl(struct csv *csv, const char *s, const char *end, bool firstonly)
{
	if (end - s == 0)
		return NULL;
	assert(end - s > 0);
	assert(csv->emit_field);
	assert(csv->emit_row);
	const char *p = s;

	while (p != end) {
		bool is_line_end = (*p == '\n' || *p == '\r');
		//realloc buffer
		if (csv->buf == 0 ||
		    (csv->bufp && csv->buf_len < csv->bufp - csv->buf + 1)) {
			csv->buf_len = (int)((csv->bufp - csv->buf + 1) *
					     csv_buf_expand_factor + 1);
			char *new_buf = (char *) csv->realloc(csv->buf, csv->buf_len);
			if (new_buf == NULL) {
				csv->error_status = CSV_ER_MEMORY_ERROR;
				return NULL;
			}
			csv->bufp = csv->bufp - csv->buf + new_buf;
			csv->buf = new_buf;
		}
		/** parser should keep previous symbol, because of "" and \r\n
		 *  and to prevent additional states of FSM
		 */
		if (csv->prev_symbol == csv->quote_char) {
			//double-quote ""
			if (*p == csv->quote_char) {
				*csv->bufp++ = csv->quote_char;
				csv->prev_symbol = ' ';
				p++;
				continue;
			}
			//quote closing or opening
			if (csv->state == CSV_IN_QUOTES)
				csv->state = CSV_OUT_OF_QUOTES;
			else
				csv->state = CSV_IN_QUOTES;
		}
		//\r\n (or \n\r) linebreak, not in quotes
		if (is_line_end && csv->state != CSV_IN_QUOTES &&
		    *p != csv->prev_symbol &&
		    (csv->prev_symbol  == '\n' || csv->prev_symbol == '\r')
		    ) {
			csv->prev_symbol = 0;
			p++;
			continue;
		}
		csv->prev_symbol = *p;
		switch(csv->state) {
		case CSV_LEADING_SPACES:
			csv->bufp = csv->buf;
			if (*p != ' ') {
				csv->state = CSV_OUT_OF_QUOTES;
			}
			else
				break; //spaces passed, perform field at once
		case CSV_OUT_OF_QUOTES:
			//end of field
			if (is_line_end || *p == csv->delimiter) {
				csv->state = CSV_LEADING_SPACES;
				csv->bufp -= csv->ending_spaces;
				if (firstonly) {
					csv->state = CSV_NEWLINE;
					return p;
				} else {
					csv->emit_field(csv->emit_ctx,
							csv->buf, csv->bufp);
				}

				csv->bufp = csv->buf;
			} else if (*p != csv->quote_char) {
				*csv->bufp++ = *p;
			}

			if (*p == ' ') {
				csv->ending_spaces++;
			} else {
				csv->ending_spaces = 0;
			}
			break;
		case CSV_IN_QUOTES:
			if (*p != csv->quote_char) {
				*csv->bufp++ = *p;
			}
			break;
		case CSV_NEWLINE:
			csv->state = CSV_LEADING_SPACES;
			break;
		}
		if (is_line_end && csv->state != CSV_IN_QUOTES) {
			assert(csv->state == CSV_LEADING_SPACES);
			/** bufp == buf means empty field,
			  * but bufp == 0 means no field at the moment,
			  * it may be end of line or end of file
			  **/
			csv->bufp = 0;
			if (firstonly) {
				if (p + 1 == end)
					return NULL;
				else
					return p + 1;
			}
			else {
				csv->emit_row(csv->emit_ctx);
			}
		}
		p++;
	}
	return end;
}


void
csv_parse_chunk(struct csv *csv, const char *s, const char *end) {
	csv_parse_impl(csv, s, end, false);
}

void
csv_finish_parsing(struct csv *csv)
{
	if (csv_isvalid(csv)){
		if (csv->bufp) {
				csv->bufp -= csv->ending_spaces;
				csv->emit_field(csv->emit_ctx,
						csv->buf, csv->bufp);
				csv->emit_row(csv->emit_ctx);
		}
		if (csv->buf)
			csv->realloc(csv->buf, 0);
		csv->bufp = NULL;
		csv->buf = NULL;
		csv->buf_len = 0;
	}
}


void
csv_iterator_create(struct csv_iterator *it, struct csv *csv)
{
	memset(it, 0, sizeof(struct csv_iterator));
	it->csv = csv;
}

/**
 * next iteration step
 **/
int
csv_next(struct csv_iterator *it)
{
	it->field = NULL;
	it->field_len = 0;
	if (it->buf_begin == NULL) //buffer isn't set
		return CSV_IT_NEEDMORE;
	/**
	  * length of buffer is zero
	  * it means end of file, but if there is no \n
	  * function must emit last field, EOL and EOF.
	  **/
	if (it->buf_begin == it->buf_end) {
		/** bufp == buf means empty field,
		  * but bufp == 0 means no field at the moment, it may be
		  * end of line or end of file
		  **/
		if (it->csv->bufp == NULL) { //nothing to emit, end of file
			return CSV_IT_EOF;
		}
		if (!it->csv->error_status && !csv_isvalid(it->csv)) {
			it->csv->realloc(it->csv->buf, 0);
			it->csv->buf = NULL;
			it->csv->bufp = NULL;
			it->csv->buf_len = 0;
			return CSV_IT_ERROR;
		}

		if (it->csv->state != CSV_END_OF_LAST_LINE) { //last field
			it->csv->state = CSV_END_OF_LAST_LINE;
			it->csv->bufp -= it->csv->ending_spaces;
			it->field = it->csv->buf;
			it->field_len = it->csv->bufp - it->csv->buf;
			it->csv->bufp = it->csv->buf;
			return CSV_IT_OK;
		}
		if (it->csv->state == CSV_END_OF_LAST_LINE) { //last line
			it->csv->realloc(it->csv->buf, 0);
			it->csv->buf = NULL;
			it->csv->bufp = NULL;
			it->csv->buf_len = 0;
			return CSV_IT_EOL;
		}

	}
	const char *tail = csv_parse_impl(it->csv, it->buf_begin,
					  it->buf_end, true);

	if (csv_get_error_status(it->csv) == CSV_ER_MEMORY_ERROR)
		return CSV_IT_ERROR;

	it->buf_begin = tail;
	//bufp == NULL means end of line
	if (it->csv->bufp == NULL && it->csv->prev_symbol)
		return CSV_IT_EOL;

	if (tail == it->buf_end) //buffer is empty
		return CSV_IT_NEEDMORE;

	//return field via iterator structure
	it->field = it->csv->buf;
	it->field_len = it->csv->bufp - it->csv->buf;
	return CSV_IT_OK;
}

void
csv_feed(struct csv_iterator *it, const char *buf, size_t buf_len)
{
	it->buf_begin = buf;
	it->buf_end = buf + buf_len;
}

size_t
csv_escape_field(struct csv *csv, const char *field,
		 size_t field_len, char *dst, size_t buf_size)
{
	char *p = dst;
	int inquotes = 0;
	//surround quotes, only if there is delimiter \n or \r
	if (memchr(field, csv->delimiter, field_len) ||
	    memchr(field, '\n', field_len) ||
	    memchr(field, '\r', field_len)) {
		inquotes = 1;
		*p++ = csv->quote_char;
	}
	while (*field) {
		// double-quote ""
		if (*field == csv->quote_char) {
			assert(p - dst < buf_size);
			*p++ = csv->quote_char;
		}
		assert(p - dst < buf_size);
		*p++ = *field++;
	}
	//adds ending quote
	if (inquotes) {
		assert(p - dst < buf_size);
		*p++ = csv->quote_char;
	}
	*p = 0;
	return p - dst;
}
