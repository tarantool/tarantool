#ifndef TNT_SNAPSHOT_H_INCLUDED
#define TNT_SNAPSHOT_H_INCLUDED

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

#include <tarantool/tnt_log.h>

struct tnt_stream_snapshot {
	struct tnt_log log;
};

#define TNT_SSNAPSHOT_CAST(S) ((struct tnt_stream_snapshot*)(S)->data)

struct tnt_stream *tnt_snapshot(struct tnt_stream *s);

int tnt_snapshot_open(struct tnt_stream *s, char *file);
void tnt_snapshot_close(struct tnt_stream *s);

enum tnt_log_error tnt_snapshot_error(struct tnt_stream *s);
char *tnt_snapshot_strerror(struct tnt_stream *s);
int tnt_snapshot_errno(struct tnt_stream *s);

#endif /* TNT_SNAPSHOT_H_INCLUDED */
