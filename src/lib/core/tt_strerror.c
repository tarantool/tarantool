/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tt_strerror.h"

#include <stdio.h>
#include <string.h>

#include "tt_static.h"
#include "trivia/config.h"

const char *
tt_strerror(int errnum)
{
#ifdef HAVE_STRERROR_R_GNU
	return strerror_r(errnum, tt_static_buf(), TT_STATIC_BUF_LEN);
#else
	char *buf = tt_static_buf();
	if (strerror_r(errnum, buf, TT_STATIC_BUF_LEN) != 0)
		snprintf(buf, TT_STATIC_BUF_LEN, "Unknown error %03d", errnum);
	return buf;
#endif
}
