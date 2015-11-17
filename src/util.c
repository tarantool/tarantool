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
#include <limits.h>

#include "say.h"

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
int2str(long long int val)
{
	static __thread char buf[22];
	snprintf(buf, sizeof(buf), "%lld", val);
	return buf;
}

