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
#include <stdbool.h>
#include <pthread.h>

/**
 * Iterate over an in-memory index when writing it to disk (dump)
 * or over a series of sorted runs on disk to create a new sorted
 * run (compaction).
 *
 * Use merge iterator to order the output and filter out
 * too old statements (older than the oldest active read view).
 *
 * Squash multiple UPSERT statements over the same key into one,
 * if possible.
 *
 * Background
 * ----------
 * Vinyl provides support for consistent read views. The oldest
 * active read view is maintained in the transaction manager.
 * To support it, when dumping or compacting statements on disk,
 * older versions need to be preserved, and versions outside
 * any active read view garbage collected. This task is handled
 * by the write iterator.
 *
 * Filtering
 * ---------
 * Let's call each transaction consistent read view LSN vlsn.
 *
 *	oldest_vlsn = MIN(vlsn) over all active transactions
 *
 * Thus to preserve relevant data for every key and purge old
 * versions, the iterator works as follows:
 *
 *      If statement lsn is greater than oldest vlsn, the
 *      statement is preserved.
 *
 *      Otherwise, if statement type is REPLACE/DELETE, then
 *      it's returned, and the iterator can proceed to the
 *      next key: the readers do not need the history.
 *
 *      Otherwise, the statement is UPSERT, and in order
 *      to restore the original tuple from UPSERT the reader
 *      does need the history: they need to look for an older
 *      statement to which the UPSERT can be applied to get
 *      a tuple. This older statement can be UPSERT as well,
 *      and so on.
 *	In other words, of statement type is UPSERT, the reader
 *	needs a range of statements from the youngest statement
 *	with lsn <= vlsn to the youngest non-UPSERT statement
 *	with lsn <= vlsn, borders included.
 *
 *	All other versions of this key can be skipped, and hence
 *	garbage collected.
 *
 * Squashing and garbage collection
 * --------------------------------
 * Filtering and garbage collection, performed by write iterator,
 * must have no effect on read views of active transactions:
 * they should read the same data as before.
 *
 * On the other hand, old version should be deleted as soon as possible;
 * multiple UPSERTs could be merged together to take up less
 * space, or substituted with REPLACE.
 *
 * Here's how it's done:
 *
 *
 *	1) Every statement with lsn greater than oldest vlsn is preserved
 *	in the output, since there could be an active transaction
 *	that needs it.
 *
 *	2) For all statements with lsn <= oldest_vlsn, only a single
 *	resultant statement is returned. Here's how.
 *
 *	2.1) If the youngest statement with lsn <= oldest _vlsn is a
 *	REPLACE/DELETE, it becomes the resultant statement.
 *
 *	2.2) Otherwise, it as an UPSERT. Then we must iterate over
 *	all older LSNs for this key until we find a REPLACE/DELETE
 *	or exhaust all input streams for this key.
 *
 *	If the older lsn is a yet another UPSERT, two upserts are
 *	squashed together into one. Otherwise we found an
 *	REPLACE/DELETE, so apply all preceding UPSERTs to it and
 *	get the resultant statement.
 *
 * There is an extra twist to this algorithm, used when performing
 * compaction of the last LSM level (i.e. merging all existing
 * runs into one). The last level does not need to store DELETEs.
 * Thus we can:
 * 1) Completely skip the resultant statement from output if it's
 *    a DELETE.
 *     |      ...      |       |     ...      |
 *     |               |       |              |    ^
 *     +- oldest vlsn -+   =   +- oldest lsn -+    ^ lsn
 *     |               |       |              |    ^
 *     |    DELETE     |       +--------------+
 *     |      ...      |
 * 2) Replace an accumulated resultant UPSERT with an appropriate
 *    REPLACE.
 *     |      ...      |       |     ...      |
 *     |     UPSERT    |       |   REPLACE    |
 *     |               |       |              |    ^
 *     +- oldest vlsn -+   =   +- oldest lsn -+    ^ lsn
 *     |               |       |              |    ^
 *     |    DELETE     |       +--------------+
 *     |      ...      |
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
 * @param oldest_vlsn - the minimal VLSN among all active transactions.
 * @return the iterator or NULL on error (diag is set).
 */
struct vy_write_iterator *
vy_write_iterator_new(const struct key_def *key_def, struct tuple_format *format,
		      struct tuple_format *upsert_format, bool is_primary,
		      bool is_last_level, int64_t oldest_vlsn);

/**
 * Add a mem as a source of iterator.
 * @return 0 on success or not 0 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_add_mem(struct vy_write_iterator *stream, struct vy_mem *mem);

/**
 * Add a run slice as a source of iterator.
 * @return 0 on success or not 0 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_add_slice(struct vy_write_iterator *stream,
			    struct vy_slice *slice, struct vy_run_env *run_env);
/**
 * Start the search. Must be called after *add* methods and
 * before *next* method.
 * @return 0 on success or not 0 on error (diag is set).
 */
int
vy_write_iterator_start(struct vy_write_iterator *stream);

/**
 * Free all resources.
 */
void
vy_write_iterator_cleanup(struct vy_write_iterator *stream);

/**
 * Delete the iterator.
 */
void
vy_write_iterator_delete(struct vy_write_iterator *stream);

/**
 * Get the next statement to write.
 * The user of the write iterator simply expects a stream
 * of statements to write to the output.
 * The tuple *ret is guaranteed to be valid until the next
 *  vy_write_iterator_next call.
 *
 * @param stream - the write iterator.
 * @param ret - a pointer to a pointer where the result will be saved to.
 * @return 0 on success or not 0 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_next(struct vy_write_iterator *stream,
		       const struct tuple **ret);

/**
 * Get the last not-NULL statement that was returned in
 * the last vy_write_iterator_next call.
 * The tuple *ret is guaranteed to be valid until the next
 *  vy_write_iterator_next call.
 *
 * @param stream - the write iterator.
 * @return the tuple or NULL if the valid tuple has been never returned.
 */
const struct tuple *
vy_write_iterator_get_last(struct vy_write_iterator *stream);

#endif /* INCLUDES_TARANTOOL_BOX_VY_WRITE_STREAM_H */

