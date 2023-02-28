/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct interval;

/** Return the number of bytes an encoded interval value takes. */
uint32_t
mp_sizeof_interval(const struct interval *itv);

/**
 * Load an interval value from the buffer.
 *
 * @param data A buffer.
 * @param len Buffer size.
 * @param[out] itv Interval to be decoded.
 * @return A pointer to the decoded interval.
 *         NULL in case of an error.
 * @post *data = next value after packed interval value.
 */
struct interval *
interval_unpack(const char **data, uint32_t len, struct interval *itv);

/**
 * Decode an interval value from MsgPack data.
 *
 * @param data A buffer.
 * @param[out] itv Interval to be decoded.
 * @return A pointer to the decoded interval.
 *         NULL in case of an error.
 * @post *data = *data + mp_sizeof_interval(itv).
 */
struct interval *
mp_decode_interval(const char **data, struct interval *itv);

/**
 * Encode an interval value to a buffer.
 *
 * @param data A buffer.
 * @param itv An interval to encode.
 *
 * @return data + mp_sizeof_interval(itv).
 */
char *
mp_encode_interval(char *data, const struct interval *itv);

/**
 * Print interval's string representation into a given buffer.
 *
 * @param buf Target buffer to write string to.
 * @param size Buffer size.
 * @param data MessagePack encoded interval, without MP_EXT header.
 * @param len MessagePack size.
 * @retval < 0 Error.
 * @retval >= 0 How many bytes were written, or would have been written, if
 *              there was enough buffer space.
 */
int
mp_snprint_interval(char *buf, int size, const char **data, uint32_t len);

/**
 * Print interval's string representation into a stream.
 *
 * @param file Target stream to write string to.
 * @param data MessagePack encoded interval, without MP_EXT header.
 * @param len MessagePack size.
 * @retval < 0 Error.
 * @retval >= 0 How many bytes were written.
 */
int
mp_fprint_interval(FILE *file, const char **data, uint32_t len);

/**
 * Check that the given buffer contains a valid interval.
 * @param data The buffer containing a packed interval without MP_EXT header.
 * @param len Length of @a data.
 * @retval 1 Couldn't decode the interval.
 * @retval 0 Ok.
 */
int
mp_validate_interval(const char *data, uint32_t len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
