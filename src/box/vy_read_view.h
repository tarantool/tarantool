#ifndef INCLUDES_TARANTOOL_BOX_VY_READ_VIEW_H
#define INCLUDES_TARANTOOL_BOX_VY_READ_VIEW_H
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

#include <stdbool.h>
#include <stdint.h>

#include <small/rlist.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** The state of the database the cursor should be looking at. */
struct vy_read_view {
	/**
	 * Consistent read view LSN. Originally read-only transactions
	 * receive a read view lsn upon creation and do not see further
	 * changes.
	 * Other transactions are expected to be read-write and
	 * have vlsn == INT64_MAX to read newest data. Once a value read
	 * by such a transaction (T) is overwritten by another
	 * commiting transaction, T permanently goes to read view that does
	 * not see this change.
	 * If T does not have any write statements by the commit time it will
	 * be committed successfully, or aborted as conflicted otherwise.
	 */
	int64_t vlsn;
	/** The link in read_views of the TX manager */
	struct rlist in_read_views;
	/**
	 * The number of references to this read view. The global
	 * read view has zero refs, we don't do reference
	 * count it as it is missing from read_views list.
	 */
	int refs;
};

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_READ_VIEW_H */
