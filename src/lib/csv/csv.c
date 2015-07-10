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
	const char *tail = s;


	do {
		int isendl = (*p == '\n' || *p == '\r');

		if (csv->buf_len < csv->bufp - csv->buf + 1) {
			csv->buf_len = (int)((csv->bufp - csv->buf + 1) * csv_buf_expand_factor + 1);
			char *new_buf = csv->csv_realloc(csv->buf, csv->buf_len);
			csv->bufp = csv->bufp - csv->buf + new_buf;
			csv->buf = new_buf;
		}

		switch(csv->state) {
		case CSV_LEADING_SPACES: //leading spaces
			if (p == end || *p != ' ') {
				csv->state = CSV_BUF_OUT_OF_QUOTES;
			}
			else break;
		case CSV_BUF_OUT_OF_QUOTES:
			if (isendl || *p == csv->csv_delim) {
				csv->state = CSV_LEADING_SPACES;
				size_t space_end = 1;
				while (csv->bufp - space_end > csv->buf && p - space_end > s &&
						*(p - space_end) == ' ')
					space_end++;
				space_end--;
				csv->bufp -= space_end;
				csv->emit_field(csv->emit_ctx, csv->buf, csv->bufp);
				tail = p + 1;
				csv->bufp = csv->buf;
			} else if (*p == csv->csv_quote) {
				if (p + 1 != end && *(p + 1) == csv->csv_quote) {
					*csv->bufp++ = csv->csv_quote;
					p++;
				} else {
					csv->state = CSV_BUF_IN_QUOTES;
				}
			}  else {
				*csv->bufp++ = *p;
			}
			break;
		case CSV_BUF_IN_QUOTES:
			if (*p == csv->csv_quote) {
				if (p + 1 != end && *(p + 1) == csv->csv_quote) {
					*csv->bufp++ = csv->csv_quote;
					p++;
				} else {
					csv->state = CSV_BUF_OUT_OF_QUOTES;
				}
			} else {
				*csv->bufp++ = *p;
			}
			break;
		}
		if (isendl && csv->state != CSV_IN_QUOTES && csv->state != CSV_BUF_IN_QUOTES) {
			assert(csv->state == CSV_LEADING_SPACES);
			csv->bufp = csv->buf;
			csv->emit_row(csv->emit_ctx);
			if (p != end - 1 && *p != *(p + 1) && (*(p + 1) == '\n' || *(p + 1) == '\r'))
				p++; //\r\n - is only 1 endl
			tail = p + 1;
		}
		p++;
	} while(p != end);

	if (endfile && *(end - 1) != '\n' && *(end - 1) != '\r') {
		if (csv->state == 2 || csv->state == 4) {
			csv->csv_invalid = 1;
		} else {
			size_t space_end = 1;
			while (p - space_end > s && *(p - space_end) == ' ')
				space_end++;
			space_end--;
			csv->bufp -= space_end;
			csv->emit_field(csv->emit_ctx, csv->buf, csv->bufp);
			csv->bufp = 0;
			csv->csv_realloc(csv->buf, 0);
			csv->buf = 0;
			csv->buf_len = 0;
		}
		csv->emit_row(csv->emit_ctx);
		return 0;
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
