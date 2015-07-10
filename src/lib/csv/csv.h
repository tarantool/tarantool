#ifndef TARANTOOL_CSV_H_INCLUDED
#define TARANTOOL_CSV_H_INCLUDED

#include<stdio.h>

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
	char csv_delim;
	char csv_quote;

	int csv_invalid;

	void *(*csv_realloc)(void*, size_t);

	int state;
	char *buf;
	char *bufp;
	size_t buf_len;
};

//parser options
enum {
	CSV_OPT_DELIMITER,
	CSV_OPT_QUOTE,
	CSV_OPT_REALLOC,
	CSV_OPT_EMIT_FIELD,
	CSV_OPT_EMIT_ROW,
	CSV_OPT_CTX
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
 * @return the pointer to the unprocessed tail
 */
const char *
csv_parse_chunk(struct csv *csv, const char *s, const char *end);

/**
 * Parses the entire buffer, assuming there is no next
 * chunk. Emits the tail of the buffer as a field even
 * if there is no newline at end of input.
 */
void
csv_parse(struct csv *csv, const char *s, const char *end);

/**
 * Format variadic arguments and print them into
 * a stream, adding CSV markup.
 */
int
csv_snprintf(struct csv *csv, FILE *f, const char *format, ...);

/**
 * if quote not closed returns 0
 */
int
csv_isvalid(struct csv *csv);

#if defined(__cplusplus)
}
#endif /* extern "C" */
#endif
