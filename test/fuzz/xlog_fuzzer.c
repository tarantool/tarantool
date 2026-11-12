/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A libFuzzer-based target for the write-ahead log reader in box/xlog.c.
 *
 * The harness parses the fuzz input as an in-memory xlog file via
 * xlog_cursor_openmem() and iterates all rows twice: once in strict
 * mode and once with force_recovery enabled, so the meta-header
 * grammar, the fixheader/crc32 checks, the zstd decompression path and
 * both the strict and the recovery row-decode paths are exercised.
 * Any memory error is reported by ASan; no product state is mutated.
 */

#include <stdint.h>
#include <string.h>

#include "xlog.h"
#include "crc32.h"
#include "memory.h"

/* Hard cap on decoded rows, as a guard against pathological loops. */
#define MAX_ROWS 1000000

__attribute__((constructor))
static void
setup(void)
{
	crc32_init();
	memory_init();
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
	if (size < 8)
		return 0;
	iterate_rows(data, size, false);
	iterate_rows(data, size, true);
	return 0;
}
