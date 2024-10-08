/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct ArrowArray;
struct ArrowSchema;

/**
 * Check that the given buffer contains a valid Arrow record batch.
 * @param data The buffer containing a record batch in Arrow IPC format, without
 *             MP_EXT header.
 * @param len  Length of @a data.
 * @retval 1   Couldn't decode Arrow data.
 * @retval 0   Ok.
 */
int
mp_validate_arrow(const char *data, uint32_t len);

/**
 * Using a MsgPack representation of size `len' pointed to by `*data', unpack
 * it to `array' and `schema'. The pointer is advanced. Can not fail, because
 * the data must be validated in advance.
 */
void
arrow_unpack(const char **data, uint32_t len, struct ArrowArray *array,
	     struct ArrowSchema *schema);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
