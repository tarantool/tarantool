#include "process_title.h"
#include "third_party/proctitle.h"
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

char **process_title_init(int argc, char **argv)
{
	char **argv_copy = init_set_proc_title(argc, argv);
	if (argv_copy == NULL)
		return NULL;

	title_buf_size = get_proc_title_max_length();
	title_buf = malloc(title_buf_size);

	/* ensure process_title_get() always yields a valid string */
	if (title_buf != NULL && title_buf_size != 0)
		title_buf[0] = '\0';

	process_title_set_interpretor_name(argv[0]);

	return argv_copy;
}

void process_title_free(int argc, char **argv)
{
	free(title_buf); title_buf = NULL;
	free(script_name); script_name = NULL;
	free(custom); custom = NULL;
	free(status); status = NULL;

	free_proc_title(argc, argv);
}

const char *process_title_get()
{
	return title_buf;
}

static const char *
short_name(const char *name)
{
	const char *sep = NULL, *p;
	if (name == NULL)
		return NULL;
	for (p = name; *p; p++)
		if (*p == '/')
			sep = p;
	if (sep)
		return sep[1] ? sep + 1 : NULL;
	return name;
}

void process_title_update()
{
	if (title_buf == NULL || title_buf_size == 0)
		return;

	char *output = title_buf;
	char *output_end = title_buf + title_buf_size;
	int rc;
	const char *script_name_short = short_name(script_name);
	const char *interpretor_name_short = short_name(interpretor_name);

	const char *part1 = "tarantool", *part2 = status, *part3 = NULL;

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
		/* omit interpretor name when it is the prefix of scriptname, ex:
		 * tarantool/tarantoolctl
		 */
		if (memcmp(script_name_short, interpretor_name_short,
		           strlen(interpretor_name_short)) != 0)
			part3 = interpretor_name_short;
	}

#define OUTPUT(...) snprintf(output, output_end - output, __VA_ARGS__)
	assert(part1);
	if (part2) {
		if (part3) {
			rc = OUTPUT("%s/%s (%s)", part1, part2, part3);
		} else {
			rc = OUTPUT("%s/%s", part1, part2);
		}
	} else {
		if (part3) {
			rc = OUTPUT("%s (%s)", part1, part3);
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
	/* failed snprintf leaves the buffer in unspecified state hence
	 * explicit NUL termination */
	*output = '\0';
	set_proc_title("%s", title_buf);
}

#define DEFINE_STRING_ACCESSORS(name) \
const char *process_title_get_ ## name() { return name; } \
void process_title_set_ ## name(const char *str) \
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
