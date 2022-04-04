/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "audit.h"

#include "say.h"
#include "trivia/config.h"

#if defined(ENABLE_AUDIT_LOG)
# error unimplemented
#endif

void
audit_log_init(const char *init_str, int log_nonblock, const char *format,
	       const char *filter)
{
	(void)log_nonblock;
	(void)format;
	(void)filter;
	if (init_str != NULL)
		say_error("audit log is not available in this build");
}
