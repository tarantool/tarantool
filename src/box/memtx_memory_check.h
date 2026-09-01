/*
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#pragma once

#include "trivia/config.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Modes of box.cfg.memtx_memory_recovery_check. */
enum memtx_memory_check_mode {
	MEMTX_MEMORY_CHECK_OFF = 0,
	MEMTX_MEMORY_CHECK_WARN = 1,
	MEMTX_MEMORY_CHECK_PANIC = 2,
};

/** box.cfg.memtx_memory_recovery_check. */
extern enum memtx_memory_check_mode memtx_memory_check_mode;

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#if defined(ENABLE_MEMTX_MEMORY_CHECK)
#include "memtx_memory_check_impl.h"
#else /* !defined(ENABLE_MEMTX_MEMORY_CHECK) */

#include <stdint.h>

struct memtx_engine;
struct recovery;

/**
 * Check that the configured memtx quota is not less than the
 * amount of memory the instance used before restart, as recorded
 * in the header of the newest snap/xlog file.
 */
static inline int
memtx_memory_check_on_recovery(struct memtx_engine *memtx,
			       struct recovery *recovery)
{
	(void)memtx;
	(void)recovery;
	return 0;
}

/**
 * Return the amount of used memtx memory to write to the header
 * of a file about to be created, or 0 if the check is disabled
 * and the MemtxUsed key must not be written.
 */
static inline uint64_t
memtx_memory_check_get_memtx_used(void)
{
	return 0;
}

#endif /* !defined(ENABLE_MEMTX_MEMORY_CHECK) */
