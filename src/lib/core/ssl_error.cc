/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ssl_error.h"

#include <stddef.h>

#include "reflection.h"
#include "trivia/config.h"

#if defined(ENABLE_SSL)
# error unimplemented
#endif

const struct type_info type_SSLError = make_type("SSLError", NULL);
