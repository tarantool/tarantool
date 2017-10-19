#ifndef TARANTOOL_BOX_CHECKPOINT_H_INCLUDED
#define TARANTOOL_BOX_CHECKPOINT_H_INCLUDED
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
#include <stddef.h>
#include <stdint.h>

/**
 * This module implements a simple API for working with checkpoints.
 * As checkpoints are, in fact, memtx snapshots, functions exported
 * by this module are C wrappers around corresponding memtx_engine
 * methods.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vclock;

/**
 * Return LSN and vclock (unless @vclock is NULL) of the most
 * recent checkpoint or -1 if there is no checkpoint.
 */
int64_t
checkpoint_last(struct vclock *vclock);

/** Iterator over all existing checkpoints. */
struct checkpoint_iterator {
	const struct vclock *curr;
};

/**
 * Init a checkpoint iterator. The iterator is valid as long
 * as the caller doesn't yield.
 */
static inline void
checkpoint_iterator_init(struct checkpoint_iterator *it)
{
	it->curr = NULL;
}

/**
 * Iterate to the next checkpoint. Return NULL if the current
 * checkpoint is the most recent one.
 *
 * If called on the last iteration, this function positions
 * the iterator to the oldest checkpoint.
 */
const struct vclock *
checkpoint_iterator_next(struct checkpoint_iterator *it);

/**
 * Iterate to the previous checkpoint. Return NULL if the current
 * checkpoint is the oldest one.
 *
 * If called on the first iteration, this function positions
 * the iterator to the newest checkpoint.
 */
const struct vclock *
checkpoint_iterator_prev(struct checkpoint_iterator *it);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_CHECKPOINT_H_INCLUDED */
