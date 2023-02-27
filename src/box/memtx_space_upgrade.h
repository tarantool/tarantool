/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_SPACE_UPGRADE)
# include "memtx_space_upgrade_impl.h"
#else /* !defined(ENABLE_SPACE_UPGRADE) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct space;

/** Memtx implementation of space_vtab::prepare_upgrade. */
int
memtx_space_prepare_upgrade(struct space *old_space, struct space *new_space);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_SPACE_UPGRADE) */
