/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Returns string describing error number.
 *
 * The string is allocated from a per-thread static buffer so in contrast
 * to strerror(), this function is MT-Safe.
 */
const char *
tt_strerror(int errnum);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
