
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
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include <tarantool/tnt_mem.h>
#include <tarantool/tnt_enc.h>
#include <tarantool/tnt_tuple.h>

/*
 * tnt_tuple_init()
 *
 * initialize tuple;
 *
 * t - tuple pointer
*/
void tnt_tuple_init(struct tnt_tuple *t) {
	memset(t, 0, sizeof(struct tnt_tuple));
}

/*
 * tnt_tuple_free()
 *
 * free tuple;
 *
 * t - tuple pointer
*/
void tnt_tuple_free(struct tnt_tuple *t) {
	if (t->data)
		tnt_mem_free(t->data);
	t->size = 0;
	t->cardinality = 0;
	t->data = NULL;
	if (t->alloc)
		tnt_mem_free(t);
}

/*
 * tnt_tuple_add()
 *
 * add field to existing tuple;
 * create new tuple and add field;
 * create new tuple;
 *
 * t    - tuple pointer, maybe NULL
 * data - tuple field data pointer, maybe NULL
 * size - tuple field data size, maybe zero
 * 
 * if tuple pointer is NULL, then new tuple will be created. 
 * If tuple pointer is NULL and size is zero, then only
 * new empty tuple will be created.
 * if tuple field data is NULL, then new field would be
 * created, but no data copied to it.
 *
 * returns tuple pointer, or NULL on error.
*/
struct tnt_tuple*
tnt_tuple_add(struct tnt_tuple *t, char *data, uint32_t size)
{
	int allocated = t == NULL;
	if (t == NULL) {
		t = tnt_mem_alloc(sizeof(struct tnt_tuple));
		if (t == NULL)
			return NULL;
		memset(t, 0, sizeof(struct tnt_tuple));
		t->alloc = 1;
		if (size == 0)
			return t;
	}
	if (t->size == 0)
		t->size = 4; /* cardinality */
	int esize = tnt_enc_size(size);
	size_t nsize = t->size + esize + size;
	/* reallocating tuple data */
	char *ndata = realloc(t->data, nsize);
	if (ndata == NULL) {
		if (allocated)
			tnt_mem_free(t);
		return NULL;
	}
	t->cardinality++;
	/* updating tuple cardinality */
	memcpy(ndata, &t->cardinality, 4);
	/* setting new field */
	tnt_enc_write(ndata + t->size, size);
	if (data)
		memcpy(ndata + t->size + esize, data, size);
	t->data = ndata;
	t->size = nsize;
	return t;
}

/*
 * tnt_tuple()
 *
 * add formated fields to existing tuple;
 * create new tuple and add formated fields;
 *
 * t   - tuple pointer, maybe NULL
 * fmt - printf-alike format (%s, %*s, %d, %l, %ll, %ul, %ull are supported)
 * 
 * if tuple pointer is NULL, then new tuple will be created. 
 * if tuple pointer is NULL and fmt is NULL, then new empty tuple
 * will be created.
 *
 * returns tuple pointer, or NULL on error.
*/
struct tnt_tuple*
tnt_tuple(struct tnt_tuple *t, char *fmt, ...)
{
	if (t == NULL) {
		t = tnt_tuple_add(NULL, NULL, 0);
		if (t == NULL)
			return NULL;
		if (fmt == NULL)
			return t;
	}
	va_list args;
	va_start(args, fmt);
	char *p = fmt;
	while (*p) {
		if (isspace(*p)) {
			p++;
			continue;
		} else
		if (*p != '%')
			return NULL;
		p++;
		switch (*p) {
		case '*': {
			if (*(p + 1) == 's') {
				int len = va_arg(args, int);
				char *s = va_arg(args, char*);
				tnt_tuple_add(t, s, len);
				p += 2;
			} else
				return NULL;
			break;
		}
		case 's': {
			char *s = va_arg(args, char*);
			tnt_tuple_add(t, s, strlen(s));
			p++;
			break;
		}
		case 'd': {
			int i = va_arg(args, int);
			tnt_tuple_add(t, (char*)&i, sizeof(int));
			p++;
			break;
		}	
		case 'u':
			if (*(p + 1) == 'l') {
				if (*(p + 2) == 'l') {
					unsigned long long int ull = va_arg(args, unsigned long long);
					tnt_tuple_add(t, (char*)&ull, sizeof(unsigned long long int));
					p += 3;
				} else {
					unsigned long int ul = va_arg(args, unsigned long int);
					tnt_tuple_add(t, (char*)&ul, sizeof(unsigned long int));
					p += 2;
				}
			} else
				return NULL;
			break;
		case 'l':
			if (*(p + 1) == 'l') {
				long long int ll = va_arg(args, int);
				tnt_tuple_add(t, (char*)&ll, sizeof(long long int));
				p += 2;
			} else {
				long int l = va_arg(args, int);
				tnt_tuple_add(t, (char*)&l, sizeof(long int));
				p++;
			}
			break;
		default:
			return NULL;
		}
	}
	va_end(args);
	return t;
}

