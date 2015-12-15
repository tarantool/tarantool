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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
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

#include "title.h"
#include "proc_title.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static char *title_buf;
size_t title_buf_size;

static char *interpretor_name;
static char *script_name;
static char *custom;
static char *status;

char **title_init(int argc, char **argv)
{
	char **argv_copy = proc_title_init(argc, argv);
	if (argv_copy == NULL)
		return NULL;

	title_buf_size = proc_title_max_length();
	title_buf = malloc(title_buf_size);

	/* ensure title_get() always yields a valid string */
	if (title_buf != NULL && title_buf_size != 0)
		title_buf[0] = '\0';

	title_set_interpretor_name(argv[0]);

	return argv_copy;
}

void title_free(int argc, char **argv)
{
	free(title_buf); title_buf = NULL;
	free(script_name); script_name = NULL;
	free(custom); custom = NULL;
	free(status); status = NULL;

	proc_title_free(argc, argv);
}

const char *title_get()
{
	return title_buf;
}

/**
 * Return a name without preceding path, e.g. /a/b/c -> c.
 * Like basename(), but doesn't modify the subject string.
 * Unlike basename, returns an empty string for directories
 * /a/b/c/
 */
static const char *
my_basename(const char *name)
{
	const char *sep = NULL;
	const char *p;
	if (name == NULL)
		return NULL;
	for (p = name; *p != '\0'; p++) {
		if (*p == '/')
			sep = p;
	}
	if (sep)
		return sep[1] ? sep + 1 : NULL;
	return name;
}

void title_update()
{
	if (title_buf == NULL || title_buf_size == 0)
		return;

	char *output = title_buf;
	char *output_end = title_buf + title_buf_size;
	int rc;
	const char *script_name_short = my_basename(script_name);
	const char *interpretor_name_short = my_basename(interpretor_name);

	const char *part1 = "tarantool", *part2 = NULL, *part3 = status;

	/*
	 * prefix
	 */
	if (script_name_short == NULL) {
		if (interpretor_name_short != NULL) {
			part1 = interpretor_name_short;
		}
	} else if (interpretor_name_short == NULL) {
		part1 = script_name_short;
	} else {
		assert(script_name_short);
		assert(interpretor_name_short);
		part1 = script_name_short;
		/*
		 * Omit interpretor name when it is the prefix of
		 * scriptname, ex: tarantool/tarantoolctl
		 */
		if (memcmp(script_name_short, interpretor_name_short,
		           strlen(interpretor_name_short)) == 0) {
			part1 = script_name_short;
		} else {
			part1 = interpretor_name_short;
			part2 = script_name_short;
		}
	}

#define OUTPUT(...) snprintf(output, output_end - output, __VA_ARGS__)
	assert(part1);
	if (part2) {
		if (part3) {
			rc = OUTPUT("%s %s <%s>", part1, part2, part3);
		} else {
			rc = OUTPUT("%s %s", part1, part2);
		}
	} else {
		if (part3) {
			rc = OUTPUT("%s <%s>", part1, part3);
		} else {
			rc = OUTPUT("%s", part1);
		}
	}
	if (rc < 0 || (output += rc) >= output_end)
		goto done;

	/*
	 * custom title
	 */
	if (custom) {
		rc = OUTPUT(": %s", custom);
		if (rc < 0 || (output += rc) >= output_end)
			goto done;
	}
#undef OUTPUT

done:
	if (output >= output_end) {
		output = output_end - 1;
	}
	/*
	 * failed snprintf leaves the buffer in unspecified state hence
	 * explicit NUL termination
	 */
	*output = '\0';
	proc_title_set("%s", title_buf);
}

#define DEFINE_STRING_ACCESSORS(name) \
const char *title_get_ ## name() { return name; } \
void title_set_ ## name(const char *str) \
{ \
	if (str == NULL || str[0] == '\0') { \
		free(name); name = NULL; \
		return; \
	} \
	size_t len = strlen(str); \
	char *p = realloc(name, len + 1); \
	if (p) { \
		name = memcpy(p, str, len + 1); \
	} \
}

DEFINE_STRING_ACCESSORS(interpretor_name)
DEFINE_STRING_ACCESSORS(script_name)
DEFINE_STRING_ACCESSORS(custom)
DEFINE_STRING_ACCESSORS(status)
