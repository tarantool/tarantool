#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <stdarg.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C"
{
#endif /* defined(__cplusplus) */

struct region;

/**
 * Return msgpack data length.
 * @param msgpack data
 * @return msgapck data length
 */
size_t
mp_len(const char *data);

/**
 * Duplicate msgpack data.
 * @param msgpack to duplicate
 * @return copy of msgpack data allocated with xmalloc()
 */
char *
mp_dup(const char *data);

/**
 * Format msgpack into string using a static buffer.
 * Useful for debugger. Example: [1, 2, "string"]
 * @param msgpack to format
 * @return formatted null-terminated string
 */
const char *
mp_str(const char *data);

/**
 * Encode variables from the variable argument list according to format string
 * into a buffer allocated on region.
 *
 * @param region Region to allocate buffer for the encoded value.
 * @param size[out] Size of returned encoded value.
 * @param format Null-terminated string containing the structure of the
 * resulting msgpack and the types of the variables from the variable argument
 * list.
 * @param src Variable argument list.
 *
 * @retval Address of the buffer with the encoded value.
 */
const char *
mp_vformat_on_region(struct region *region, size_t *size, const char *format,
		     va_list src);

/**
 * Encode a sequence of values according to format string into a buffer
 * allocated on region.
 *
 * @param region Region to allocate buffer for the encoded value.
 * @param size[out] Size of returned encoded value.
 * @param format Null-terminated string containing the structure of the
 * resulting msgpack and the types of the next arguments.
 * @param ... Values to encode.
 *
 * @retval Address of the buffer with the encoded value.
 */
const char *
mp_format_on_region(struct region *region, size_t *size, const char *format,
		    ...);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
