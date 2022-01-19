/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "trivia/config.h"

#if defined(ENABLE_TUPLE_COMPRESSION)
# error unimplemented
#endif

const char *compression_type_strs[] = {
        "none",
};
