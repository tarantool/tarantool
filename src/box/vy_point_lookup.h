#ifndef INCLUDES_TARANTOOL_BOX_VY_POINT_LOOKUP_H
#define INCLUDES_TARANTOOL_BOX_VY_POINT_LOOKUP_H
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

/**
 * Point lookup is a special case of read iterator that is designed for
 * retrieving one value from an LSM tree by a full key (all parts are present).
 *
 * Iterator collects necessary history of the given key from different sources
 * (txw, cache, mems, runs) that consists of some number of sequential upserts
 * and possibly one terminal statement (replace or delete). The iterator
 * sequentially scans txw, cache, mems and runs until a terminal statement is
 * met. After reading the slices the iterator checks that the list of mems
 * hasn't been changed and restarts if it is the case.
 * After the history is collected the iterator calculates resultant statement
 * and, if the result is the latest version of the key, adds it to cache.
 */

#include "vy_entry.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_lsm;
struct vy_tx;
struct vy_read_view;

/**
 * Given a key that has all index parts (including primary index
 * parts in case of a secondary index), lookup the corresponding
 * tuple in the LSM tree. The tuple is returned in @ret with its
 * reference counter elevated.
 *
 * Note, this function doesn't track the result in the transaction
 * read set, i.e. it is up to the caller to call vy_tx_track() if
 * necessary.
 */
int
vy_point_lookup(struct vy_lsm *lsm, struct vy_tx *tx,
		const struct vy_read_view **rv,
		struct vy_entry key, struct vy_entry *ret);

/**
 * Look up a tuple by key in memory.
 *
 * This function works just like vy_point_lookup() except:
 *
 * - It only scans in-memory level and cache and hence doesn't yield.
 * - It doesn't turn DELETE into NULL so it returns NULL if and only
 *   if no terminal statement matching the key is present in memory
 *   (there still may be statements stored on disk though).
 * - It doesn't account the lookup to LSM tree stats (as it never
 *   descends to lower levels).
 *
 * The function returns 0 on success, -1 on memory allocation error.
 */
int
vy_point_lookup_mem(struct vy_lsm *lsm, const struct vy_read_view **rv,
		    struct vy_entry key, struct vy_entry *ret);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_POINT_LOOKUP_H */
