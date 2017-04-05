#ifndef TARANTOOL_BOX_GC_H_INCLUDED
#define TARANTOOL_BOX_GC_H_INCLUDED
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

#include <stdint.h>

#include "vclock.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Checkpoint info. */
struct checkpoint_info {
	/** Checkpoint vclock, linked in gc_state.checkpoints. */
	struct vclock vclock;
	/**
	 * Number of active users of this checkpoint.
	 * A checkpoint can't be collected unless @refs is 0.
	 */
	int refs;
};

/** Iterator over checkpoints tracked by gc. */
struct checkpoint_iterator {
	struct vclock *curr;
};

/** Init a checkpoint iterator. */
static inline void
checkpoint_iterator_init(struct checkpoint_iterator *it)
{
	it->curr = NULL;
}

/**
 * Iterate to the next checkpoint.
 * Returns NULL to stop.
 */
const struct checkpoint_info *
checkpoint_iterator_next(struct checkpoint_iterator *it);

#define checkpoint_foreach(it, cpt) \
	for (cpt = checkpoint_iterator_next(it); cpt != NULL; \
	     cpt = checkpoint_iterator_next(it))

/**
 * Initialize the garbage collection state.
 * @snap_dirname is a path to the snapshot directory.
 * Return 0 on success, -1 on failure.
 */
int
gc_init(const char *snap_dirname);

/**
 * Destroy the garbage collection state.
 */
void
gc_free(void);

/**
 * Add a new checkpoint to the garbage collection state.
 * Returns 0 on success, -1 on OOM.
 */
int
gc_add_checkpoint(const struct vclock *vclock);

/**
 * Get the last checkpoint vclock and return its signature.
 * Returns -1 if there are no checkpoints.
 */
int64_t
gc_last_checkpoint(struct vclock *vclock);

/**
 * Pin the last checkpoint so that it cannot be removed by garbage
 * collection. The checkpoint vclock is returned in @vclock.
 * Returns the checkpoint signature or -1 if there are no checkpoints.
 */
int64_t
gc_ref_last_checkpoint(struct vclock *vclock);

/**
 * Unpin a checkpoint that was pinned with gc_pin_last_checkpoint()
 * and retry garbage collection if necessary.
 */
void
gc_unref_checkpoint(struct vclock *vclock);

/**
 * Invoke garbage collection in order to remove files left from
 * checkpoints older than @signature.
 */
void
gc_run(int64_t signature);

/**
 * Return max signature garbage collection has been called for
 * since the server start, or -1 if garbage collection hasn't
 * been called at all.
 */
int64_t
gc_signature(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_GC_H_INCLUDED */
