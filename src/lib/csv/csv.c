#include "csv.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
//parser states
enum {  CSV_LEADING_SPACES = 0,
	CSV_OUT_OF_QUOTES = 1,
	CSV_IN_QUOTES = 2,
	CSV_BUF_OUT_OF_QUOTES = 3,
	CSV_BUF_IN_QUOTES = 4
};

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
	(void)csv;
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
csv_parse_common(struct csv *csv, const char *s, const char *end, int endfile)
{
	if (end - s == 0)
		return s;
	assert(end - s > 0);
	if (csv->emit_field == NULL)
		csv->emit_field = csv_emit_field_empty;
	if (csv->emit_row == NULL)
		csv->emit_row = csv_emit_row_empty;
	const char *p = s;
	int state = 0;
	const char *field_begin;
	const char *field_end;
	char *buf = 0;
	size_t buf_len = 0;
	char *bufp = 0;
	const char *tail = s;
	do {
		int isendl = (*p == '\n' || *p == '\r');

		// in some cases buffer is not used
		if ((state == 3 || state == 4) && !bufp) {
			if (buf_len < end - p) {
				buf = csv->csv_realloc(buf, (end - p) * sizeof(char));
				buf_len = end - p;
			}
			memcpy(buf, field_begin, p - field_begin);
			bufp = buf + (p - field_begin);
		}
		switch(state) {
		case CSV_LEADING_SPACES: //leading spaces
			if (p == end || *p != ' ') {
				state = CSV_OUT_OF_QUOTES;
				field_begin = p;
			}
			else break;
		case CSV_OUT_OF_QUOTES: //common case without buffer
			if (isendl || *p == csv->csv_delim) {
				state = CSV_LEADING_SPACES;
				field_end = p - 1;
				while(field_end > field_begin && *field_end == ' ')
					field_end--;
				field_end++;
				csv->emit_field(csv->emit_ctx, field_begin, field_end);
				tail = p + 1;
			} else if (*p == csv->csv_quote) {
				if (p + 1 != end && *(p + 1) == csv->csv_quote) {
					p--;
					state = CSV_BUF_OUT_OF_QUOTES;
				} else {
					state = CSV_BUF_IN_QUOTES;
					if (field_begin == p) {
						field_begin = p + 1;
						state = CSV_IN_QUOTES;
					}
				}
			}
			break;
		case CSV_IN_QUOTES: //quote case without buffer
			if (*p == csv->csv_quote) {
				if (p + 1 != end && *(p + 1) == csv->csv_quote) {
					p--;
					state = CSV_BUF_IN_QUOTES;
				} else {
					field_end = p;
					p = p + 1;
					while (p < end && *p == ' ')
						p++;
					if (*p == csv->csv_delim || *p == '\n' || *p == '\r') {
						state = CSV_LEADING_SPACES;
						isendl = (*p == '\n' || *p == '\r');
						csv->emit_field(csv->emit_ctx, field_begin, field_end);
						tail = p + 1;
					} else {
						p = field_end - 1;
						state = CSV_BUF_IN_QUOTES;
					}
				}
			}
			break;
		case CSV_BUF_OUT_OF_QUOTES: //common case with buffer
			if (isendl || *p == csv->csv_delim) {
				state = CSV_LEADING_SPACES;
				bufp--;
				while (bufp > buf && *bufp == ' ')
					bufp--;
				bufp++;
				csv->emit_field(csv->emit_ctx, buf, bufp);
				tail = p + 1;
			} else if (*p == csv->csv_quote) {
				if (p + 1 != end && *(p + 1) == csv->csv_quote) {
					*bufp++ = csv->csv_quote;
					p++;
				} else {
					state = CSV_BUF_IN_QUOTES;
				}
			}  else {
				*bufp++ = *p;
			}
			break;
		case CSV_BUF_IN_QUOTES: //quote case with buffer
			if (*p == csv->csv_quote) {
				if (p + 1 != end && *(p + 1) == csv->csv_quote) {
					*bufp++ = csv->csv_quote;
					p++;
				} else {
					state = CSV_BUF_OUT_OF_QUOTES;
				}
			} else {
				*bufp++ = *p;
			}
			break;
		}
		if (isendl && state != CSV_IN_QUOTES && state != CSV_BUF_IN_QUOTES) {
			assert(state == CSV_LEADING_SPACES);
			bufp = 0;
			csv->emit_row(csv->emit_ctx);
			if (p != end && *p != *(p + 1) && (*(p + 1) == '\n' || *(p + 1) == '\r'))
				p++; //\r\n - is only 1 endl
			tail = p + 1;
		}
		p++;
	} while(p != end);

	if (endfile && *(end - 1) != '\n' && *(end - 1) != '\r') {
		if (state == 2 || state == 4) {
			csv->csv_invalid = 1;
		} else if (bufp) {
			bufp--;
			while(bufp > buf && *bufp == ' ')
				bufp--;
			bufp++;
			csv->emit_field(csv->emit_ctx, buf, bufp);
		} else {
			field_begin = tail;
			field_end = end - 1;
			while(field_end > field_begin && *field_end == ' ')
				field_end--;
			field_end++;
			csv->emit_field(csv->emit_ctx, field_begin, field_end);
		}
		csv->emit_row(csv->emit_ctx);
	}
	if (buf) {
		csv->csv_realloc(buf, 0);
		buf = 0;
	}
	return tail;
}


const char *
csv_parse_chunk(struct csv *csv, const char *s, const char *end) {
	return csv_parse_common(csv, s, end, 0);
}
void
csv_parse(struct csv *csv, const char *s, const char *end)
{
	csv_parse_common(csv, s, end, 1);
	return;
}
