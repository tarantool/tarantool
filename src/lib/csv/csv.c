#include "csv.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

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
	csv->csv_delim= ',';
	csv->csv_quote = '\"';
	csv->csv_realloc = realloc;
}

void
csv_destroy(struct csv *csv)
{
	if(csv->buf) {
		csv->csv_realloc(csv->buf, 0);
		csv->buf = 0;
	}
}

int csv_isvalid(struct csv *csv)
{
	if (csv->prevsymb == csv->csv_quote) {
		csv->state = csv->state == CSV_BUF_IN_QUOTES ? CSV_BUF_OUT_OF_QUOTES : CSV_BUF_IN_QUOTES;
		csv->prevsymb = ' ';
	}
	csv->csv_invalid = csv->state  == CSV_BUF_IN_QUOTES;
	return !csv->csv_invalid;
}
void
csv_setopt(struct csv *csv, int opt, ...)
{
	va_list args;
	va_start(args, opt);
	switch(opt) {
	case CSV_OPT_DELIMITER:
		csv->csv_delim = va_arg(args, int);
		break;
	case CSV_OPT_QUOTE:
		csv->csv_quote = va_arg(args, int);
		break;
	case CSV_OPT_REALLOC:
		csv->csv_realloc = va_arg(args, void*);
		break;
	case CSV_OPT_EMIT_FIELD:
		csv->emit_field = va_arg(args, void*);
	case CSV_OPT_EMIT_ROW:
		csv->emit_row = va_arg(args, void*);
	case CSV_OPT_CTX:
		csv->emit_ctx = va_arg(args, void*);
	}
	va_end(args);
}

