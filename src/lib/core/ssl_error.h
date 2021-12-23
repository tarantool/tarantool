/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_SSL)
# include "ssl_error_impl.h"
#else /* !defined(ENABLE_SSL) */

#include <stddef.h>

#include "exception.h"
#include "reflection.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

extern const struct type_info type_SSLError;

#if defined(__cplusplus)
} /* extern "C" */

class SSLError: public Exception {
public:
	SSLError(): Exception(&type_SSLError, NULL, 0) {}
	virtual void raise() { throw this; }
};

#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_SSL) */
