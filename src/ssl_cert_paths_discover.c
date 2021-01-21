/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/ssl.h>
#include "ssl_cert_paths_discover.h"
#include "tt_static.h"

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
			if (ent->d_type == DT_REG ||
			    ent->d_type == DT_LNK ||
			    ent->d_type == DT_DIR) {
				is_empty = 0;
				break;
			}
		}
	}

	closedir(dir);
	return is_empty;
}

const char **
get_default_cert_dir_paths()
{
	static const char *default_cert_dir_paths[] = {
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
	return default_cert_dir_paths;
}

const char **
get_default_cert_file_paths()
{
	static const char *default_cert_file_paths[] = {
		/* Fedora/RHEL 6 */
		"/etc/pki/tls/certs/ca-bundle.crt",
		/* CentOS/RHEL 7 */
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
	return default_cert_file_paths;
}

void
ssl_cert_paths_discover(int overwrite)
{
	char *cert_dirs = tt_static_buf();
	char *cert_dirs_end = cert_dirs;

	const char *path = NULL;
	const char **default_dir_paths = get_default_cert_dir_paths();
	for (int i = 0; (path = default_dir_paths[i]); i++) {
		struct stat sb;
		if (stat(path, &sb) != 0 || !S_ISDIR(sb.st_mode))
			continue;

		if (!is_dir_empty(path))
			cert_dirs_end += sprintf(cert_dirs_end, "%s:", path);
	}

	path = NULL;
	const char *cert_file = NULL;
	const char **default_file_paths = get_default_cert_file_paths();
	for (int i = 0; (path = default_file_paths[i]); i++) {
		struct stat sb;
		if (stat(path, &sb) == 0) {
			cert_file = path;
			break;
		}
	}

	if (cert_dirs_end > cert_dirs) {
		*(cert_dirs_end -1) = '\0';
		setenv(X509_get_default_cert_dir_env(), cert_dirs, overwrite);
	}

	if (cert_file)
		setenv(X509_get_default_cert_file_env(), cert_file, overwrite);
}
