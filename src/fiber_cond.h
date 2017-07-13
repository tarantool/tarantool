#ifndef TARANTOOL_FIBER_COND_H_INCLUDED
#define TARANTOOL_FIBER_COND_H_INCLUDED 1
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

#include <small/rlist.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct ipc_cond {
	struct rlist waiters;
};

/**
 * Initialize a cond - semantics as in POSIX condition variable.
 */
void
ipc_cond_create(struct ipc_cond *c);

/**
 * Finalize a cond. UB if there are fibers waiting for a cond.
 */
void
ipc_cond_destroy(struct ipc_cond *c);

/**
 * Wake one fiber waiting for the cond.
 * Does nothing if no one is waiting.
 */
void
ipc_cond_signal(struct ipc_cond *c);

/**
 * Wake all fibers waiting for the cond.
 */
void
ipc_cond_broadcast(struct ipc_cond *c);

int
ipc_cond_wait_timeout(struct ipc_cond *c, double timeout);

int
ipc_cond_wait(struct ipc_cond *c);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_FIBER_COND_H_INCLUDED */
