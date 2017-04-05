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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vclock;

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

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_GC_H_INCLUDED */
