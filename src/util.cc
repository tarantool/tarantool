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
#include "trivia/util.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <cxxabi.h>

#ifdef HAVE_BFD
#include <bfd.h>
#endif /* HAVE_BFD */

#include "fiber.h"
#include "say.h"

#ifndef HAVE_LIBC_STACK_END
void *__libc_stack_end;
#endif

#define CRLF "\n"

/** Find a string in an array of strings.
 *
 * @param haystack  Array of strings. Either NULL
 *                  pointer terminated (for arrays of
 *                  unknown size) or of size hmax.
 * @param needle    string to look for
 * @param hmax      the index to use if nothing is found
 *                  also limits the size of the array
 * @return  string index or hmax if the string is not found.
 */
uint32_t
strindex(const char **haystack, const char *needle, uint32_t hmax)
{
	for (int index = 0; index != hmax && haystack[index]; index++)
		if (strcasecmp(haystack[index], needle) == 0)
			return index;
	return hmax;
}

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

	/* flush buffers to avoid multiple output */
	/* https://github.com/tarantool/tarantool/issues/366 */
	fflush(stdout);
	fflush(stderr);
	if (fork() == 0) {
		close_all_xcpt(0);
#ifdef ENABLE_GCOV
		__gcov_flush();
#endif
		abort();
	}
}

static int
itoa(int val, char *buf)
{
	char *p = buf;

	if (val < 0) {
		*p++ = '-';
		val = -val;
	}
	/* Print full range if it is an unsigned number. */
	unsigned uval = val;
	char *start = p;
	do {
		*p++ = '0' + uval % 10;
		uval /= 10;
	} while (uval > 0);

	int len = (int)(p - buf);

	*p-- = '\0';

	/* Reverse the resulting string. */
	do {
		char tmp = *p;
		*p = *start;
		*start = tmp;
	} while (++start < --p);

	return len;
}

/**
 * Async-signal-safe implementation of printf() into an fd, to be
 * able to write messages into the error log inside a signal
 * handler. Only supports %s and %d, %u, format specifiers.
 */
ssize_t
fdprintf(int fd, const char *format, ...)
{
	ssize_t total = 0;
	char buf[22];
	va_list args;
	va_start(args, format);

	while (*format) {
		const char *start = format;
		ssize_t len, res;
		if (*format++ != '%') {
			while (*format != '\0' && *format != '%')
				format++;
			len = format - start;
			goto out;
		}
		switch (*format++) {
		case '%':
			len = 1;
			break;
		case 's':
			start = va_arg(args, char *);
			if (start == NULL)
				start = "(null)";
			len = strlen(start);
			break;
		case 'd':
		case 'u':
			start = buf;
			len = itoa(va_arg(args, int), buf);
			break;
		default:
			len = 2;
			break;
		}
out:
		res = write(fd, start, len);
		if (res > 0)
			total += res;
		if (res != len)
			break;
	}
	va_end(args);
	return total;
}

#ifndef HAVE_FMEMOPEN
FILE *
fmemopen(void *buf, size_t size, const char *mode)
{
	(void) mode;
	assert(strcmp(mode, "r") == 0);

	FILE *ret = tmpfile();
	fwrite(buf, 1, size, ret);
	rewind(ret);
	return ret;
}
#endif /* HAVE_FMEMOPEN */

#ifdef ENABLE_BACKTRACE

/*
 * We use a global static buffer because it is too late to do any
 * allocation when we are printing backtrace and fiber stack is
 * small.
 */

static char backtrace_buf[4096 * 4];

/*
 * note, stack unwinding code assumes that binary is compiled with
 * frame pointers
 */

struct frame {
	struct frame *rbp;
	void *ret;
};