/*
 * tnt_tuple_set()
 *
 * set tuple from data;
 * create new tuple and set it from data;
 *
 * t    - tuple pointer, maybe NULL
 * buf  - iproto tuple buffer representation
 * size - buffer size
 * 
 * if tuple pointer is NULL, then new tuple will be created. 
 *
 * returns tuple pointer, or NULL on error.
*/
struct tnt_tuple *tnt_tuple_set(struct tnt_tuple *t, char *buf, size_t size) {
	int allocated = t == NULL;
	if (t == NULL) {
		t = tnt_tuple_add(NULL, NULL, 0);
		if (t == NULL)
			return NULL;
	}
	if (size < sizeof(uint32_t))
		goto error;
	t->cardinality = *(uint32_t*)buf;
	t->size = size;
	t->data = tnt_mem_alloc(size);
	if (t->data == NULL)
		goto error;
	memcpy(t->data, buf, size);
	return t;
error:
	if (allocated)
		tnt_tuple_free(t);
	return NULL;
}

/*
 * tnt_list_init()
 *
 * initialize tuple list;
 *
 * l - tuple list pointer
*/
void tnt_list_init(struct tnt_list *l) {
	memset(l, 0, sizeof(struct tnt_list));
}

/*
 * tnt_list_free()
 *
 * free tuple list;
 *
 * l - tuple list pointer
*/
void tnt_list_free(struct tnt_list *l) {
	if (l->list == NULL)
		return;
	uint32_t i;
	for (i = 0 ; i < l->count ; i++)
		tnt_tuple_free(l->list[i].ptr);
	tnt_mem_free(l->list);
	if (l->alloc)
		tnt_mem_free(l);
}

/*
 * tnt_list()
 *
 * add tuples to the list;
 *
 * l - tuple list pointer, maybe NULL
 *
 * returns tuple list pointer, NULL on error.
*/
struct tnt_list *tnt_list(struct tnt_list *l, ...) {
	if (l == NULL) {
		l = tnt_mem_alloc(sizeof(struct tnt_list));
		if (l == NULL)
			return NULL;
		memset(l, 0, sizeof(struct tnt_list));
		l->alloc++;
	}
	va_list args;
	va_start(args, l);
	while (1) {
		struct tnt_tuple *ptr = va_arg(args, struct tnt_tuple*);
		if (ptr == NULL)
			break;
		tnt_list_at(l, ptr);
	}
	va_end(args);
	return l;
}

/*
 * tnt_list_at()
 *
 * attach tuple to list;
 * create new tuple and attach it the the list;
 *
 * l - tuple list pointer
 * t - tuple pointer
 *
 * returns tuple pointer, NULL on error.
*/
struct tnt_tuple *tnt_list_at(struct tnt_list *l, struct tnt_tuple *t) {
	/* allocating tuple if necessary */
	int allocated = t == NULL;
	if (t == NULL) {
		t = tnt_tuple_add(NULL, NULL, 0);
		if (t == NULL)
			return NULL;
	}
	/* reallocating tuple data */
	char *ndata = realloc(l->list, sizeof(struct tnt_list_ptr) * (l->count + 1));
	if (ndata == NULL) {
		if (allocated)
			tnt_tuple_free(t);
		return NULL;
	}
	l->list = (struct tnt_list_ptr*)ndata;
	/* setting pointer data */
	l->list[l->count].ptr = t;
	l->count++;
	return t;
}
