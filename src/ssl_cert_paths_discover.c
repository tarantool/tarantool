/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/ssl.h>
#include "ssl_cert_paths_discover.h"

static int
is_dir_empty(const char *dir_path)
{
	DIR *dir = opendir(dir_path);
	if (!dir)
		return 1;

	struct dirent *ent;
	int is_empty = 1;
	while ((ent = readdir(dir))) {
		if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..")) {
			is_empty = 0;
			break;
		}
	}

	closedir(dir);
	return is_empty;
}

/**
 * Default paths of directory where stores OpenSSL certificates
 * for different systems
 */
const char *default_cert_dir_paths[] = {
	/* Fedora/RHEL/Centos */
	"/etc/pki/tls/certs",
	/* Debian/Ubuntu/Gentoo etc. (OpenSuse) */
	"/etc/ssl/certs",
	/* FreeBSD */
	"/usr/local/share/certs",
	/* NetBSD */
	"/etc/openssl/certs",
	/* macOS */
	"/private/etc/ssl/certs",
	"/usr/local/etc/openssl@1.1/certs",
	"/usr/local/etc/openssl@1.0/certs",
	"/usr/local/etc/openssl/certs",
	NULL
};

/**
 * Default paths of OpenSSL certificates file for different systems
 */
const char *default_cert_file_paths[] = {
	/* Fedora/RHEL 6 */
	"/etc/pki/tls/certs/ca-bundle.crt",
	/* CentOS/RHEL 7/8 */
	"/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
	/* Debian/Ubuntu/Gentoo etc. */
	"/etc/ssl/certs/ca-certificates.crt",
	/* OpenSUSE */
	"/etc/ssl/ca-bundle.pem",
	/* FreeBSD */
	"/usr/local/share/certs/ca-root-nss.crt",
	/* macOS */
	"/private/etc/ssl/cert.pem",
	"/usr/local/etc/openssl@1.1/cert.pem",
	"/usr/local/etc/openssl@1.0/cert.pem",
	NULL
};

int
ssl_cert_paths_discover(int overwrite)
{
	int rc = -1;
	struct stat sb;
	size_t dir_buf_end = 0;
	size_t initial_size = 32;
	size_t buf_size = initial_size;
	char *dir_buf = (char *) malloc(buf_size);

	if (dir_buf == NULL)
		goto fail;

	for (const char **path = default_cert_dir_paths; *path != NULL; path++) {
		if ((stat(*path, &sb) != 0) ||
		    (!S_ISDIR(sb.st_mode)) ||
		    (is_dir_empty(*path) != 0))
			continue;

		size_t path_len = strlen(*path);
		size_t needed = dir_buf_end + path_len + 1;
		if (needed > buf_size) {
			if (buf_size * 2 >= needed)
				buf_size *= 2;
			else
				buf_size = needed * 2;
			char *new_buf = (char *) realloc(dir_buf, buf_size);
			if (new_buf == NULL)
				goto fail;
			dir_buf = new_buf;
		}
		memcpy(dir_buf + dir_buf_end, *path, path_len);
		dir_buf_end += path_len;
		dir_buf[dir_buf_end++] = ':';
	}

	const char *cert_file = NULL;
	for (const char **path = default_cert_file_paths; *path != NULL; path++) {
		if (stat(*path, &sb) == 0) {
			cert_file = *path;
			break;
		}
	}

	if (dir_buf_end > 0) {
		dir_buf[dir_buf_end - 1] = '\0';
		setenv(X509_get_default_cert_dir_env(), dir_buf, overwrite);
	}

	if (cert_file)
		setenv(X509_get_default_cert_file_env(), cert_file, overwrite);

	rc = 0;

fail:
	free(dir_buf);
	return rc;
}
