
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
#include <stdint.h>
#include <string.h>

#include <connector/c/include/tarantool/tnt_mem.h>
#include <connector/c/include/tarantool/tnt_proto.h>
#include <connector/c/include/tarantool/tnt_enc.h>
#include <connector/c/include/tarantool/tnt_tuple.h>
#include <connector/c/include/tarantool/tnt_request.h>
#include <connector/c/include/tarantool/tnt_reply.h>
#include <connector/c/include/tarantool/tnt_stream.h>
#include <connector/c/include/tarantool/tnt_iter.h>

static struct tnt_iter *tnt_iter_init(struct tnt_iter *i) {
	if (i) {
		memset(i, 0, sizeof(struct tnt_iter));
		return i;
	}
	i = tnt_mem_alloc(sizeof(struct tnt_iter));
	if (i == NULL)
		return NULL;
	memset(i, 0, sizeof(struct tnt_iter));
	i->alloc = 1;
	i->status = TNT_ITER_OK;
	return i;
}

static int tnt_iter_field_next(struct tnt_iter *i) {
	struct tnt_iter_field *ip = TNT_IFIELD(i);
	/* initializing iter to the first field */
	if (ip->fld_ptr == NULL) {
		/* in case of insufficient data */
		if (ip->tu->size < 4) {
			i->status = TNT_ITER_FAIL;
			return 0;
		}
		/* tuple could be empty */
		if (ip->tu->size == 4) {
			if (ip->tu->cardinality != 0)
				i->status = TNT_ITER_FAIL;
			return 0;
		}
		ip->fld_ptr = ip->tu->data + 4; /* skipping tuple cardinality */
		ip->fld_index = 0;
		ip->fld_esize = tnt_enc_read(ip->fld_ptr, &ip->fld_size);
		if (ip->fld_esize == -1) {
			i->status = TNT_ITER_FAIL;
			return 0;
		}
		ip->fld_data = ip->fld_ptr + ip->fld_esize;
		return 1;
	} else
	if (ip->tu->cardinality == (ip->fld_index + 1)) /* checking end */
		return 0;
	/* skipping to the next field */
	ip->fld_ptr += ip->fld_esize + ip->fld_size;
	ip->fld_index++;
	/* reading field size */
	ip->fld_esize = tnt_enc_read(ip->fld_ptr, &ip->fld_size);
	if (ip->fld_esize == -1) {
		i->status = TNT_ITER_FAIL;
		return 0;
	}
	ip->fld_data = ip->fld_ptr + ip->fld_esize;
	return 1;
}

static void tnt_iter_field_rewind(struct tnt_iter *i) {
	struct tnt_iter_field *ip = TNT_IFIELD(i);
	ip->fld_ptr = NULL;
	ip->fld_index = 0;
	ip->fld_data = NULL;
	ip->fld_size = 0;
	ip->fld_esize = 0;
}

/*
 * tnt_iter()
 *
 * initialize tuple field iterator;
 * create and initialize tuple field iterator;
 *
 * i - tuple field iterator pointer, maybe NULL
 * t - tuple pointer
 * 
 * if tuple field iterator pointer is NULL, then new tuple iterator will be created. 
 *
 * returns tuple iterator pointer, or NULL on error.
*/
struct tnt_iter*
tnt_iter(struct tnt_iter *i, struct tnt_tuple *t)
{
	i = tnt_iter_init(i);
	if (i == NULL)
		return NULL;
	i->type = TNT_ITER_FIELD;
	i->next = tnt_iter_field_next;
	i->rewind = tnt_iter_field_rewind;
	i->free = NULL;
	struct tnt_iter_field *ip = TNT_IFIELD(i);
	ip->tu = t;
	return i;
}

static int tnt_iter_list_next(struct tnt_iter *i) {
	struct tnt_iter_list *il = TNT_ILIST(i);
	if (il->tu_index == il->l->count)
		return 0;
	il->tu = il->l->list[il->tu_index++].ptr;
	return 1;
}

static void tnt_iter_list_rewind(struct tnt_iter *i) {
	struct tnt_iter_list *il = TNT_ILIST(i);
	il->tu_index = 0;
}

/*
 * tnt_iter_list()
 *
 * initialize tuple list iterator;
 * create and initialize tuple list iterator;
 *
 * i - tuple list iterator pointer, maybe NULL
 * t - tuple list pointer
 * 
 * if tuple list iterator pointer is NULL, then new tuple list
 * iterator will be created. 
 *
 * returns tuple list iterator pointer, or NULL on error.
*/
struct tnt_iter*
tnt_iter_list(struct tnt_iter *i, struct tnt_list *l)
{
	i = tnt_iter_init(i);
	if (i == NULL)
		return NULL;
	i->type = TNT_ITER_LIST;
	i->next = tnt_iter_list_next;
	i->rewind = tnt_iter_list_rewind;
	i->free = NULL;
	struct tnt_iter_list *il = TNT_ILIST(i);
	il->l = l;
	return i;
}

