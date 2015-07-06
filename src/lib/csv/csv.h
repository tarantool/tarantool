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
    int csv_delim, csv_quote;

    char *buf;
    size_t buf_len;
    void *(*csv_realloc)(void*, size_t);
    void (*csv_free)(void*);
};

void
csv_create(struct csv *csv);

void
csv_destroy(struct csv *csv);

/**
 * Set a parser option.
 */
void
csv_setopt(struct csv *csv, const char *optname, void *optval);

/**
 * Parse input and call emit_row/emit_line.
 *
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

#if defined(__cplusplus)
}
#endif /* extern "C" */
#endif
