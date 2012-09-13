#ifndef TNT_IO_H_INCLUDED
#define TNT_IO_H_INCLUDED

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

enum tnt_error tnt_io_connect(struct tnt_stream_net *s, const char *host, int port);
void tnt_io_close(struct tnt_stream_net *s);

ssize_t tnt_io_flush(struct tnt_stream_net *s);

ssize_t tnt_io_send_raw(struct tnt_stream_net *s, const char *buf, size_t size, int all);
ssize_t tnt_io_sendv_raw(struct tnt_stream_net *s, struct iovec *iov, int count, int all);
ssize_t tnt_io_recv_raw(struct tnt_stream_net *s, char *buf, size_t size, int all);

ssize_t tnt_io_send(struct tnt_stream_net *s, const char *buf, size_t size);
ssize_t tnt_io_sendv(struct tnt_stream_net *s, struct iovec *iov, int count);
ssize_t tnt_io_recv(struct tnt_stream_net *s, char *buf, size_t size);

#endif /* TNT_IO_H_INCLUDED */
