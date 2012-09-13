#ifndef TNT_NET_H_INCLUDED
#define TNT_NET_H_INCLUDED

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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/time.h>

#include <tarantool/tnt_opt.h>
#include <tarantool/tnt_iob.h>

enum tnt_error {
	TNT_EOK,
	TNT_EFAIL,
	TNT_EMEMORY,
	TNT_ESYSTEM,
	TNT_EBIG,
	TNT_ESIZE,
	TNT_ERESOLVE,
	TNT_ETMOUT,
	TNT_EBADVAL,
	TNT_LAST
};

struct tnt_stream_net {
	struct tnt_opt opt;
	int connected;
	int fd;
	struct tnt_iob sbuf;
	struct tnt_iob rbuf;
	enum tnt_error error;
	int errno_;
};

#define TNT_SNET_CAST(S) ((struct tnt_stream_net*)(S)->data)

struct tnt_stream *tnt_net(struct tnt_stream *s);
int tnt_set(struct tnt_stream *s, int opt, ...);
int tnt_init(struct tnt_stream *s);

int tnt_connect(struct tnt_stream *s);
void tnt_close(struct tnt_stream *s);

ssize_t tnt_flush(struct tnt_stream *s);
int tnt_fd(struct tnt_stream *s);

enum tnt_error tnt_error(struct tnt_stream *s);
char *tnt_strerror(struct tnt_stream *s);
int tnt_errno(struct tnt_stream *s);

#ifdef __cplusplus
}
#endif

#endif /* TNT_NET_H_INCLUDED */