const char *
csv_parse_common(struct csv *csv, const char *s, const char *end, int onlyfirst)
{
	if (end - s == 0)
		return 0;
	assert(end - s > 0);
	if (csv->emit_field == NULL)
		csv->emit_field = csv_emit_field_empty;
	if (csv->emit_row == NULL)
		csv->emit_row = csv_emit_row_empty;
	const char *p = s;

	 while(p != end) {
		int isendl = (*p == '\n' || *p == '\r');

		if (csv->buf == 0 || (csv->bufp && csv->buf_len < csv->bufp - csv->buf + 1)) {
			csv->buf_len = (int)((csv->bufp - csv->buf + 1) * csv_buf_expand_factor + 1);
			char *new_buf = csv->csv_realloc(csv->buf, csv->buf_len);
			csv->bufp = csv->bufp - csv->buf + new_buf;
			csv->buf = new_buf;
		}

		if (csv->prevsymb == csv->csv_quote) {
			if(*p == csv->csv_quote) {
				*csv->bufp++ = csv->csv_quote;
				csv->prevsymb = ' ';
				p++;
				continue;
			}
			csv->state = csv->state == CSV_BUF_IN_QUOTES ? CSV_BUF_OUT_OF_QUOTES : CSV_BUF_IN_QUOTES;
		}
		if (isendl && csv->state != CSV_BUF_IN_QUOTES &&
				*p != csv->prevsymb && (csv->prevsymb  == '\n' || csv->prevsymb == '\r')) {
			csv->prevsymb = 0;
			p++;
			continue;
		}
		csv->prevsymb = *p;
		switch(csv->state) {
		case CSV_LEADING_SPACES: //leading spaces
			csv->bufp = csv->buf;
			if (*p != ' ') {
				csv->state = CSV_BUF_OUT_OF_QUOTES;
			}
			else break;
		case CSV_BUF_OUT_OF_QUOTES:
			if (isendl || *p == csv->csv_delim) {
				csv->state = CSV_LEADING_SPACES;
				csv->bufp -= csv->csv_ending_spaces;
				if(onlyfirst) {
					csv->state = CSV_NEWLINE;
					return p;
				} else {
					csv->emit_field(csv->emit_ctx, csv->buf, csv->bufp);
				}

				csv->bufp = csv->buf;
			} else if (*p != csv->csv_quote) {
				*csv->bufp++ = *p;
			}

			if (*p == ' ') {
				csv->csv_ending_spaces++;
			} else {
				csv->csv_ending_spaces = 0;
			}
			break;
		case CSV_BUF_IN_QUOTES:
			if (*p != csv->csv_quote) {
				*csv->bufp++ = *p;
			}
			break;
		case CSV_NEWLINE:
			csv->state = CSV_LEADING_SPACES;
			break;
		}
		if (isendl && csv->state != CSV_BUF_IN_QUOTES) {
			assert(csv->state == CSV_LEADING_SPACES);
			csv->bufp = 0;
			if(onlyfirst) {
				if(p + 1 == end)
					return 0;
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
	csv_parse_common(csv, s, end, 0);
}

void
csv_finish_parsing(struct csv *csv)
{
	if (csv_isvalid(csv)){
		if (csv->bufp) {
				csv->bufp -= csv->csv_ending_spaces;
				csv->emit_field(csv->emit_ctx, csv->buf, csv->bufp);
				csv->emit_row(csv->emit_ctx);
		}
		if (csv->buf)
			csv->csv_realloc(csv->buf, 0);
		csv->bufp = 0;
		csv->buf = 0;
		csv->buf_len = 0;
	}
}


void
csv_iter_create(struct csv_iterator *it, struct csv *csv)
{
	memset(it, 0, sizeof(struct csv_iterator));
	it->csv = csv;
}

int
csv_next(struct csv_iterator *it) {
	it->field = 0;
	it->field_len = 0;
	if(it->buf_begin == 0)
		return CSV_IT_NEEDMORE;
	if(it->buf_begin == it->buf_end) {
		if (!it->csv->csv_invalid && !csv_isvalid(it->csv)) {
			it->csv->csv_invalid = 1;
			it->csv->csv_realloc(it->csv->buf, 0);
			it->csv->buf = 0;
			it->csv->bufp = 0;
			it->csv->buf_len = 0;
			return CSV_IT_ERROR;
		}
		if(it->csv->bufp == 0) {
			return CSV_IT_EOF;
		}
		if(it->csv->state != CSV_END_OF_INPUT) {
			it->csv->state = CSV_END_OF_INPUT;
			it->csv->bufp -= it->csv->csv_ending_spaces;
			it->field = it->csv->buf;
			it->field_len = it->csv->bufp - it->csv->buf;
			it->csv->bufp = it->csv->buf;
			return CSV_IT_OK;
		} else {
			it->csv->csv_realloc(it->csv->buf, 0);
			it->csv->buf = 0;
			it->csv->bufp = 0;
			it->csv->buf_len = 0;
			return CSV_IT_EOL;
		}
	}
	const char *tail = csv_parse_common(it->csv, it->buf_begin, it->buf_end, 1);
	it->buf_begin = tail;
	if(it->csv->bufp == 0 && it->csv->prevsymb)
		return CSV_IT_EOL;
	if(tail == it->buf_end)
		return CSV_IT_NEEDMORE;
	it->field = it->csv->buf;
	it->field_len = it->csv->bufp - it->csv->buf;
	return CSV_IT_OK;
}

void
csv_feed(struct csv_iterator *it, const char *str)
{
	it->buf_begin = str;
	it->buf_end = str + strlen(str);
}

int
csv_escape_field(struct csv *csv, const char *field, size_t field_len, char *dst, size_t buf_size)
{
	char *p = dst;
	int inquotes = 0;
	if(memchr(field, csv->csv_delim, field_len) || memchr(field, '\n', field_len) || memchr(field, '\r', field_len)) {
		inquotes = 1;
		*p++ = csv->csv_quote;
	}
	while(*field) {
		if(*field == csv->csv_quote) {
			if(p - dst >= buf_size)
				return -1;
			*p++ = csv->csv_quote;
		}
		if(p - dst >= buf_size)
			return -1;
		*p++ = *field++;
	}
	if(inquotes) {
		if(p - dst >= buf_size)
			return -1;
		*p++ = csv->csv_quote;
	}
	*p = 0;
	return p - dst;
}
