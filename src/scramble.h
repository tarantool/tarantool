#ifndef INCLUDES_TARANTOOL_SCRAMBLE_H
#define INCLUDES_TARANTOOL_SCRAMBLE_H
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#endif
/**
 * These are the core bits of the built-in Tarantool
 * authentication. They implement the same algorithm as
 * in MySQL 4.1 authentication:
 *
 * SERVER:  seed = create_random_string()
 *          send(seed)
 *
 * CLIENT:  recv(seed)
 *          hash1 = sha1("password")
 *          hash2 = sha1(hash1)
 *          reply = xor(hash1, sha1(seed, hash2))
 *
 *          ^^ these steps are done in scramble_prepare()
 *
 *          send(reply)
 *
 *
 * SERVER:  recv(reply)
 *
 *          hash1 = xor(reply, sha1(seed, hash2))
 *          candidate_hash2 = sha1(hash1)
 *          check(candidate_hash2 == hash2)
 *
 *          ^^ these steps are done in scramble_check()
 */

enum { SCRAMBLE_SIZE = 20, SCRAMBLE_BASE64_SIZE = 28 };

/**
 * Prepare a scramble (cipher) to send over the wire
 * to the server for authentication.
 */
void
scramble_prepare(void *out, const void *salt, const void *password,
		 int password_len);


/**
 * Verify a password.
 *
 * @retval 0  passwords do match
 * @retval !0 passwords do not match
 */
int
scramble_check(const void *scramble, const void *salt, const void *hash2);


/**
 * Prepare a password hash as is stored in the _user space.
 * @pre out must be at least SCRAMBLE_BASE64_SIZE
 * @post out contains base64_encode(sha1(sha1(password)), 0)
 */
void
password_prepare(const char *password, int len, char *out, int out_len);

/**
 * Given a scramble received from a client, salt sent to client,
 * salt received from another instance and user hash2, recalculate
 * a scramble to be sent to a remote instance for authentication.
 */
void
scramble_reencode(void *out, const void *in, const void *salt,
		  const void *msalt, const void *hash2);

#if defined(__cplusplus)
} /* extern "C" */
#endif
#endif /* INCLUDES_TARANTOOL_SCRAMBLE_H */
