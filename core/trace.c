/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "tarantool.h"

static int level = 0;
static FILE *trace_fd = NULL;

void __attribute__ ((no_instrument_function, constructor)) trace_init(void)
{
	char *trace = getenv("TARANTOOL_TRACE");

	if (trace == NULL)
		return;

	if (strncmp(trace, "stderr", 6) == 0)
		trace_fd = stderr;
	else
		trace_fd = fopen(trace, "w+");
}

void __attribute__ ((no_instrument_function)) __cyg_profile_func_enter(void *f, void
								       *callsite
								       __attribute__ ((unused)))
{
	if (unlikely(trace_fd != NULL))
		fprintf(trace_fd, "%i %*c%p\n", getpid(), level++, 'E', f);
}

void __attribute__ ((no_instrument_function)) __cyg_profile_func_exit(void *f, void
								      *callsite
								      __attribute__ ((unused)))
{
	if (unlikely(trace_fd != NULL))
		fprintf(trace_fd, "%i %*c%p\n", getpid(), --level, 'X', f);
}
