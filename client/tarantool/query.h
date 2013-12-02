#ifndef TC_QUERY_H_INCLUDED
#define TC_QUERY_H_INCLUDED
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

typedef int (*tc_query_t)(char *reply, size_t size, void *ctx);

int tc_printer(char *r, size_t size, void *ctx);
int tc_exec(char *q, tc_query_t cb, void *ctx);

static inline int
tc_query(char *q, void *cb) {
	return tc_exec(q, (tc_query_t)cb, NULL);
}


#if 0
typedef int (*tc_query_t)(struct tnt_reply *r, void *ptr, char **e);

char *tc_query_type(uint32_t type);

int tc_query_printer(struct tnt_reply *r, void *ptr, char **e);
int tc_query_foreach(tc_query_t cb, void *cba, char **e);
int tc_query(char *q, char **e);

struct tnt_reply;

char *tc_query_error(char *fmt, ...);
char *tc_query_op(struct tnt_reply *r);
#endif

#endif /* TC_QUERY_H_INCLUDED */
