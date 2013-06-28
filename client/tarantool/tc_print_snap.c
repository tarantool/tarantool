#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_snapshot.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_print.h"
#include "client/tarantool/tc_print_snap.h"
#include "client/tarantool/tc_query.h"
#include "client/tarantool/tc_store.h"

extern struct tc tc;

static void
tc_printer_snap_raw( struct tnt_log_row_snap_v11 *row,
		     struct tnt_tuple *tu)
{
	if (tc.opt.raw_with_headers) {
		fwrite(&tnt_log_marker_v11,
			sizeof(tnt_log_marker_v11), 1, stdout);
	}
	fwrite(row, sizeof(row), 1, stdout);
	fwrite(tu->data, tu->size, 1, stdout);
}
static void
tc_printer_snap_tarantool( struct tnt_log_row_snap_v11 *row,
			   struct tnt_tuple *tu)
{
	tc_printf("tag: %"PRIu16", cookie: %"PRIu64", space: %"PRIu32"\n",
		row->tag,
		row->cookie,
		row->space);
	tc_print_tuple(tu);

}
static void
tc_printer_snap_lua( struct tnt_log_row_snap_v11 *row,
		     struct tnt_tuple *tu)
{
	tc_printf("lua box.insert(%"PRIu32", ", row->space);
	tc_print_lua_fields(tu);
	tc_printf(")");
	if (tc.opt.delim_len > 0)
		tc_printf("%s\n", tc.opt.delim);
	else
		tc_printf("\n");
}

tc_printerf_snap_t tc_print_getsnapcb(const char *name)
{
	if (name == NULL)
		return tc_printer_snap_tarantool;
	if (!strcasecmp(name, "tarantool"))
		return tc_printer_snap_tarantool;
	if (!strcasecmp(name, "raw"))
		return tc_printer_snap_raw;
	if (!strcasecmp(name, "lua"))
		return tc_printer_snap_lua;
	return NULL;
}
