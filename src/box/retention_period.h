/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_RETENTION_PERIOD)
#include "retention_period_impl.h"
#else /* !defined(ENABLE_RETENTION_PERIOD) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include "vclock/vclock.h"
#include "xlog.h"

/**
 * Allocate vclock structure. In EE additional memory is allocated,
 * where expiration time is saved.
 */
static inline struct vclock*
retention_vclock_new(void)
{
	return (struct vclock *)xmalloc(sizeof(struct vclock));
}

/**
 * Set expiration time of the @retention_vclock to now + @period.
 */
static inline void
retention_vclock_set(struct vclock *retention_vclock, double period)
{
	(void)retention_vclock;
	(void)period;
}

/**
 * Update expiration time of all files. New period must be saved inside xdir.
 */
static inline void
retention_index_update(struct xdir *xdir, double old_period)
{
	(void)xdir;
	(void)old_period;
}

/**
 * Return vclock of the oldest file, which is protected from garbage collection.
 * Vclock is cleared, if none of the files are protected. Vclock must be
 * non-nil.
 */
static inline void
retention_index_get(vclockset_t *index, struct vclock *vclock)
{
	(void)index;
	assert(vclock != NULL);
	vclock_clear(vclock);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_RETENTION_PERIOD) */
