#ifndef INCLUDES_TARANTOOL_BOX_VY_WRITE_STREAM_H
#define INCLUDES_TARANTOOL_BOX_VY_WRITE_STREAM_H
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "trivia/util.h"
#include "vy_stmt_stream.h"
#include "vy_read_view.h"
#include <stdbool.h>
#include <pthread.h>

/**
 * Iterate over an in-memory index when writing it to disk (dump)
 * or over a series of sorted runs on disk to create a new sorted
 * run (compaction).
 *
 * Background
 * ----------
 * The write iterator merges multiple data sources into one,
 * ordering statements by key and then by LSN and purging
 * unnecessary changes.
 *
 * The sources supply statements in ascending order of the
 * key and descending order of LSN (newest changes first).
 * A heap is used to preserve descending order of LSNs
 * in the output.
 *
 * There may be many statements for the same key, forming
 * a history.
 *
 * The iterator needs to preserve only the statements
 * which are visible to the active read views, each represented
 * by a view LSN (VLSN) and purge the rest.
 *
 * The list of read views always contains at least the "current"
 * read view, represented by INT64_MAX. 0 stands for the oldest
 * possible LSN:
 *
 * [0, vlsn1, vlsn2, vlsn3, ... INT64_MAX].
 *
 * The iterator splits a sequence of LSNs for the same key into
 * a series of histories, one for each read view, and then merges
 * each history into a single statement:
 *
 *                         --------
 *                         SAME KEY
 *                         --------
 * 0               VLSN1                 VLSN2       ...     INT64_MAX
 * |                 |                     |                     |
 * | LSN1 ... LSN(i) | LSN(i+1) ... LSN(j) | LSN(j+1) ... LSN(N) |
 * \________________/ \___________________/ \____________________/
 *       merge                merge                 merge
 *
 * The following optimizations are applicable, all aiming at
 * purging unnecessary statements from the output. The
 * optimizations are applied while reading the statements from
 * the heap, from newest LSN to oldest.
 *
 * ---------------------------------------------------------------
 * Optimization #1: when merging the last level of the LSM tree,
 * e.g. when doing a major compaction, skip DELETEs from the
 * output as long as they are older than the oldest read view:
 *
 *                 ---------------------------
 *                 SAME KEY, MAJOR COMPACTION
 *                 ---------------------------
 *
 * 0                          VLSN1          ...         INT64_MAX
 * |                            |                            |
 * | LSN1  LSN2   ...   DELETE  | LSNi   LSNi+1  ...  LSN_N  |
 * \___________________________/ \___________________________/
 *            skip                         merge
 *
 * Indeed, we don't have to store absent data on disk, including
 * the statements even older than the pruned delete.
 * As for all other read views, if a DELETE is visible to a read
 * view, it has to be preserved.
 *
 * ---------------------------------------------------------------
 * Optimization #2: once we found a REPLACE or DELETE, we can skip
 * the rest of the stream until the next read view:
 *
 *                         --------
 *                         SAME KEY
 *                         --------
 * VLSN1                    VLSN2                     INT64_MAX
 *   |                        |                           |
 *   | LSN1 LSN2 ...  REPLACE | LSNi ... DELETE ... LSN_N |
 *   \______________/\_______/ \_______/\_________________/
 *         skip        keep       skip         merge
 *
 * ---------------------------------------------------------------
 * Optimization #3: when compacting runs of a secondary key, skip
 * statements, which do not update this key.
 *
 *                         --------
 *                         SAME KEY
 *                         --------
 *            VLSN(i)                                    VLSN(i+1)
 * Masks        |                                            |
 * intersection:| not 0     0       0     not 0        not 0 |
 *              |  ANY   DELETE  REPLACE   ANY  ...  REPLACE |
 *              \______/\_______________/\___________________/
 *               merge       skip              merge
 *
 * Details: when UPDATE is executed by Tarantool, it is
 * transformed into DELETE + REPLACE or a single REPLACE. But it
 * is only necessary to write anything into the secondary key if
 * such UPDATE changes any field, which is part of the key.
 * All other UPDATEs can be simply skipped.
 *
 * ---------------------------------------------------------------
 * Optimization #4: use older REPLACE/DELETE to apply UPSERTs and
 * convert them into a single REPLACE. When compaction includes
 * the last level, absence of REPLACE or DELETE is equivalent
 * to a DELETE, and UPSERT can be converted to REPLACE as well.
 * If REPLACE or DELETE is found in an older read view, it can
 * be used as well.
 *
 *                         --------
 *                         SAME KEY
 *                         --------
 * 0   VLSN1     VLSN2     VLSN3     VLSN4     VLSN5   INT64_MAX
 * |     |         |         |         |         |        |
 * |     | REPLACE | UPSERT  | UPSERT  | UPSERT  |   ...  |
 * \_____|___^_____|_________|_________|_________|________/
 *           ^ <  <  apply
 *                    ^  <  <   apply
 *                               ^  <  <  apply
 *
 * Result:
 *
 * 0   VLSN1     VLSN2     VLSN3     VLSN4     VLSN5   INT64_MAX
 * |     |         |         |         |         |        |
 * |     | REPLACE | REPLACE | REPLACE | REPLACE |   ...  |
 * \_____|_________|_________|_________|_________|________/
 *
 * See implementation details in
 * vy_write_iterator_build_read_views.
 *
 * ---------------------------------------------------------------
 * Optimization #5: discard a tautological DELETE statement, i.e.
 * a statement that was not removed from the history because it
 * is referenced by read view, but that is preceeded by another
 * DELETE and hence not needed.
 *
 *                         --------
 *                         SAME KEY
 *                         --------
 *
 * VLSN(i)                   VLSN(i+1)                   VLSN(i+2)
 *   |                          |                           |
 *   | LSN1  LSN2  ...  DELETE  | LSNi  LSNi+1  ...  DELETE |
 *   \________________/\_______/ \_________________/\______/
 *          skip         keep           skip         discard
 *
 * ---------------------------------------------------------------
 * Optimization #6: discard the first DELETE if the oldest
 * statement for the current key among all sources is an INSERT.
 * Rationale: if a key's history starts from an INSERT, there is
 * either no statements for this key in older runs or the latest
 * statement is a DELETE; in either case, the first DELETE does
 * not affect the resulting tuple, no matter which read view it
 * is looked from, and hence can be skipped.
 *
 *                         --------
 *                         SAME KEY
 *                         --------
 *
 * 0                               VLSN1              INT64_MAX
 * |                                 |                    |
 * | INSERT  LSN2  ...  LSNi  DELETE | LSNi+2  ...  LSN_N |
 * \________________________/\______/ \__________________/
 *           skip             discard         merge
 *
 * If this optimization is performed, the resulting key's history
 * will either be empty or start with a REPLACE or INSERT. In the
 * latter case we convert the first REPLACE to INSERT so that if
 * the key gets deleted later, we will perform this optimization
 * again on the next compaction to drop the DELETE.
 *
 * In order not to trigger this optimization by mistake, we must
 * also turn the first INSERT in the resulting key's history to a
 * REPLACE in case the oldest statement among all sources is not
 * an INSERT.
 */

struct vy_write_iterator;
struct key_def;
struct tuple_format;
struct tuple;
struct vy_mem;
struct vy_slice;

/**
 * Open an empty write iterator. To add sources to the iterator
 * use vy_write_iterator_add_* functions.
 * @param cmp_def - key definition for tuple compare.
 * @param format - dormat to allocate new REPLACE and DELETE tuples from vy_run.
 * @param LSM tree is_primary - set if this iterator is for a primary index.
 * @param is_last_level - there is no older level than the one we're writing to.
 * @param read_views - Opened read views.
 * @return the iterator or NULL on error (diag is set).
 */
struct vy_stmt_stream *
vy_write_iterator_new(const struct key_def *cmp_def,
		      struct tuple_format *format,
		      bool is_primary, bool is_last_level,
		      struct rlist *read_views);

/**
 * Add a mem as a source to the iterator.
 * @return 0 on success, -1 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_new_mem(struct vy_stmt_stream *stream, struct vy_mem *mem);

/**
 * Add a run slice as a source to the iterator.
 * @return 0 on success, -1 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_new_slice(struct vy_stmt_stream *stream,
			    struct vy_slice *slice);

#endif /* INCLUDES_TARANTOOL_BOX_VY_WRITE_STREAM_H */

