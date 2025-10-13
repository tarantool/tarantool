/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "xlog.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** `xlog_reader_next()` result. */
enum xlog_reader_result {
	/** No errors. */
	XLOG_READER_OK,
	/** EOF is reached. */
	XLOG_READER_EOF,
	/** EOF is reached. EOF marker is read. */
	XLOG_READER_EOF_MARKER,
	/** Error reading entry. */
	XLOG_READER_READ_ERROR,
	/** Error decoding entry body. */
	XLOG_READER_DECODE_ERROR,
};

struct xlog_reader;

/**
 * Allocates and initializes reader for xlog in `filename`.
 *
 * The main difference from `xlog_cursor_open` is that reading is done in
 * a different thread so that reading and handling requests can be done in
 * parallel. Also the row is parsed to request in that thread too, so this part
 * of work is also offloaded to thread.
 *
 * The reader should be used only in TX thread.
 *
 * Never fails (never returns NULL).
 */
struct xlog_reader *
xlog_reader_new(const char *filename);

/**
 * Destroys and frees reader for xlog in `filename`.
 */
void
xlog_reader_delete(struct xlog_reader *reader);

/**
 * Returns next xlog entry.
 *
 * Result is returned in `entry` output argument. NULL is returned in case
 * of EOF.
 *
 * Xlog entry memory is managed by reader. Note that on reading next entry
 * the previous may be invalidated so it should not be used after that.
 *
 * If the result is `XLOG_READER_DECODE_ERROR` or `XLOG_READER_READ_ERROR` and
 * error is `XlogError` the reading can be continued. Otherwise continuing
 * reading is UB.
 *
 * Returns:
 *   XLOG_READER_OK - entry is set.
 *   XLOG_READER_EOF - EOF is reached, entry is not set.
 *   XLOG_READER_EOF_MARKER - EOF is reached, EOF marker is read,
 *     entry is not set.
 *   XLOG_READER_READ_ERROR - error reading entry, diag is set.
 *   XLOG_READER_DECODE_ERROR - error decoding entry body, entry is set,
 *     but only header is valid in this case. Diag is set.
 */
enum xlog_reader_result
xlog_reader_next(struct xlog_reader *reader, struct xlog_entry **entry);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
