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
