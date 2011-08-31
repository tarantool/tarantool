#ifndef TNT_DELETE_H_INCLUDED
#define TNT_DELETE_H_INCLUDED

/*
 * Copyright (C) 2011 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * @defgroup Delete
 * @ingroup Operations
 * @brief Delete operation
 *
 * @{
 */

/**
 * Delete operation with tuple.
 *
 * If bufferization is in use, then request would be placed in
 * internal buffer for later sending. Otherwise, operation
 * would be processed immediately.
 *
 * @param t handler pointer
 * @param reqid user supplied integer value
 * @param ns namespace number
 * @param key tuple object pointer
 * @returns 0 on success, -1 on error
 */
int tnt_delete_tuple(struct tnt *t, int reqid, int ns, struct tnt_tuple *key);

/**
 * Delete operation.
 *
 * If bufferization is in use, then request would be placed in
 * internal buffer for later sending. Otherwise, operation
 * would be processed immediately.
 *
 * @param t handler pointer
 * @param reqid user supplied integer value
 * @param ns namespace number
 * @param key key data
 * @param key key data size
 * @returns 0 on success, -1 on error
 */
int tnt_delete(struct tnt *t, int reqid, int ns, char *key, int key_size);
/** @} */

#endif /* TNT_DELETE_H_INCLUDED */