static int tnt_iter_reply_next(struct tnt_iter *i) {
	struct tnt_iter_reply *ir = TNT_IREPLY(i);
	tnt_reply_free(&ir->r);
	tnt_reply_init(&ir->r);
	int rc = ir->s->read_reply(ir->s, &ir->r);
	if (rc == -1) {
		i->status = TNT_ITER_FAIL;
		return 0;
	}
	return (rc == 1 /* finish */ ) ? 0 : 1;
}

static void tnt_iter_reply_free(struct tnt_iter *i) {
	struct tnt_iter_reply *ir = TNT_IREPLY(i);
	tnt_reply_free(&ir->r);
}

/*
 * tnt_iter_reply()
 *
 * initialize tuple reply iterator;
 * create and initialize reply iterator;
 *
 * i - tuple reply iterator pointer, maybe NULL
 * s - stream pointer
 * 
 * if stream iterator pointer is NULL, then new stream
 * iterator will be created. 
 *
 * returns stream iterator pointer, or NULL on error.
*/
struct tnt_iter *tnt_iter_reply(struct tnt_iter *i, struct tnt_stream *s) {
	i = tnt_iter_init(i);
	if (i == NULL)
		return NULL;
	i->type = TNT_ITER_REPLY;
	i->next = tnt_iter_reply_next;
	i->rewind = NULL;
	i->free = tnt_iter_reply_free;
	struct tnt_iter_reply *ir = TNT_IREPLY(i);
	ir->s = s;
	tnt_reply_init(&ir->r);
	return i;
}

static int tnt_iter_request_next(struct tnt_iter *i) {
	struct tnt_iter_request *ir = TNT_IREQUEST(i);
	tnt_request_free(&ir->r);
	tnt_request_init(&ir->r);
	int rc = ir->s->read_request(ir->s, &ir->r);
	if (rc == -1) {
		i->status = TNT_ITER_FAIL;
		return 0;
	}
	return (rc == 1 /* finish */ ) ? 0 : 1;
}

static void tnt_iter_request_free(struct tnt_iter *i) {
	struct tnt_iter_request *ir = TNT_IREQUEST(i);
	tnt_request_free(&ir->r);
}

/*
 * tnt_iter_request()
 *
 * initialize tuple request iterator;
 * create and initialize request iterator;
 *
 * i - tuple request iterator pointer, maybe NULL
 * s - stream pointer
 * 
 * if stream iterator pointer is NULL, then new stream
 * iterator will be created. 
 *
 * returns stream iterator pointer, or NULL on error.
*/
struct tnt_iter *tnt_iter_request(struct tnt_iter *i, struct tnt_stream *s) {
	i = tnt_iter_init(i);
	if (i == NULL)
		return NULL;
	i->type = TNT_ITER_REQUEST;
	i->next = tnt_iter_request_next;
	i->rewind = NULL;
	i->free = tnt_iter_request_free;
	struct tnt_iter_request *ir = TNT_IREQUEST(i);
	ir->s = s;
	tnt_request_init(&ir->r);
	return i;
}

/*
 * tnt_iter_free()
 *
 * free iterator.
 *
 * i - iterator pointer
 *
*/
void tnt_iter_free(struct tnt_iter *i) {
	if (i->free)
		i->free(i);
	if (i->alloc)
		tnt_mem_free(i);
}

/*
 * tnt_next()
 *
 * iterates to next field;
 * iterates to tuple in list; 
 *
 * i - iterator pointer
 *
 * depend on iterator tuple, sets to the
 * next tuple field or next tuple in the list.
 *
 * returns 1 or 0 on end.
*/
int tnt_next(struct tnt_iter *i) {
	return i->next(i);
}

/*
 * tnt_rewind()
 *
 * iterates to first field;
 * iterates to first tuple in a list; 
 *
 * i - iterator pointer
 *
 * depend on iterator tuple, sets to the
 * first tuple field or first tuple in a list.
*/
void tnt_rewind(struct tnt_iter *i) {
	i->status = TNT_ITER_OK;
	if (i->rewind)
		i->rewind(i);
}

/*
 * tnt_field()
 *
 * set or create iterator to the specified tuple field;
 *
 * i     - tuple iterator pointer
 * t     - tuple pointer
 * index - tuple field index
 *
 * returns tuple iterator pointer if field found, NULL otherwise.
*/
struct tnt_iter*
tnt_field(struct tnt_iter *i, struct tnt_tuple *t, uint32_t index)
{
	int allocated = i == NULL;
	if (i == NULL) {
		i = tnt_iter(i, t);
		if (i == NULL)
			return NULL;
	} else
		tnt_rewind(i);
	while (tnt_next(i))
		if (TNT_IFIELD_IDX(i) == index)
			return i;
	if (allocated)
		tnt_iter_free(i);
	return NULL;
}
