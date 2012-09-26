
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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <third_party/crc32.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_log.h>
#include <connector/c/include/tarantool/tnt_snapshot.h>

static void tnt_snapshot_free(struct tnt_stream *s) {
	struct tnt_stream_snapshot *ss = TNT_SSNAPSHOT_CAST(s);
	tnt_log_close(&ss->log);
	tnt_mem_free(s->data);
	s->data = NULL;
}

static int
tnt_snapshot_read_tuple(struct tnt_stream *s, struct tnt_tuple *t)
{
	struct tnt_stream_snapshot *ss = TNT_SSNAPSHOT_CAST(s);

	struct tnt_log_row *row =
		tnt_log_next_to(&ss->log, (union tnt_log_value*)t);

	if (row == NULL && tnt_log_error(&ss->log) == TNT_LOG_EOK)
		return 1;

	return (row) ? 0: -1;
}

/*
 * tnt_snapshot()
 *
 * create and initialize snapshot stream;
 *
 * s - stream pointer, maybe NULL
 * 
 * if stream pointer is NULL, then new stream will be created. 
 *
 * returns stream pointer, or NULL on error.
*/
struct tnt_stream *tnt_snapshot(struct tnt_stream *s)
{
	int allocated = s == NULL;
	s = tnt_stream_init(s);
	if (s == NULL)
		return NULL;
	/* allocating stream data */
	s->data = tnt_mem_alloc(sizeof(struct tnt_stream_snapshot));
	if (s->data == NULL) {
		if (allocated)
			tnt_stream_free(s);
		return NULL;
	}
	memset(s->data, 0, sizeof(struct tnt_stream_snapshot));
	/* initializing interfaces */
	s->read = NULL;
	s->read_request = NULL;
	s->read_reply = NULL;
	s->read_tuple = tnt_snapshot_read_tuple;
	s->write = NULL;
	s->writev = NULL;
	s->free = tnt_snapshot_free;
	/* initializing internal data */
	return s;
}

/*
 * tnt_snapshot_open()
 *
 * open snapshot file and associate it with stream;
 *
 * s - snapshot stream pointer
 * 
 * returns 0 on success, or -1 on error.
*/
int tnt_snapshot_open(struct tnt_stream *s, char *file) {
	struct tnt_stream_snapshot *ss = TNT_SSNAPSHOT_CAST(s);
	return tnt_log_open(&ss->log, file, TNT_LOG_SNAPSHOT);
}

/*
 * tnt_snapshot_close()
 *
 * close snapshot stream; 
 *
 * s - snapshot stream pointer
 * 
 * returns 0 on success, or -1 on error.
*/
void tnt_snapshot_close(struct tnt_stream *s) {
	struct tnt_stream_snapshot *ss = TNT_SSNAPSHOT_CAST(s);
	tnt_log_close(&ss->log);
}

/*
 * tnt_snapshot_error()
 *
 * get stream error status;
 *
 * s - snapshot stream pointer
*/
enum tnt_log_error tnt_snapshot_error(struct tnt_stream *s) {
	return TNT_SSNAPSHOT_CAST(s)->log.error;
}

/*
 * tnt_snapshot_strerror()
 *
 * get stream error status description string;
 *
 * s - snapshot stream pointer
*/
char *tnt_snapshot_strerror(struct tnt_stream *s) {
	return tnt_log_strerror(&TNT_SSNAPSHOT_CAST(s)->log);
}

/*
 * tnt_snapshot_errno()
 *
 * get saved errno;
 *
 * s - snapshot stream pointer
*/
int tnt_snapshot_errno(struct tnt_stream *s) {
	return TNT_SSNAPSHOT_CAST(s)->log.errno_;
}
