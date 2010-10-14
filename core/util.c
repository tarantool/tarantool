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

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <util.h>
#include <fiber.h>

void
close_all_xcpt(int fdc, ...)
{
	int keep[fdc];
	va_list ap;
	struct rlimit nofile;

	va_start(ap, fdc);
	for (int j = 0; j < fdc; j++) {
		keep[j] = va_arg(ap, int);
	}
	va_end(ap);

	if (getrlimit(RLIMIT_NOFILE, &nofile) != 0)
		nofile.rlim_cur = 10000;

	for (int i = 3; i < nofile.rlim_cur; i++) {
		bool found = false;
		for (int j = 0; j < fdc; j++) {
			if (keep[j] == i) {
				found = true;
				break;
			}
		}
		if (!found)
			close(i);
	}
}

void
coredump(int dump_interval)
{
	static time_t last_coredump = 0;
	time_t now = time(NULL);

	if (now - last_coredump < dump_interval)
		return;

	last_coredump = now;

	if (fork() == 0) {
		close_all_xcpt(0);
#ifdef COVERAGE
		__gcov_flush();
#endif
		abort();
	}
}

void *
xrealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (size > 0 && ret == NULL)
		abort();
	return ret;
}

#if CORO_ASM
void
save_rbp(void **rbp)
{
#  if __amd64
	asm("movq %%rbp, %0"::"m"(*rbp));
#  elif __i386
	asm("movl %%ebp, %0"::"m"(*rbp));
#  else
#  error unsupported architecture
#  endif
}

void *main_stack_frame;
static void
print_trace(FILE *f)
{
	void *dummy;
	struct frame *frame;
	save_rbp(&dummy);
	frame = dummy;

	void *stack_top = fiber->coro.stack + fiber->coro.stack_size;
	void *stack_bottom = fiber->coro.stack;
	if (strcmp(fiber->name, "sched") == 0) {
		stack_top = main_stack_frame + 128;
		stack_bottom = frame;
	}
	fprintf(f, "backtrace:\n");
	while (stack_bottom <= (void *)frame && (void *)frame < stack_top) {
		fprintf(f, "  - { frame: %p, pc: %p }\n", frame + 2 * sizeof(void *), frame->ret);
		frame = frame->rbp;
	}
}
#endif

void __attribute__ ((noreturn))
    assert_fail(const char *assertion, const char *file, unsigned int line, const char *function)
{
	fprintf(stderr, "%s:%i: %s: assertion %s failed.\n", file, line, function, assertion);
#if CORO_ASM
	print_trace(stderr);
#endif
	close_all_xcpt(0);
	abort();
}
