#include "csv.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
    csv->csv_free = free;
}

void
csv_destroy(struct csv *csv)
{
    (void)csv;
}

void
csv_setopt(struct csv *csv, const char *optname, void *optval)
{
    (void)csv;
    (void)optname;
    (void)optval;
}

void
csv_resize_buf(struct csv *csv, size_t newsize)
{
    if(csv->buf_len < newsize) {
        csv->buf = csv->csv_realloc(csv->buf, newsize * sizeof(char));
        csv->buf_len = newsize;
    }
}

void
csv_parse(struct csv *csv, const char *s, const char *end)
{
    if(end - s == 0)
        return;
    assert(end - s > 0);
    if(csv->emit_field == NULL)
        csv->emit_field = csv_emit_field_empty;
    if(csv->emit_row == NULL)
        csv->emit_row = csv_emit_row_empty;
    const char *p = s;
    int state = 0;
    const char *field_begin;
    const char *field_end;
    char *bufp = 0;
    do {
        int isendl = (p == end && *(p - 1) != '\n' && *(p - 1) != '\r') || *p == '\n' || *p == '\r';
        // in some cases buffer is not used
        switch(state) {
        case 0: //leading spaces
            if(p == end || *p != ' ') {
                state = 1;
                field_begin = p;
            }
            else break;
        case 1: //common case without buffer
            if(isendl || *p == csv->csv_delim) {
                state = 0;
                for(field_end = p - 1; field_end > field_begin && *field_end == ' '; field_end--);
                field_end++;
                csv->emit_field(csv->emit_ctx, field_begin, field_end);
            } else if(*p == csv->csv_quote) {
                if(p + 1 != end && *(p + 1) == csv->csv_quote) {
                    p--;
                    state = 3;
                } else {
                    state = 4;
                    if(field_begin == p) {
                        field_begin = p + 1;
                        state = 2;
                    }
                }
            }
            break;
        case 2: //quote case without buffer
            if(isendl) { //quoting doesn't ends
                p--;
                isendl = 0;
                state = 1;
                break;
            }
            if(*p == csv->csv_quote) {
                if(p + 1 != end && *(p + 1) == csv->csv_quote) {
                    p--;
                    state = 4;
                } else {
                   field_end = p;
                   for(p++;p < end && *p == ' '; p++);
                   if(*p == csv->csv_delim || *p == '\n' || *p == '\r') {
                       state = 0;
                       isendl = (*p == '\n' || *p == '\r');
                       csv->emit_field(csv->emit_ctx, field_begin, field_end);
                   } else {
                       p = field_end - 1;
                       state = 4;
                   }
                }
            }
            break;
        case 3: //common case with buffer
            if(!bufp) {
                csv_resize_buf(csv, end - p);
                memcpy(csv->buf, field_begin, p - field_begin);
                bufp = csv->buf + (p - field_begin);
            }
            if(isendl || *p == csv->csv_delim) {
                state = 0;
                for(bufp--; bufp > csv->buf && *bufp == ' '; bufp--);
                bufp++;
                csv->emit_field(csv->emit_ctx, csv->buf, bufp);
            } else if(*p == csv->csv_quote) {
                if(p + 1 != end && *(p + 1) == csv->csv_quote) {
                    *bufp++ = csv->csv_quote;
                    p++;
                } else {
                    state = 4;
                }
            }  else {
                *bufp++ = *p;
            }
            break;
        case 4: //quote case with buffer
            if(!bufp) {
                csv_resize_buf(csv, end - p);
                memcpy(csv->buf, field_begin, p - field_begin);
                bufp = csv->buf + (p - field_begin);
            }
            if(isendl) { //quoting doesn't ends
                p--;
                isendl = 0;
                state = 3;
                break;
            }
            if(*p == csv->csv_quote) {
                if(p + 1 != end && *(p + 1) == csv->csv_quote) {
                    *bufp++ = csv->csv_quote;
                    p++;
                } else {
                    state = 3;
                }
            } else {
                *bufp++ = *p;
            }
            break;
        }
        if(isendl) {
            assert(state == 0);
            bufp = csv->buf;
            csv->emit_row(csv->emit_ctx);
            if(p != end && *p != *(p + 1) && (*(p + 1) == '\n' || *(p + 1) == '\r'))
                p++; //\r\n - is only 1 endl
        }
        p++;
    } while(p != end + 1);

    if(csv->buf) {
        free(csv->buf);
        csv->buf = 0;
    }
}


const char *
csv_parse_chunk(struct csv *csv, const char *s, const char *end)
{
    const char *p;
    for(p = end - 1; *p != '\n' && *p != '\r' && p >= s; p--);
    csv_parse(csv, s, p + 1);
    return p + 1;
}
