#ifndef TARANTOOL_SSL_CERT_PATH_DISCOVER_H_INCLUDED
#define TARANTOOL_SSL_CERT_PATH_DISCOVER_H_INCLUDED
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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Get default paths of directory where stores OpenSSL certificates
 * for different systems
 */
const char** get_default_cert_dir_paths();

/**
 * Get default paths of OpenSSL certificates file for different systems
 */
const char** get_default_cert_file_paths();

/**
 * Set SSL certificates paths (from system default paths) through
 * OpenSSL env variables if these variables are not set:
 *   - SSL_CERT_DIR - it's a colon-separated list of directories
 *     (like the Unix PATH variable) where stores certiifcates.
 *   - SSL_CERT_FILE - it's a path to certificate file
 * @param overwrite - flag for setenv if overwrite is zero, then the
 * value of name (variable) not changed (from man setenv)
 * @see https://serverfault.com/a/722646
 * @see https://golang.org/src/crypto/x509/root_unix.go
 * @see https://golang.org/src/crypto/x509/root_linux.go
 * @see https://github.com/edenhill/librdkafka/blob/cb69d2a8486344252e0fcaa1f959c4ab2d8afff3/src/rdkafka_ssl.c#L845
 * @see https://www.happyassassin.net/posts/2015/01/12/a-note-about-ssltls-trusted-certificate-stores-and-platforms/
 */
void ssl_cert_paths_discover(int overwrite);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_SSL_CERT_PATH_DISCOVER_H_INCLUDED */
