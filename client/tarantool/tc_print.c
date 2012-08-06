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
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_rpl.h>

#include "client/tarantool/tc_print.h"
#include "client/tarantool/tc_query.h"

static void tc_print_fields(struct tnt_tuple *tu) {
	struct tnt_iter ifl;
	tnt_iter(&ifl, tu);
	while (tnt_next(&ifl)) {
		if (TNT_IFIELD_IDX(&ifl) != 0)
			printf(", ");
		char *data = TNT_IFIELD_DATA(&ifl);
		uint32_t size = TNT_IFIELD_SIZE(&ifl);
		if (!isprint(data[0]) && (size == 4 || size == 8)) {
			if (size == 4) {
				uint32_t i = *((uint32_t*)data);
				printf("%"PRIu32, i);
			} else {
				uint64_t i = *((uint64_t*)data);
				printf("%"PRIu64, i);
			}
		} else {
			printf("'%-.*s'", size, data);
		}
	}
	if (ifl.status == TNT_ITER_FAIL)
		printf("<parsing error>");
	tnt_iter_free(&ifl);
}

void tc_print_tuple(struct tnt_tuple *tu)
{
	printf("[");
	tc_print_fields(tu);
	printf("]\n");
}

void tc_print_list(struct tnt_list *l)
{
	struct tnt_iter it;
	tnt_iter_list(&it, l);
	while (tnt_next(&it)) {
		struct tnt_tuple *tu = TNT_ILIST_TUPLE(&it);
		tc_print_tuple(tu);
	}
	tnt_iter_free(&it);
}

static void
tc_printer_tarantool(struct tnt_xlog_header_v11 *hdr,
		     struct tnt_request *r)
{
	printf("%s lsn: %"PRIu64", time: %f, len: %"PRIu32"\n",
	       tc_query_type(r->h.type),
	       hdr->lsn,
	       hdr->tm,
	       hdr->len);
	switch (r->h.type) {
	case TNT_OP_INSERT:
		tc_print_tuple(&r->r.insert.t);
		break;
	case TNT_OP_DELETE:
		tc_print_tuple(&r->r.del.t);
		break;
	case TNT_OP_UPDATE:
		tc_print_tuple(&r->r.update.t);
		break;
	}
}

static void tc_printer_sql_tuple(struct tnt_tuple *tu) {
	printf("(");
	tc_print_fields(tu);
	printf(")");
}

static void
tc_printer_sql(struct tnt_xlog_header_v11 *hdr,
	       struct tnt_request *r)
{
	(void)hdr;
	switch (r->h.type) {
	case TNT_OP_INSERT:
		if (r->r.insert.h.flags & TNT_FLAG_REPLACE)
			printf("replace");
		else
			printf("insert");
		printf(" into t%"PRIu32" values ", r->r.insert.h.ns);
		tc_printer_sql_tuple(&r->r.insert.t);
		printf("\n");
		break;
	case TNT_OP_DELETE:
		printf("delete from t%"PRIu32" where k0 = ", r->r.del.h.ns);
		tc_printer_sql_tuple(&r->r.del.t);
		printf("\n");
		break;
	case TNT_OP_UPDATE: {
		printf("update t%"PRIu32" set ", r->r.update.h.ns);
		int i = 0;
		while (i < r->r.update.opc) {
			struct tnt_request_update_op *op = &r->r.update.opv[i];
			switch (op->op) {
			case TNT_UPDATE_ASSIGN:
				/* XXX: hexademical data input needed */
				break;
			case TNT_UPDATE_ADD:
				break;
			case TNT_UPDATE_AND:
				break;
			case TNT_UPDATE_XOR:
				break;
			case TNT_UPDATE_OR:
				break;
			case TNT_UPDATE_SPLICE:
				break;
			case TNT_UPDATE_DELETE:
				break;
			case TNT_UPDATE_INSERT:
				break;
			default:
				break;
			}

			i++;
		}
		break;
	}
	default:
		break;
	}
}

tc_printerf_t tc_print_getcb(const char *name)
{
	if (name == NULL)
		return tc_printer_tarantool;
	if (!strcasecmp(name, "tarantool"))
		return tc_printer_tarantool;
	else
	if (!strcasecmp(name, "sql"))
		return tc_printer_sql;
	else
	if (!strcasecmp(name, "yaml"))
		return NULL;
	return NULL;
}