char *
backtrace(void *frame_, void *stack, size_t stack_size)
{
	struct frame *frame = (struct frame *) frame_;
	void *stack_top = (char *) stack + stack_size;
	void *stack_bottom = stack;

	char *p = backtrace_buf;
	char *end = p + sizeof(backtrace_buf) - 1;
	int frameno = 0;
	while (stack_bottom <= (void *)frame && (void *)frame < stack_top) {
		/**
		 * The stack may be overwritten by the callback
		 * in case of optimized builds.
		 */
		struct frame *prev_frame = frame->rbp;
		p += snprintf(p, end - p, "#%-2d %p in ", frameno++, frame->ret);

		if (p >= end)
			goto out;
#ifdef HAVE_BFD
		struct symbol *s = addr2symbol(frame->ret);
		if (s != NULL) {
			size_t offset = (const char *) frame->ret - (const char *) s->addr;
			p += snprintf(p, end - p, "%s+%" PRI_SZ "",
				      s->name, offset);
		} else
#endif /* HAVE_BFD */
		{
			p += snprintf(p, end - p, "?");
		}
		if (p >= end)
			goto out;
		p += snprintf(p, end - p, CRLF);
		if (p >= end)
			goto out;
#ifdef HAVE_BFD
		if (s != NULL && strcmp(s->name, "main") == 0)
			break;

#endif
		frame = prev_frame;
	}
out:
	*p = '\0';
	return backtrace_buf;
}

void
backtrace_foreach(backtrace_cb cb, void *frame_, void *stack, size_t stack_size,
                  void *cb_ctx)
{
	struct frame *frame = (struct frame *) frame_;
	void *stack_top = (char *) stack + stack_size;
	void *stack_bottom = stack;
	int frameno = 0;
	const char *sym = NULL;
	size_t offset = 0;
	while (stack_bottom <= (void *)frame && (void *)frame < stack_top) {
		/**
		 * The stack may be overwritten by the callback
		 * in case of optimized builds.
		 */
		struct frame *prev_frame = frame->rbp;
#ifdef HAVE_BFD
		struct symbol *s = addr2symbol(frame->ret);
		if (s != NULL) {
			sym = s->name;
			offset = (const char *) frame->ret - (const char *) s->addr;
			if (strcmp(s->name, "main") == 0) {
				cb(frameno, frame->ret, sym, offset, cb_ctx);
				break;
			}
		}
#endif /* HAVE_BFD */
		int rc = cb(frameno, frame->ret, sym, offset, cb_ctx);
		if (rc != 0)
			return;
		frame = prev_frame;
		sym = NULL;
		offset = 0;
		frameno++;
	}
}

void
print_backtrace()
{
	void *frame = __builtin_frame_address(0);
	void *stack_top;
	size_t stack_size;

	if (fiber() == NULL || fiber_name(fiber()) == NULL ||
	    strcmp(fiber_name(fiber()), "sched") == 0) {
		stack_top = frame; /* we don't know where the system stack top is */
		stack_size = (const char *) __libc_stack_end - (const char *) frame;
	} else {
		stack_top = fiber()->coro.stack;
		stack_size = fiber()->coro.stack_size;
	}

	fdprintf(STDERR_FILENO, "%s", backtrace(frame, stack_top, stack_size));
}
#endif /* ENABLE_BACKTRACE */


void __attribute__ ((noreturn))
assert_fail(const char *assertion, const char *file, unsigned int line, const char *function)
{
	fprintf(stderr, "%s:%i: %s: assertion %s failed.\n", file, line, function, assertion);
#ifdef ENABLE_BACKTRACE
	print_backtrace();
#endif /* ENABLE_BACKTRACE */
	close_all_xcpt(0);
	abort();
}

/** Allocate and fill an absolute path to a file. */
char *
abspath(const char *filename)
{
	if (filename[0] == '/')
		return strdup(filename);

	char *abspath = (char *) malloc(PATH_MAX + 1);
	if (abspath == NULL)
		return NULL;

	if (getcwd(abspath, PATH_MAX - strlen(filename) - 1) == NULL)
		say_syserror("getcwd");
	else {
		strcat(abspath, "/");
	}
	strcat(abspath, filename);
	return abspath;
}

char *
int2str(long int val)
{
	static __thread char buf[22];
	snprintf(buf, sizeof(buf), "%ld", val);
	return buf;
}

#ifdef HAVE_BFD
static struct symbol *symbols;
static ssize_t symbol_count;

int
compare_symbol(const void *_a, const void *_b)
{
	const struct symbol *a = (const struct symbol *) _a;
	const struct symbol *b = (const struct symbol *) _b;
	if (a->addr > b->addr)
		return 1;
	if (a->addr == b->addr)
		return 0;
	return -1;
}

