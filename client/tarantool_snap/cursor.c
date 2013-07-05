
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <third_party/crc32.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_snapshot.h>
#include <connector/c/include/tarantool/tnt_dir.h>

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include <lib/small/region.h>

#include "key.h"
#include "hash.h"
#include "options.h"
#include "space.h"
#include "sha1.h"
#include "ref.h"
#include "ts.h"
#include "cursor.h"

extern struct ts tss;

int
ts_cursor_open(struct ts_cursor *c, struct ts_key *k)
{
	c->k = k;
	c->r = ts_reftable_map(&tss.rt, k->file);

	int rc = tnt_log_open(&c->current, c->r->file,
					      (c->r->is_snap ? TNT_LOG_SNAPSHOT : TNT_LOG_XLOG));
	if (rc == -1) {
		printf("failed to open file: %s\n", c->r->file);
		return -1;
	}
	rc = tnt_log_seek(&c->current, k->offset);
	if (rc == -1) {
		printf("failed to seek for: %s:%d\n", c->r->file, k->offset);
		tnt_log_close(&c->current);
		return -1;
	}
	if (tnt_log_next(&c->current) == NULL) {
		printf("failed to read: %s:%d\n", c->r->file, k->offset);
		tnt_log_close(&c->current);
		return -1;
	}
	return 0;
}

struct tnt_tuple*
ts_cursor_tuple(struct ts_cursor *c)
{
	struct tnt_tuple *t = NULL;

	if (c->r->is_snap) {
		t = &c->current.current_value.t;
	} else {
		struct tnt_request *rp = &c->current.current_value.r;
		switch (rp->h.type) {
		case TNT_OP_INSERT:
			t = &rp->r.insert.t;
			break;
		case TNT_OP_DELETE:
			t = &rp->r.del.t;
			return 0;
		case TNT_OP_UPDATE:
			assert(0);
			break;
		default:
			assert(0);
			break;
		}
	}
	return t;
}

void
ts_cursor_close(struct ts_cursor *c)
{
	if (c->r == NULL)
		return;
	if (c->r->is_snap) {
		tnt_tuple_free(&c->current.current_value.t);
	} else {
		tnt_request_free(&c->current.current_value.r);
	}
	tnt_log_close(&c->current);
}
