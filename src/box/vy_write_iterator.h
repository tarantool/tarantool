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
#include "vy_stmt_iterator.h"
#include <stdbool.h>
#include <pthread.h>

/**
 * Iterate over an in-memory index when writing it to disk (dump)
 * or over a series of sorted runs on disk to create a new sorted
 * run (compaction).
 *
 * Background
 * ----------
 * With no loss of generality, lets consider the write_iterator to
 * have a single statements source (struct vy_write_src). Each key
 * consists of an LSNs sequence. A range of control points also
 * exists, named read views, each of which is characterized by
 * their VLSN. By default, for the biggest VLSN INT64_MAX is used,
 * and for the smallest one 0 is used:
 *
 * [0, vlsn1, vlsn2, vlsn3, ... INT64_MAX].
 *
 * The purpose of the write_iterator is to split LSNs sequence of
 * one key into subsequences, bordered with VLSNs, and then merge
 * each subsequence into a one statement.
 *                         --------
 *                         ONE KEY:
 *                         --------
 * 0              VLSN1               VLSN2       ...   INT64_MAX
 * |                |                   |                   |
 * | LSN1 ... LSN(i)|LSN(i+1) ... LSN(j)|LSN(j+1) ... LSN(N)|
 * \_______________/\__________________/\___________________/
 *      merge              merge               merge
 *
 * A range of optimizations is possible, which allow decrease
 * count of source statements and count of merged subsequences.
 *
 * ---------------------------------------------------------------
 * Optimization 1: skip DELETE from the last level of the oldest
 * read view.
 *                 ---------------------------
 *                 ONE KEY, LAST LEVEL SOURCE:
 *                 ---------------------------
 * LSN1   LSN2   ...   DELETE   LSNi   LSNi+1   ...   LSN_N
 * \___________________________/\___________________________/
 *            skip                         merge
 *
 * Details: if the source is the oldest source of all, then it is
 * not necessary to write any DELETE to the disk, including all
 * LSNs of the same key, which are older than this DELETE. However
 * such a skip is possible only if there is no read views to the
 * same key, older than this DELETE, because read views can't be
 * skipped.
 *
 * ---------------------------------------------------------------
 * Optimization 2: on REPLACE/DELETE skip rest of statements,
 * until the next read view.
 *                         --------
 *                         ONE KEY:
 *                         --------
 *    ...    LSN(k)    ...    LSN(i-1)   LSN(i)   REPLACE/DELETE
 * Read
 * views:     *                                        *
 *   _____________/\_____________________________/\__________/
 *       merge                 skip                    return
 *
 * Is is possible, since the REPLACE and DELETE discard all older
 * key versions.
 *
 * ---------------------------------------------------------------
 * Optimization 3: skip statements, which do not update the
 * secondary key.
 *
 * Masks intersection: not 0    0       0     not 0        not 0
 *                KEY: LSN1  DELETE  REPLACE  LSN5  ...  REPLACE
 *                    \____/\_______________/\__________________/
 *                    merge       skip              merge
 *
 * Details: if UPDATE is executed, it is transformed into
 * DELETE + REPLACE or single REPLACE. But in the secondary index
 * only keys are stored and if such UPDATE didn't change this key,
 * then there is no necessity to write this UPDATE. Actually,
 * existance of such UPDATEs is simply ignored.
 *
 * ---------------------------------------------------------------
 * Optimization 4: use older REPLACE/DELETE as a hints to apply
 * newer UPSERTs.
 *
 * After building history of a key, UPSERT sequences can be
 * accumulated in some read views. If the older read views have
 * REPLACE/DELETE, they can be used to turn newer UPSERTs into
 * REPLACEs.
 *                         --------
 *                         ONE KEY:
 *                         --------
 *        LSN1      LSN2     LSN3     LSN4
 *       REPLACE   UPSERT   UPSERT   UPSERT
 * Read
 * views:  *          *        *        *
 *         ^      \________________________/
 *         +- - - - - - - -< apply
 * Result:
 *        LSN1     LSN2     LSN3     LSN4
 *       REPLACE  REPLACE  REPLACE  REPLACE
 * Read
 * views:  *        *        *        *
 *
 * See implementation details in
 * vy_write_iterator_build_read_views.
 */

struct vy_write_iterator;
struct key_def;
struct tuple_format;
struct tuple;
struct vy_mem;
struct vy_slice;
struct vy_run_env;

/**
 * Open an empty write iterator. To add sources to the iterator
 * use vy_write_iterator_add_* functions.
 * @param key_def - key definition for tuple compare.
 * @param format - dormat to allocate new REPLACE and DELETE tuples from vy_run.
 * @param upsert_format - same as format, but for UPSERT tuples.
 * @param is_primary - set if this iterator is for a primary index.
 * @param is_last_level - there is no older level than the one we're writing to.
 * @param read_views - Opened read views.
 * @return the iterator or NULL on error (diag is set).
 */
struct vy_stmt_stream *
vy_write_iterator_new(const struct key_def *key_def, struct tuple_format *format,
		      struct tuple_format *upsert_format, bool is_primary,
		      bool is_last_level, struct rlist *read_views);

/**
 * Add a mem as a source of iterator.
 * @return 0 on success or not 0 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_add_mem(struct vy_stmt_stream *stream, struct vy_mem *mem);

/**
 * Add a run slice as a source of iterator.
 * @return 0 on success or not 0 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_add_slice(struct vy_stmt_stream *stream,
			    struct vy_slice *slice, struct vy_run_env *run_env);

#endif /* INCLUDES_TARANTOOL_BOX_VY_WRITE_STREAM_H */