void
symbols_load(const char *name)
{
	char *path = find_path(name);
	long storage_needed;
	asymbol **symbol_table = NULL;
	long number_of_symbols;
	bfd *h;
	char **matching;
	int j;

	bfd_init();
	h = bfd_openr(path, NULL);
	if (h == NULL) {
		say_syserror("bfd_open(%s) failed", path);
		goto out;
	}

	if (bfd_check_format(h, bfd_archive)) {
		say_warn("bfd_check_format() failed");
		goto out;
	}

	if (!bfd_check_format_matches(h, bfd_object, &matching)) {
		say_warn("bfd_check_format_matches() failed");
		goto out;
	}

	storage_needed = bfd_get_symtab_upper_bound(h);

	if (storage_needed <= 0) {
		say_warn("storage_needed is out of bounds");
		goto out;
	}

	symbol_table = (asymbol **) malloc(storage_needed);
	if (symbol_table == NULL) {
		say_warn("failed to allocate symbol table");
		goto out;
	}

	number_of_symbols = bfd_canonicalize_symtab (h, symbol_table);
	if (number_of_symbols < 0) {
		say_warn("failed to canonicalize symbol table");
		goto out;
	}

	for (int i = 0; i < number_of_symbols; i++) {
		struct bfd_section *section;
		unsigned long int vma, size;
		section = bfd_get_section(symbol_table[i]);
		vma = bfd_get_section_vma(h, section);
		size = bfd_get_section_size(section);

		/* On ELF (but not elsewhere) use BSF_FUNCTION flag.  */
		bool is_func = (bfd_target_elf_flavour == h->xvec->flavour) ?
			symbol_table[i]->flags & BSF_FUNCTION : 1;

		if (is_func && vma + symbol_table[i]->value > 0 &&
		    symbol_table[i]->value < size)
			symbol_count++;
	}

	if (symbol_count == 0) {
		say_warn("symbol count is 0");
		goto out;
	}

	j = 0;
	symbols = (struct symbol *) malloc(symbol_count * sizeof(struct symbol));
	if (symbols == NULL)
		goto out;

	for (int i = 0; i < number_of_symbols; i++) {
		struct bfd_section *section;
		unsigned long int vma, size;
		section = bfd_get_section(symbol_table[i]);
		vma = bfd_get_section_vma(h, section);
		size = bfd_get_section_size(section);

		/* On ELF (but not elsewhere) use BSF_FUNCTION flag.  */
		bool is_func = (bfd_target_elf_flavour == h->xvec->flavour) ?
			symbol_table[i]->flags & BSF_FUNCTION : 1;

		if (is_func && (vma + symbol_table[i]->value) > 0 &&
		    symbol_table[i]->value < size)
		{
			int status;
			symbols[j].name = abi::__cxa_demangle(symbol_table[i]->name, 0, 0, &status);
			if (symbols[j].name == NULL)
				symbols[j].name = strdup(symbol_table[i]->name);
			symbols[j].addr = (void *)(uintptr_t)(vma + symbol_table[i]->value);
			symbols[j].end = (void *)(uintptr_t)(vma + size);
			j++;
		}
	}
	bfd_close(h);

	qsort(symbols, symbol_count, sizeof(struct symbol), compare_symbol);

	for (int j = 0; j < symbol_count - 1; j++)
		symbols[j].end = MIN((char *) symbols[j].end,
				     (char *) symbols[j + 1].addr - 1);

out:
	if (symbol_count == 0)
		say_warn("no symbols found in %s", path);

	if (symbol_table)
		free(symbol_table);
}

void symbols_free()
{
	for (struct symbol *s = symbols; s < symbols + symbol_count; s++)
		free((void *) s->name);
	free(symbols);
}

/**
 * Sic: this assumes stack direction is from lowest to
 * highest.
 */
struct symbol *
addr2symbol(void *addr)
{
	if (symbols == NULL)
		return NULL;

	struct symbol key;
	key.addr = addr;
	key.name = NULL;
	key.end = NULL;
	struct symbol *low = symbols;
	struct symbol *high = symbols + symbol_count;

	while (low + 1 < high) { /* there are at least two to choose from. */
		struct symbol *middle = low + ((high - low) >> 1);

		int diff = compare_symbol(&key, middle);
		if (diff < 0) { /* key < middle. */
			high = middle;
		} else {/* key >= middle */
			low = middle;
			if (diff == 0)
				break;
		}
	}
	if (low->addr <= key.addr && low->end >= key.addr)
		return low;
	return NULL;
}

#endif /* HAVE_BFD */
