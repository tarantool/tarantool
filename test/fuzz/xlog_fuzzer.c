/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A libFuzzer-based target for the write-ahead log reader in box/xlog.c.
 *
 * The harness parses the fuzz input as an in-memory xlog file via
 * xlog_cursor_openmem() and iterates all rows, with the decoding
 * mode (strict or force_recovery) selected from the input, so the
 * meta-header grammar, the fixheader/crc32 checks, the zstd
 * decompression path and both the strict and the recovery
 * row-decode paths are exercised. The harness mutates no product
 * state.
 */

#include <stdint.h>
#include <string.h>

#include "xlog.h"
#include "crc32.h"
#include "memory.h"
#include "say.h"

/* Hard cap on decoded rows, as a guard against pathological loops. */
#define MAX_ROWS 1000000

__attribute__((constructor))
static void
setup(void)
{
	crc32_init();
	memory_init();
	/*
	 * xlog_cursor_next() with force_recovery reports every
	 * undecodable row via say_error(), which floods stderr and
	 * hurts the fuzzing throughput: keep the logger silent.
	 */
	say_set_log_level(S_FATAL);
}

__attribute__((destructor))
static void
teardown(void)
{
	memory_free();
}

static void
iterate_rows(const uint8_t *data, size_t size, bool force_recovery)
{
	struct xlog_cursor cursor;
	if (xlog_cursor_openmem(&cursor, (const char *)data, size,
				"mem") != 0)
		return;
	struct xrow_header row;
	memset(&row, 0, sizeof(row));
	for (long i = 0; i < MAX_ROWS; i++) {
		if (xlog_cursor_next(&cursor, &row, force_recovery) != 0)
			break;
	}
	xlog_cursor_close(&cursor, false);
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	/* Too short to hold even the meta header magic plus a fixheader. */
	if (size < 8)
		return 0;
	/*
	 * The decoding mode is chosen from the input, FuzzedDataProvider
	 * style: consume the last byte from the end of the buffer, its
	 * bit 0 enables force_recovery, and strip it from the payload
	 * passed to the xlog parser.
	 */
	bool force_recovery = data[size - 1] & 1;

	iterate_rows(data, size - 1, force_recovery);
	return 0;
}
