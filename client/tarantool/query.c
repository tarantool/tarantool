
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

#include <lib/tarantool.h>

#include <include/errcode.h>
#include <client/tarantool/opt.h>
#include <client/tarantool/main.h>
#include <client/tarantool/print.h>
#include <client/tarantool/query.h>

extern struct tarantool_client tc;

int tc_printer(char *reply, size_t size, void *ctx)
{
	(void)ctx;
	(void)size;
	tc_printf("%s", reply);
	return 0;
}

int tc_exec(char *q, tc_query_t cb, void *ctx)
{
	int rc = tb_conwrite(&tc.admin, q, strlen(q));
	if (rc == -1)
		return -1;
	size_t size;
	char *reply;
	rc = tb_conread(&tc.admin, &reply, &size);
	if (rc == -1)
		return -1;
	rc = 0;
	if (cb && reply)
		rc = cb(reply, size, ctx);
	free(reply);
	return rc;
}

#if 0
char *tc_query_error(char *fmt, ...) {
	char msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	char *ptr = strdup(msg);
	if (ptr == NULL)
		tc_error("memory allocation failed");
	return ptr;
}

char *tc_query_type(uint32_t type) {
	switch (type) {
	case TNT_OP_PING:   return "Ping";
	case TNT_OP_INSERT: return "Insert";
	case TNT_OP_DELETE: return "Delete";
	case TNT_OP_UPDATE: return "Update";
	case TNT_OP_SELECT: return "Select";
	case TNT_OP_CALL:   return "Call";
	}
	return "Unknown";
}

char *tc_query_op(struct tnt_reply *r) {
	return tc_query_type(r->op);
}

int tc_query_printer(struct tnt_reply *r, void *ptr, char **e) {
	(void)ptr;
	(void)e;
	tc_printf("%s OK, %d rows affected\n", tc_query_op(r),
		  r->count);
	tc_print_list(TNT_REPLY_LIST(r));
	return 0;
}

int tc_query_foreach(tc_query_t cb, void *cba, char **e)
{
	int rc = -1;
	struct tnt_iter i;
	tnt_iter_reply(&i, tc.net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		if (tnt_error(tc.net) != TNT_EOK) {
			*e = tc_query_error("%s ERROR, %s",
					    tc_query_op(r),
					    tnt_strerror(tc.net));
			goto error;
		} else if (r->code != 0) {
			*e = tc_query_error("%s ERROR, %s (%s)",
					    tc_query_op(r), ((r->error) ? r->error : ""),
					    tnt_errcode_str(r->code >> 8));
			goto error;
		}
		/* invoking callback if supplied */
		if (cb) {
			if (cb(r, cba, e) == -1)
				goto error;
		}
	}
	rc = (i.status == TNT_ITER_FAIL) ? -1 : 0;
error:
	tnt_iter_free(&i);
	return rc;
}

int tc_query(char *q, char **e) {
	int rc = tnt_query(tc.net, q, strlen(q), e);
	if (rc == -1)
		return -1;
	rc = tnt_flush(tc.net);
	if (rc < 0) {
		char *ee = tnt_strerror(tc.net);
		if (ee) {
			*e = tc_query_error("%s", ee);
		}
		return -1;
	}
	return 0;
}
#endif

