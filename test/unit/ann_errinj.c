/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "errinj.h"

#define ANN_ERRINJ_INIT(name, type, state) {#name, type, state},

/** Local error-injection registry for standalone ANN unit executables. */
struct errinj errinjs[errinj_id_MAX] = {
	ERRINJ_LIST(ANN_ERRINJ_INIT)
};
