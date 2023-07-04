/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;
struct tuple_format_map;
struct mpstream;

/** Return the number of bytes an encoded tuple value takes. */
uint32_t
mp_sizeof_tuple(struct tuple *tuple);

/**
 * Decode a tuple value from MsgPack data, including MP_EXT prefix.
 *
 * @param data A buffer.
 * @param format_map A tuple format map used for recovering the tuple's format.
 * @return A pointer to the decoded tuple. NULL in case of an error.
 * @post *data = *data + mp_sizeof_tuple(tuple).
 */
struct tuple *
mp_decode_tuple(const char **data, struct tuple_format_map *format_map);

/**
 * Load a tuple value from the buffer and recover the tuple's format.
 *
 * @param data A buffer.
 * @param format_map A tuple format map used for recovering the tuple's format.
 * @return A pointer to the decoded tuple. NULL in case of an error.
 * @post *data = next value after packed tuple value.
 */
struct tuple *
tuple_unpack(const char **data, struct tuple_format_map *format_map);

/**
 * Load a tuple value from the buffer without recovering the tuple's format.
 *
 * @param data A buffer.
 * @param format_map A tuple format map used for recovering the tuple's format.
 * @return A pointer to the decoded tuple. NULL in case of an error.
 * @post *data = next value after packed tuple value.
 */
struct tuple *
tuple_unpack_without_format(const char **data);

/**
 * Encode a tuple value to a buffer.
 *
 * @param data A buffer.
 * @param tuple An tuple to encode.
 *
 * @return data + mp_sizeof_tuple(tuple).
 */
char *
mp_encode_tuple(char *data, struct tuple *tuple);

/** Encode tuple to MsgPack stream as MP_TUPLE extension. */
void
tuple_to_mpstream_as_ext(struct tuple *tuple, struct mpstream *stream);

/**
 * Check that the given buffer contains a valid tuple.
 * @param data The buffer containing a packed tuple without MP_EXT header.
 * @param len Length of @a data.
 * @retval 1 Couldn't decode the tuple.
 * @retval 0 Ok.
 */
int
mp_validate_tuple(const char *data, uint32_t len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
