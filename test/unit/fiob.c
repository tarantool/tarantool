#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>
#include <errno.h>

#include "test.h"
#include <fiob.h>
#include <say.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>



#define PLAN		47

#define ITEMS		7


const char *
catfile(const char *a, const char *b)
{
	size_t la = strlen(a);

	static char r[4096];

	strcpy(r, a);

	if (a[la - 1] == '/' && b[0] == '/') {
		strcat(r, b + 1);
		return r;
	}

	if (a[la - 1]== '/') {
		strcat(r, b);
		return r;
	}
	r[la] = '/';
	r[la + 1] = 0;
	strcat(r, b);
	return r;
}


void sayf(int level, const char *filename, int line, const char *error,
                          const char *format, ...)
{
	const char *dbg = getenv("DEBUG");
	if (!dbg)
		return;
	if (strcmp(dbg, "1") != 0)
		return;

	printf("# ");
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	printf("\n#\tat %s line %d\n", filename, line);
	if (error)
		printf("#\t%s\n", error);
}
sayfunc_t _say = sayf;

int
main(void)
{
	plan(PLAN);

	char *td = mkdtemp(strdup("/tmp/fiob.XXXXXX"));
	isnt(td, NULL, "tempdir is created");

	static char buf[4096];

	{

		FILE *f = fiob_open(catfile(td, "t0"), "w+d");
		isnt(f, NULL, "common open");
		size_t done = fwrite("Hello, world", 1, 12, f);
		is(done, 12, "Hello world is written (%zu bytes)", done);

		is(ftell(f), 12, "current position");
		is(fseek(f, 0L, SEEK_SET), 0, "set new position");
		is(ftell(f), 0, "current position %zu", (size_t) ftell(f));


		done = fread(buf, 1, 12, f);
		is(done, 12, "Hello world is read (%zu bytes)", done);
		is(memcmp(buf, "Hello, world", 12), 0, "data");

		is(fseek(f, 0L, SEEK_SET), 0, "set new position");
		done = fread(buf + 1, 1, 12, f);
		is(done, 12, "Hello world is read (%zu bytes)", done);
		is(memcmp(buf + 1, "Hello, world", 12), 0, "data");


		is(fseek(f, 0L, SEEK_SET), 0, "set new position");
		fwrite("ololo ololo ololo", 1, 17, f);
		is(fseek(f, 1L, SEEK_SET), 0, "set new position");

		done = fread(buf + 1, 1, 12, f);
		is(done, 12, "data is read");
		is(memcmp(buf + 1, "lolo ololo ololo", 12), 0, "data is read");

		is(fclose(f), 0, "fclose");

		f = fopen(catfile(td, "t0"), "r");
		isnt(f, NULL, "reopened file");
		is(fseek(f, 0L, SEEK_END), 0, "move pos at finish");
		is(ftell(f), 17, "file size");
		is(fclose(f), 0, "fclose");

		f = fiob_open(catfile(td, "t0"), "w+x");
		is(f, NULL, "common open: O_EXCL");
	}

	{
		FILE *f = fiob_open(catfile(td, "t1"), "w+");
		isnt(f, NULL, "common open");
		size_t done = fwrite("Hello, world", 1, 12, f);
		is(done, 12, "Hello world is written (%zu bytes)", done);

		is(fseek(f, 1, SEEK_SET), 0, "move pos");
		done = fwrite("Hello, world", 1, 12, f);
		is(done, 12, "Hello world is written (%zu bytes)", done);

		is(fseek(f, 2, SEEK_SET), 0, "move pos");
		done = fread(buf, 1, 12, f);
		is(done, 11, "read 11 bytes");
		is(memcmp(buf, "ello, world", 11), 0, "content was read");

		is(fclose(f), 0, "fclose");
	}

	{
		FILE *f = fiob_open(catfile(td, "tm"), "wxd");
		isnt(f, NULL, "open big file");
		size_t done = fwrite("Hello, world\n", 1, 13, f);
		is(done, 13, "Hello world is written (%zu bytes)", done);

		size_t i;

		for (i = 0; i < 1000000; i++) {
			done += fwrite("Hello, world\n", 1, 13, f);
		}
		is(done, 13 + 13 * 1000000, "all bytes were written");
		is(fclose(f), 0, "fclose");

		f = fopen(catfile(td, "tm"), "r");
		isnt(f, NULL, "reopen file for reading");
		done = 0;
		for (i = 0; i < 1000000 + 1; i++) {
			buf[0] = 0;
			fgets(buf, 4096, f);
			if (strcmp(buf, "Hello, world\n") == 0)
				done++;
		}
		is(done, 1000000 + 1, "all records were written properly");

		is(fgets(buf, 4096, f), NULL, "eof");
		isnt(feof(f), 0, "feof");
		is(fclose(f), 0, "fclose");
	}
	{
		FILE *f = fiob_open(catfile(td, "tm"), "w+d");
		setvbuf(f, NULL, _IONBF, 0);
		isnt(f, NULL, "open big file");
		size_t done = fwrite("Hello, world\n", 1, 13, f);
		is(done, 13, "Hello world is written (%zu bytes)", done);

		size_t i;

		for (i = 0; i < 1000000; i++) {
			done += fwrite("Hello, world\n", 1, 13, f);
		}
		is(done, 13 + 13 * 1000000, "all bytes were written");
		is(fclose(f), 0, "fclose");

		f = fopen(catfile(td, "tm"), "r");
		isnt(f, NULL, "reopen file for reading");
		done = 0;
		for (i = 0; i < 1000000 + 1; i++) {
			memset(buf, 0, 4096);
			fgets(buf, 4096, f);
			if (strcmp(buf, "Hello, world\n") == 0)
				done++;
/*                         else */
/*                                 fprintf(stderr, "#   wrong line %zu: %s", */
/*                                         i, buf); */
		}
		is(done, 1000000 + 1, "all records were written properly");

		is(fgets(buf, 4096, f), NULL, "eof");
		isnt(feof(f), 0, "feof");
		is(fclose(f), 0, "fclose");
	}



	if (fork() == 0)
		execl("/bin/rm", "/bin/rm", "-fr", td, NULL);

	free(td);
	return check_plan();
}
