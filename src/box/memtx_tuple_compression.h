#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "trivia/config.h"

#if defined(ENABLE_TUPLE_COMPRESSION)
# include "memtx_tuple_compression_impl.h"
#else /* !defined(ENABLE_TUPLE_COMPRESSION) */

#include "tuple.h"

#if defined(__cplusplus)
extern "C" {
#endif

static inline struct tuple *
memtx_tuple_compress(struct tuple *tuple)
{
        (void)tuple;
        unreachable();
        return NULL;
}

static inline struct tuple *
memtx_tuple_decompress(struct tuple *tuple)
{
	(void)tuple;
	unreachable();
	return NULL;
}

static inline struct tuple *
memtx_tuple_maybe_decompress(struct tuple *tuple)
{
        if (!tuple_is_compressed(tuple))
                return tuple;
        return memtx_tuple_decompress(tuple);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_TUPLE_COMPRESSION) */
