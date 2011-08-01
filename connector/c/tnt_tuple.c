
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <tnt_queue.h>
#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_leb128.h>
#include <tnt_tuple.h>

int
tnt_tuple_init(struct tnt_tuple *tuple, unsigned int fields)
{
	tuple->fields = tnt_mem_alloc(fields *
		sizeof(struct tnt_tuple_field));
	if (tuple->fields == NULL)
		return -1;
	memset(tuple->fields, 0,
		sizeof(struct tnt_tuple_field) * fields);
	tuple->count = fields;
	tuple->size_enc = 4; /* cardinality */
	return 0;
}

void
tnt_tuple_free(struct tnt_tuple *tuple)
{
	unsigned int i;
	for (i = 0 ; i < tuple->count ; i++) {
		if (tuple->fields[i].data)
			tnt_mem_free(tuple->fields[i].data);
	}
	tnt_mem_free(tuple->fields);
}

int
tnt_tuple_set(struct tnt_tuple *tuple,
	      unsigned int field, char *data, unsigned int size)
{
	if (field >= tuple->count)
		return -1;
	char *p = tnt_mem_alloc(size);
	if (p == NULL)
		return -1;
	memcpy(p, data, size);

	tuple->size_enc -= tuple->fields[field].size +
		tuple->fields[field].size_leb;

	tuple->fields[field].size = size;
	tuple->fields[field].size_leb = tnt_leb128_size(size);

	tuple->size_enc += tuple->fields[field].size +
		tuple->fields[field].size_leb;

	if (tuple->fields[field].data)
		tnt_mem_free(tuple->fields[field].data);
	tuple->fields[field].data = p;
	return 0;
}

struct tnt_tuple_field*
tnt_tuple_get(struct tnt_tuple *tuple, unsigned int field)
{
	if (field >= tuple->count)
		return NULL;
	return &tuple->fields[field];
}

enum tnt_error
tnt_tuple_pack(struct tnt_tuple *tuple, char **data, unsigned int *size)
{
	*size = tuple->size_enc;
	*data = tnt_mem_alloc(tuple->size_enc);
	if (*data == NULL)
		return TNT_EMEMORY;
	char *p = *data;
	memcpy(p, &tuple->count, 4);
	p += 4;

	unsigned int i;
	for (i = 0 ; i < tuple->count ; i++) {
		struct tnt_tuple_field *f = &tuple->fields[i];
		tnt_leb128_write(p, f->size);
		p += f->size_leb;
		if (f->data) {
			memcpy(p, f->data, f->size); 
			p += f->size;
		}
	}
	return TNT_EOK;
}

enum tnt_error
tnt_tuple_pack_to(struct tnt_tuple *tuple, char *dest)
{
	memcpy(dest, &tuple->count, 4);
	dest += 4;

	unsigned int i;
	for (i = 0 ; i < tuple->count ; i++) {
		struct tnt_tuple_field *f = &tuple->fields[i];
		tnt_leb128_write(dest, f->size);
		dest += f->size_leb;
		if (f->data) {
			memcpy(dest, f->data, f->size); 
			dest += f->size;
		}
	}
	return TNT_EOK;
}

void
tnt_tuples_init(struct tnt_tuples *tuples)
{
	tuples->count = 0;
	STAILQ_INIT(&tuples->list);
}

void
tnt_tuples_free(struct tnt_tuples *tuples)
{
	struct tnt_tuple *t, *tnext;
	STAILQ_FOREACH_SAFE(t, &tuples->list, next, tnext) {
		tnt_tuple_free(t);
		tnt_mem_free(t);
	}
}

struct tnt_tuple*
tnt_tuples_add(struct tnt_tuples *tuples)
{
	struct tnt_tuple *t =
		tnt_mem_alloc(sizeof(struct tnt_tuple));
	if (t == NULL)
		return NULL;
	memset(t, 0, sizeof(struct tnt_tuple));
	tuples->count++;
	STAILQ_INSERT_TAIL(&tuples->list, t, next);
	return t;
}

enum tnt_error
tnt_tuples_pack(struct tnt_tuples *tuples, char **data, unsigned int *size)
{
	if (tuples->count == 0)
		return TNT_EEMPTY;
	*size = 4; /* count */

	struct tnt_tuple *t;
	STAILQ_FOREACH(t, &tuples->list, next)
		*size += t->size_enc;

	*data = tnt_mem_alloc(*size);
	if (*data == NULL)
		return TNT_EMEMORY;

	char *p = *data;
	memcpy(p, &tuples->count, 4);
	p += 4;

	STAILQ_FOREACH(t, &tuples->list, next) {
		enum tnt_error result = tnt_tuple_pack_to(t, p);
		if (result != TNT_EOK) {
			tnt_mem_free(*data);
			*data = NULL;
			*size = 0;
			return result;
		}
		p += t->size_enc;
	}
	return TNT_EOK;
}

enum tnt_error
tnt_tuples_unpack(struct tnt_tuples *tuples, char *data, unsigned int size)
{
	struct tnt_tuple *t = tnt_tuples_add(tuples);
	if (t == NULL)
		return TNT_EMEMORY;

	char *p = data;
	uint32_t i, c = *(uint32_t*)p;
	int off	= 4;
	p += 4;

	if (tnt_tuple_init(t, c) == -1) {
		STAILQ_REMOVE(&tuples->list, t, tnt_tuple, next);
		tnt_mem_free(t);
		return TNT_EMEMORY;
	}

	for (i = 0 ; i < c ; i++) {
		uint32_t s;
		int r = tnt_leb128_read(p, size - off, &s);
		if (r == -1) 
			return TNT_EPROTO;
		off += r, p += r;
		if (s > (uint32_t)(size - off))
			return TNT_EPROTO;
		enum tnt_error res = tnt_tuple_set(t, i, p, s);
		if ( res != TNT_EOK )
			return res;
		off += s, p+= s;
	}

	return TNT_EOK;
}
