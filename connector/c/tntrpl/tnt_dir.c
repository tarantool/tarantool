
/*
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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_dir.h>

void tnt_dir_init(struct tnt_dir *d, enum tnt_dir_type type) {
	d->type = type;
	d->path = NULL;
	d->files = NULL;
	d->count = 0;
}

void tnt_dir_free(struct tnt_dir *d) {
	if (d->path) {
		tnt_mem_free(d->path);
		d->path = NULL;
	}
	if (d->files) {
		int i = 0;
		while (i < d->count) {
			if (d->files[i].name)
				tnt_mem_free(d->files[i].name);
			i++;
		}
		tnt_mem_free(d->files);
		d->files = NULL;
	}
}

static int tnt_dir_put(struct tnt_dir *d,
		       int *top, char *name, uint64_t lsn)
{
	if (d->count == *top) {
		*top = (*top == 0) ? 128 : *top * 2;
		d->files = tnt_mem_realloc(d->files, sizeof(struct tnt_dir_file) * *top);
		if (d->files == NULL)
			return -1;
	}
	struct tnt_dir_file *current = &d->files[d->count];
	current->lsn = lsn;
	current->name = (char*)tnt_mem_dup(name);
	if (current->name == NULL)
		return -1;
	d->count++;
	return 0;
}

static int tnt_dir_cmp(const void *_a, const void *_b) {
	const struct tnt_dir_file *a = _a;
	const struct tnt_dir_file *b = _b;
	if (a->lsn == b->lsn)
		return 0;
	return (a->lsn > b->lsn) ? 1: -1;
}

int tnt_dir_scan(struct tnt_dir *d, char *path) {
	d->path = tnt_mem_dup(path);
	if (d->path == NULL)
		return -1;
	DIR *dir = opendir(d->path);
	if (dir == NULL)
		goto error;

	struct dirent *dep = NULL;
	struct dirent de;
	int rc, top = 0;
	while ((rc = readdir_r(dir, &de, &dep)) == 0) {
		if (dep == NULL)
			break;
		if (strcmp(de.d_name, ".") == 0 ||
		    strcmp(de.d_name, "..") == 0)
			continue;

		char *ext = strchr(de.d_name, '.');
		if (ext == NULL)
			continue;

		switch (d->type) {
		case TNT_DIR_XLOG:
			if (strcmp(ext, ".xlog") != 0 && 
			    strcmp(ext, ".xlog.inprogress") != 0)
				continue;
			break;
		case TNT_DIR_SNAPSHOT:
			if (strcmp(ext, ".snap") != 0)
				continue;
			break;
		}

		uint64_t lsn = strtoll(de.d_name, &ext, 10);
		if (lsn == LLONG_MAX || lsn == LLONG_MIN)
			continue;

		rc = tnt_dir_put(d, &top, de.d_name, lsn);
		if (rc == -1)
			goto error;
	}
	if (rc != 0)
		goto error;

	qsort(d->files, d->count, sizeof(struct tnt_dir_file),
	      tnt_dir_cmp);

	closedir(dir);
	return 0;
error:
	if (dir)
		closedir(dir);
	tnt_dir_free(d);
	return -1;
}

int tnt_dir_match_gt(struct tnt_dir *d, uint64_t *out) {
	if (d->count == 0)
		return -1;
	*out = d->files[d->count -1].lsn;
	return 0;
}

int tnt_dir_match_inc(struct tnt_dir *d, uint64_t lsn, uint64_t *out) {
	if (d->count == 0)
		return -1;
	int current = 0;
	int count = d->count;
	while (count > 1) {
		if (d->files[current].lsn <= lsn && lsn <= d->files[current + 1].lsn) {
			*out = d->files[current].lsn;
			return 0;
		}
		current++;
		count--;
	}
	*out = d->files[current].lsn;
	return 0;
}
