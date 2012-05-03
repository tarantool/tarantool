#ifndef TNT_ITER_H_INCLUDED
#define TNT_ITER_H_INCLUDED

/*
 * Copyright (C) 2011 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* tuple iterator type */

enum tnt_iter_type {
	TNT_ITER_FIELD,
	TNT_ITER_LIST,
	TNT_ITER_STREAM
};

/* tuple field iterator */

struct tnt_iter_field {
	struct tnt_tuple *tu; /* tuple pointer */
	char *fld_ptr;        /* current iteration offset */
	char *fld_ptr_prev;   /* prev iteration offset */
	uint32_t fld_index;   /* current field index */
	uint32_t fld_size;    /* current field size */
	int fld_esize;        /* current field encoding size */
	char *fld_data;       /* current field data */
};

/* tuple field iterator accessors */

#define TNT_IFIELD(I)       (&(I)->data.field)
#define TNT_IFIELD_TUPLE(I) TNT_IFIELD(I)->tu
#define TNT_IFIELD_IDX(I)   TNT_IFIELD(I)->fld_index
#define TNT_IFIELD_DATA(I)  TNT_IFIELD(I)->fld_data
#define TNT_IFIELD_SIZE(I)  TNT_IFIELD(I)->fld_size

/* list iterator */

struct tnt_iter_list {
	struct tnt_list *l;   /* list pointer */
	struct tnt_tuple *tu; /* current tuple pointer */
	uint32_t tu_index;    /* current tuple index */
};

/* list iterator accessors */

#define TNT_ILIST(I)        (&(I)->data.list)
#define TNT_ILIST_TUPLE(I)  TNT_ILIST(I)->tu
#define TNT_ILIST_INDEX(I)  TNT_ILIST(I)->tu_index

/* stream iterator */

struct tnt_iter_stream {
	struct tnt_stream *s; /* stream pointer */
	struct tnt_reply r;   /* current reply */
};

/* stream iterator accessors */

#define TNT_ISTREAM(I)        (&(I)->data.stream)
#define TNT_ISTREAM_REPLY(I)  &TNT_ISTREAM(I)->r

enum tnt_iter_status {
	TNT_ITER_OK,
	TNT_ITER_FAIL
};

/* common iterator object */

struct tnt_iter {
	enum tnt_iter_type type;
	enum tnt_iter_status status;
	int alloc;
	/* interface callbacks */
	int  (*next)(struct tnt_iter *iter);
	void (*rewind)(struct tnt_iter *iter);
	void (*free)(struct tnt_iter *iter);
	/* iterator data */
	union {
		struct tnt_iter_field field;
		struct tnt_iter_list list;
		struct tnt_iter_stream stream;
	} data;
};

struct tnt_iter *tnt_iter(struct tnt_iter *i, struct tnt_tuple *t);
struct tnt_iter *tnt_iter_list(struct tnt_iter *i, struct tnt_list *l);
struct tnt_iter *tnt_iter_stream(struct tnt_iter *i, struct tnt_stream *s);

void tnt_iter_free(struct tnt_iter *i);

int tnt_next(struct tnt_iter *i);
void tnt_rewind(struct tnt_iter *i);

struct tnt_iter *tnt_field(struct tnt_iter *i, struct tnt_tuple *t, uint32_t index);

#endif /* TNT_ITER_H_INCLUDED */
