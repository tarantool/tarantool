#ifndef TNT_TUPLE_H_INCLUDED
#define TNT_TUPLE_H_INCLUDED

/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* tuple */

struct tnt_tuple {
	uint32_t cardinality; /* tuple fields count */
	char *data;           /* tuple fields */
	size_t size;          /* tuple size */
	int alloc;            /* allocation mark */
};

void tnt_tuple_init(struct tnt_tuple *t);
void tnt_tuple_free(struct tnt_tuple *t);

struct tnt_tuple *tnt_tuple(struct tnt_tuple *t, const char *fmt, ...);
struct tnt_tuple *tnt_tuple_add(struct tnt_tuple *t, const void *data, uint32_t size);
struct tnt_tuple *tnt_tuple_set(struct tnt_tuple *t, const void *buf, size_t size);
struct tnt_tuple* tnt_tuple_set_as(struct tnt_tuple *t, const void *buf, size_t size, uint32_t num);

/* tuple list */

struct tnt_list_ptr {
	struct tnt_tuple *ptr;     /* tuple pointer */
};

struct tnt_list {
	struct tnt_list_ptr *list; /* tuple list */
	uint32_t count;            /* tuple list count */
	int alloc;                 /* allocation mark */
};

void tnt_list_init(struct tnt_list *l);
void tnt_list_free(struct tnt_list *l);

struct tnt_list *tnt_list(struct tnt_list *l, ...);
struct tnt_tuple *tnt_list_at(struct tnt_list *l, struct tnt_tuple *t);

#endif /* TNT_TUPLE_H_INCLUDED */
