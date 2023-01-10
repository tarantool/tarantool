/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "xlog.h"
#include "xrow.h"
#include "crc32.h"
#include "random.h"
#include "memory.h"
#include "iproto_constants.h"

/**
 * Keep in sync with src/box/xlog.c!
 */
enum {
	XLOG_READ_AHEAD_MIN = 128 * 1024,
	XLOG_READ_AHEAD_MAX = 8 * 1024 * 1024,
};

/**
 * Create a temporary directory, initialize it as xdir, and create a new xlog.
 */
static void
create_xlog(struct xlog *xlog, char *dirname)
{
	fail_if(mkdtemp(dirname) == NULL);

	struct xdir xdir;
	struct tt_uuid tt_uuid;
	struct vclock vclock;
	memset(&tt_uuid, 1, sizeof(tt_uuid));
	memset(&vclock, 0, sizeof(vclock));

	xdir_create(&xdir, dirname, XLOG, &tt_uuid, &xlog_opts_default);

	fail_if(xdir_create_xlog(&xdir, xlog, &vclock) < 0);
}

/**
 * Write a tuple to the xlog.
 */
static void
write_tuple(struct xlog *xlog, const char *data, uint32_t size)
{
	static int64_t lsn;
	struct request_replace_body body;
	request_replace_body_create(&body, 0);

	struct xrow_header row;
	memset(&row, 0, sizeof(struct xrow_header));
	row.lsn = ++lsn;
	row.type = IPROTO_INSERT;
	row.bodycnt = 2;
	row.body[0].iov_base = &body;
	row.body[0].iov_len = sizeof(body);
	row.body[1].iov_base = (char *)data;
	row.body[1].iov_len = size;

	fail_if(xlog_write_row(xlog, &row) < 0);
}

/**
 * Write 1 KB of random uncompressed data to the xlog. The compressed size
 * is roughly the same due to the randomness of the data.
 */
static void
write_1k(struct xlog *xlog)
{
	char data[1024];
	const size_t data_size = sizeof(data) - 3;
	random_bytes(mp_encode_binl(data, data_size), data_size);
	write_tuple(xlog, data, sizeof(data));
}

/**
 * Test that the size of the read buffer dynamically increased while reading a
 * large file, and shrunk when xlog is written/read by small portions of data.
 */
static void
test_dynamic_sized_ibuf(void)
{
	header();
	plan(4);
	struct xlog xlog;
	char dirname[] = "./xlog.XXXXXX";
	char filename[PATH_MAX];
	create_xlog(&xlog, dirname);
	strlcpy(filename, xlog.filename, sizeof(filename));

	/* Write about 20 MB of data to the xlog. */
	for (int i = 0; i < 20 * 1024; i++)
		write_1k(&xlog);
	fail_if(xlog_flush(&xlog) < 0);

	struct xlog_cursor cursor;
	fail_if(xlog_cursor_open(&cursor, xlog.filename) < 0);

	/*
	 * Read the whole xlog and check that the size of the buffer reaches
	 * maximum value while reading, it will decrease when reading near the
	 * end of the file, so keep it on each iteration.
	 */
	int rc;
	int64_t prev_lsn = 0;
	struct xrow_header row;
	size_t read_ahead_max = 0;
	size_t ibuf_used_max = 0;
	while ((rc = xlog_cursor_next(&cursor, &row, false)) == 0) {
		fail_if(row.lsn != prev_lsn + 1);
		prev_lsn = row.lsn;

		read_ahead_max = MAX(cursor.read_ahead, read_ahead_max);
		ibuf_used_max = MAX(ibuf_used_max, ibuf_used(&cursor.rbuf));
	}

	is(read_ahead_max, XLOG_READ_AHEAD_MAX,
	   "read_ahead increased to %d", XLOG_READ_AHEAD_MAX);
	ok(ibuf_used_max >= XLOG_READ_AHEAD_MAX,
	   "ibuf size increased to at least %d", XLOG_READ_AHEAD_MAX);

	/*
	 * Do 1 KB write/read to shrink the read buffer to the minimal capacity.
	 */
	write_1k(&xlog);
	fail_if(xlog_flush(&xlog) < 0);
	while ((rc = xlog_cursor_next(&cursor, &row, false)) == 0) {
		fail_if(row.lsn != prev_lsn + 1);
		prev_lsn = row.lsn;
	}

	is(cursor.read_ahead, XLOG_READ_AHEAD_MIN,
	   "read_ahead decreased to %d", XLOG_READ_AHEAD_MIN);
	ok(ibuf_capacity(&cursor.rbuf) == 0, "ibuf capacity decreased to 0");

	xlog_cursor_close(&cursor, false);
	fail_if(xlog_close(&xlog, false) < 0);
	unlink(filename);
	rmdir(dirname);

	check_plan();
	footer();
}

int
main(void)
{
	plan(1);
	crc32_init();
	memory_init();
	random_init();

	test_dynamic_sized_ibuf();

	random_free();
	memory_free();
	return check_plan();
}
