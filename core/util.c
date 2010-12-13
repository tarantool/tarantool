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

#ifdef RESOLVE_SYMBOLS
#include <bfd.h>
#endif

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

void *main_stack_frame;
#if defined(__x86) || defined (__amd64) || defined(__i386)
static void
print_trace(FILE *f)
{
	void *dummy = NULL;
	struct frame *frame;
	save_rbp(dummy);
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
#if defined(__x86) || defined (__amd64) || defined(__i386)
	print_trace(stderr);
#endif
	close_all_xcpt(0);
	abort();
}

#ifdef RESOLVE_SYMBOLS
struct fsym *fsyms;
size_t fsyms_count;

int
compare_fsym(const void *_a, const void *_b)
{
	const struct fsym *a = _a, *b = _b;
	if (a->addr > b->addr)
		return 1;
	if (a->addr == b->addr)
		return 0;
	return -1;
}

void __attribute__((constructor))
load_syms(const char *name)
{
	long storage_needed;
	asymbol **symbol_table;
	long number_of_symbols;
	bfd *h;
	char **matching;
	int j;

	bfd_init();
	h = bfd_openr (name, NULL);
	if (h == NULL)
		goto out;

	if (bfd_check_format(h, bfd_archive))
		goto out;

	if (!bfd_check_format_matches(h, bfd_object, &matching))
		goto out;

	storage_needed = bfd_get_symtab_upper_bound(h);

	if (storage_needed <= 0)
                goto out;

	symbol_table = malloc(storage_needed);
	number_of_symbols = bfd_canonicalize_symtab (h, symbol_table);
	if (number_of_symbols < 0)
		goto out;

	for (int i = 0; i < number_of_symbols; i++)
		if (symbol_table[i]->flags & BSF_FUNCTION &&
		    symbol_table[i]->value > 0)
			fsyms_count++;
	j = 0;
	fsyms = malloc(fsyms_count * sizeof(struct fsym));

	for (int i = 0; i < number_of_symbols; i++) {
		struct bfd_section *section;
		unsigned long int vma, size;
		section = bfd_get_section(symbol_table[i]);
		vma = bfd_get_section_vma(h, section);
		size = bfd_get_section_size(section);

		if (symbol_table[i]->flags & BSF_FUNCTION &&
		    vma + symbol_table[i]->value > 0 &&
		    symbol_table[i]->value < size)
		{
			fsyms[j].name = strdup(symbol_table[i]->name);
			fsyms[j].addr = (void *)(uintptr_t)(vma + symbol_table[i]->value);
			fsyms[j].end = (void *)(uintptr_t)(vma + size);
			j++;
		}
	}

	qsort(fsyms, fsyms_count, sizeof(struct fsym), compare_fsym);

out:
	if (symbol_table)
		free(symbol_table);
}

struct fsym *
addr2sym(void *addr)
{
	uint low = 0, high = fsyms_count, middle = -1;
	struct fsym *ret, key = {.addr = addr};

	while(low < high) {
		middle = low + ((high - low) >> 1);
		int diff = compare_fsym(fsyms + middle, &key);

		if (diff < 0) {
			low = middle + 1;
		} else if (diff > 0) {
			high = middle;
		} else {
			ret = fsyms + middle;
			goto out;
		}
	}
	ret = fsyms + middle - 1;
out:
	if (middle != -1 && ret->addr <= key.addr && key.addr < ret->end)
		return ret;
	return NULL;
}

#endif
