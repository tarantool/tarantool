#ifndef TARANTOOL_BACKTRACE_H_INCLUDED
#define TARANTOOL_BACKTRACE_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "trivia/config.h"
#include <stddef.h>

extern void *__libc_stack_end;

#ifdef ENABLE_BACKTRACE
void print_backtrace();

char *
backtrace(void *frame, void *stack, size_t stack_size);

typedef int (backtrace_cb)(int frameno, void *frameret,
                           const char *func, size_t offset, void *cb_ctx);

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

void
backtrace_foreach(backtrace_cb cb, void *frame, void *stack,
		  size_t stack_size, void *cb_ctx);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* ENABLE_BACKTRACE */


#ifdef HAVE_BFD
void
symbols_load(const char *name);

void
symbols_free();
#endif /* HAVE_BFD */
#endif /* TARANTOOL_BACKTRACE_H_INCLUDED */
