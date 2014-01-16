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

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <strings.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_rpl.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc_print.h"
#include "client/tarantool/tc_print_xlog.h"
#include "client/tarantool/tc_query.h"
#include "client/tarantool/tc_print.h"
#include "client/tarantool/tc.h"

extern struct tc tc;

static void
tc_printer_xlog_raw(struct tnt_log_row *row,
		    struct tnt_request *r)
{
	if (tc.opt.raw_with_headers) {
		fwrite(&tnt_log_marker_v11,
			sizeof(tnt_log_marker_v11), 1, stdout);
	}
	fwrite(&(row->hdr), sizeof(row->hdr), 1, stdout);
	fwrite(r->origin, r->origin_size, 1, stdout);
}

static void
tc_printer_xlog_tarantool(struct tnt_log_row *row,
			  struct tnt_request *r)
{
	struct sockaddr_in *peer = (void *)&row->row.cookie;
	tc_printf("%s, lsn: %"PRIu64", time: %lf, len: %"PRIu32", space: "
			"%"PRIu32", cookie: %s:%d ",
		tc_query_type(r->h.type),
		row->hdr.lsn,
		row->hdr.tm,
		row->hdr.len,
		r->r.insert.h.ns,
		inet_ntoa(peer->sin_addr),
		ntohs(peer->sin_port)
		);
	switch (r->h.type) {
	case TNT_OP_INSERT:
		tc_print_tuple(&r->r.insert.t);
		break;
	case TNT_OP_DELETE:
	case TNT_OP_DELETE_1_3:
		tc_print_tuple(&r->r.del.t);
		break;
	case TNT_OP_UPDATE:
		tc_print_tuple(&r->r.update.t);
		break;
	}
}

static void
tc_printer_xlog_lua(struct tnt_log_row *row,
		    struct tnt_request *r)
{
	tc_printf("lua box.");
	switch (r->h.type) {
	case TNT_OP_INSERT:
		if (r->r.insert.h.flags && TNT_FLAG_REPLACE == TNT_FLAG_REPLACE)
			tc_printf("replace(");
		else
			tc_printf("insert(");
		tc_printf("%"PRIu32", ", r->r.insert.h.ns);
		tc_print_lua_fields(&r->r.insert.t);
		break;
	case TNT_OP_DELETE:
	case TNT_OP_DELETE_1_3:
		tc_printf("delete(");
		tc_printf("%"PRIu32", ", r->r.del.h.ns);
		tc_print_lua_tuple(&r->r.del.t);
		break;
	case TNT_OP_UPDATE:
		tc_printf("update(");
		tc_printf("%"PRIu32", ", r->r.update.h.ns);
		tc_print_lua_tuple(&r->r.update.t);
		tc_printf(", '");
		for (uint32_t i = 0; i < r->r.update.opc; i++) {
			switch (r->r.update.opv[i].op) {
			case TNT_UPDATE_ASSIGN:
				tc_printf("=p");
				break;
			case TNT_UPDATE_ADD:
				tc_printf("+p");
				break;
			case TNT_UPDATE_AND:
				tc_printf("&p");
				break;
			case TNT_UPDATE_XOR:
				tc_printf("^p");
				break;
			case TNT_UPDATE_OR:
				tc_printf("|p");
				break;
			case TNT_UPDATE_SPLICE:
				tc_printf(":p");
				break;
			case TNT_UPDATE_DELETE:
				tc_printf("#p");
				break;
			case TNT_UPDATE_INSERT:
				tc_printf("!p");
				break;
			}
		}
		tc_printf("'");
		for (uint32_t i = 0; i < r->r.update.opc; i++) {
			tc_printf(", %"PRIu32,
				r->r.update.opv[i].field);
			switch (r->r.update.opv[i].op){
			case TNT_UPDATE_ADD:
			case TNT_UPDATE_AND:
			case TNT_UPDATE_XOR:
			case TNT_UPDATE_OR:
				tc_printf(", ");
				tc_print_lua_field(r->r.update.opv[i].data,
						r->r.update.opv[i].size, 0);
				break;
			case TNT_UPDATE_SPLICE:
				tc_printf(", box.pack('ppp'");
				char *data = r->r.update.opv[i].data;
				size_t pos = 1;
				tc_printf(", %"PRId32,
					*(int32_t *)(data + pos));
				pos += 5;
				tc_printf(", %"PRId32", ",
					*(int32_t *)(data + pos));
				pos += 4 + r->r.update.opv[i].size_enc_len;
				tc_printf("\'");
				tc_print_string(data,
					r->r.update.opv[i].size - pos, 1);
				tc_printf("\'");
				tc_printf(")");
				break;
			case TNT_UPDATE_DELETE:
				tc_printf(", \'\'");
				break;
			case TNT_UPDATE_INSERT:
			case TNT_UPDATE_ASSIGN:
				tc_printf(", ");
				tc_print_lua_field(r->r.update.opv[i].data,
						r->r.update.opv[i].size,
						tc.opt.str_instead_int);
				break;
			}
		}
		break;
	}
	tc_printf(") -- %"PRIu64, row->hdr.lsn);
	if (tc.opt.delim_len > 0)
		tc_printf("%s\n", tc.opt.delim);
	else
		tc_printf("\n");
}

tc_printerf_xlog_t tc_print_getxlogcb(const char *name)
{
	if (name == NULL)
		return tc_printer_xlog_tarantool;
	if (!strcasecmp(name, "tarantool"))
		return tc_printer_xlog_tarantool;
	if (!strcasecmp(name, "raw"))
		return tc_printer_xlog_raw;
	if (!strcasecmp(name, "lua"))
		return tc_printer_xlog_lua;
	return NULL;
}
