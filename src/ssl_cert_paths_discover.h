#ifndef TARANTOOL_SSL_CERT_PATH_DISCOVER_H_INCLUDED
#define TARANTOOL_SSL_CERT_PATH_DISCOVER_H_INCLUDED
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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Set SSL certificates paths (from system default paths) through
 * OpenSSL env variables:
 *   - SSL_CERT_DIR - it's a colon-separated list of directories
 *     (like the Unix PATH variable) where stores certificates.
 *   - SSL_CERT_FILE - it's a path to certificate file
 * @param overwrite - flag for setenv. If this param is zero, then user
 * defined OpenSSL env variables won't be overwritten.
 * @return 0 on success
 */
int ssl_cert_paths_discover(int overwrite);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_SSL_CERT_PATH_DISCOVER_H_INCLUDED */
