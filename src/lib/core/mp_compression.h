#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "trivia/config.h"

#if defined(ENABLE_TUPLE_COMPRESSION)
# include "mp_compression_impl.h"
#else /* !defined(ENABLE_TUPLE_COMPRESSION) */

#include <stdint.h>
#include <stdio.h>
#include "tt_compression.h"
#include <trivia/util.h>

#if defined(__cplusplus)
extern "C" {
#endif

static inline int
mp_snprint_compression(char *buf, int size, const char **data, uint32_t len)
{
        (void)buf;
        (void)size;
        (void)data;
        (void)len;
        unreachable();
        return -1;
}

static inline int
mp_fprint_compression(FILE *file, const char **data, uint32_t len)
{
        (void)file;
        (void)data;
        (void)len;
        unreachable();
        return -1;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_TUPLE_COMPRESSION) */
