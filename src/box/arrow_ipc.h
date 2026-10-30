/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_ARROW_IPC)
# include "arrow_ipc_impl.h"
#else /* !defined(ENABLE_ARROW_IPC) */

#include "arrow/abi.h"
#include "diag.h"
#include "error.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct region;

static inline int
arrow_ipc_encode(struct ArrowArray *array, struct ArrowSchema *schema,
		 struct region *region, const char **ret_data,
		 const char **ret_data_end)
{
	(void)array;
	(void)schema;
	(void)region;
	(void)ret_data;
	(void)ret_data_end;
	diag_set(ClientError, ER_UNSUPPORTED, "CE version", "arrow format");
	return -1;
}

static inline int
arrow_ipc_decode(struct ArrowArray *array, struct ArrowSchema *schema,
		 const char *data, const char *data_end)
{
	(void)array;
	(void)schema;
	(void)data;
	(void)data_end;
	diag_set(ClientError, ER_UNSUPPORTED, "CE version", "arrow format");
	return -1;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_ARROW_IPC) */
