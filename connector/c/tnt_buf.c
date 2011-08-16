
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <sys/uio.h>

#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_buf.h>

int
tnt_buf_init(struct tnt_buf *buf, size_t size,
	     tnt_buf_tx_t tx, tnt_buf_txv_t txv, void *ptr)
{
	buf->tx = tx;
	buf->txv = txv;
	buf->ptr = ptr;
	buf->size = size;
	buf->off = 0;
	buf->top = 0;
	buf->buf = NULL;
	if (size > 0) {
		buf->buf = tnt_mem_alloc(size);
		if (buf->buf == NULL)
			return -1;
		memset(buf->buf, 0, size);
	}
	return 0;
}

void
tnt_buf_free(struct tnt_buf *buf)
{
	if (buf->buf)
		tnt_mem_free(buf->buf);
}
