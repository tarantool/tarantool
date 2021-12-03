/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <string.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct space;

int
msgpuck_compress_fields(struct space *space, const char *data,
                        const char *data_end, char **new_data,
                        char **new_data_end);

int
msgpuck_decompress_fields(struct space *space, const char *data,
                          const char *data_end, char **new_data,
                          size_t new_data_size);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
