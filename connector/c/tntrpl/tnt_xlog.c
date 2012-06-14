
/*
 * Copyright (C) 2012 Mail.RU
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
#include <stdarg.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <third_party/crc32.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>

static const uint32_t tnt_xlog_marker_v11 = 0xba0babed;
static const uint32_t tnt_xlog_marker_eof_v11 = 0x10adab1e;

inline static int
tnt_xlog_seterr(struct tnt_stream_xlog *s, enum tnt_xlog_error e) {
	s->error = e;
	if (e == TNT_XLOG_ESYSTEM)
		s->errno_ = errno;
	return -1;
}

static void tnt_xlog_free(struct tnt_stream *s) {
	struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(s);
	if (sx->file) {
		tnt_mem_free(sx->file);
		sx->file = NULL;
	}
	if (sx->fd) {
		fclose(sx->fd);
		sx->fd = NULL;
	}
	tnt_mem_free(s->data);
}

inline static int
tnt_xlog_eof(struct tnt_stream_xlog *s, unsigned char *data) {
	uint32_t marker = 0;
	if (data)
		tnt_mem_free(data);
	/* checking eof condition */
	if (ftello(s->fd) == s->offset + sizeof(tnt_xlog_marker_eof_v11)) {
		fseeko(s->fd, s->offset, SEEK_SET);
		if (fread(&marker, sizeof(marker), 1, s->fd) != 1)
			return tnt_xlog_seterr(s, TNT_XLOG_ESYSTEM);
		else
		if (marker != tnt_xlog_marker_eof_v11)
			return tnt_xlog_seterr(s, TNT_XLOG_ECORRUPT);
		s->offset = ftello(s->fd);
	}
	/* eof */
	return 1;
}

static int
tnt_xlog_request(struct tnt_stream *s, struct tnt_request *r)
{
	struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(s);
	/* reading marker */
	unsigned char *data = NULL;
	uint32_t marker = 0;
	if (fread(&marker, sizeof(marker), 1, sx->fd) != 1)
		return tnt_xlog_eof(sx, data);

	/* seeking for marker if necessary */
	while (marker != tnt_xlog_marker_v11) {
		int c = fgetc(sx->fd);
		if (c == EOF)
			return tnt_xlog_eof(sx, data);
		marker = marker >> 8 | ((uint32_t) c & 0xff) <<
			 (sizeof(marker) * 8 - 8);
	}

	/* reading header */
	if (fread(&sx->hdr, sizeof(sx->hdr), 1, sx->fd) != 1)
		return tnt_xlog_eof(sx, data);

	/* checking header crc, starting from lsn */
	uint32_t crc32_hdr =
		crc32c(0, (unsigned char*)&sx->hdr + sizeof(uint32_t),
		       sizeof(struct tnt_xlog_header_v11) -
		       sizeof(uint32_t));
	if (crc32_hdr != sx->hdr.crc32_hdr)
		return tnt_xlog_seterr(sx, TNT_XLOG_ECORRUPT);

	/* allocating memory and reading data */
	data = tnt_mem_alloc(sx->hdr.len);
	if (data == NULL)
		return tnt_xlog_seterr(sx, TNT_XLOG_EMEMORY);
	if (fread(data, sx->hdr.len, 1, sx->fd) != 1)
		return tnt_xlog_eof(sx, data);

	/* checking data crc */
	uint32_t crc32_data = crc32c(0, data, sx->hdr.len);
	if (crc32_data != sx->hdr.crc32_data) {
		tnt_mem_free(data);
		return tnt_xlog_seterr(sx, TNT_XLOG_ECORRUPT);
	}

	/* copying row data */
	memcpy(&sx->row, data, sizeof(struct tnt_xlog_row_v11));

	/* preparing pseudo iproto header */
	struct tnt_header hdr_iproto;
	hdr_iproto.type = sx->row.op;
	hdr_iproto.len = sx->hdr.len - sizeof(struct tnt_xlog_row_v11);
	hdr_iproto.reqid = 0;

	/* deserializing operation */
	tnt_request_init(r);
	size_t off = 0;
	int rc = tnt_request(r, (char*)data + sizeof(struct tnt_xlog_row_v11),
			     sx->hdr.len - sizeof(struct tnt_xlog_row_v11),
			     &off,
			     &hdr_iproto);
	tnt_mem_free(data);
	/* in case of not completed request or parsing error */
	if (rc != 0)
		return tnt_xlog_seterr(sx, TNT_XLOG_ECORRUPT);
	/* updating offset */
	sx->offset = ftello(sx->fd);
	return 0;
}

/*
 * tnt_xlog()
 *
 * create and initialize xlog stream;
 *
 * s - stream pointer, maybe NULL
 * 
 * if stream pointer is NULL, then new stream will be created. 
 *
 * returns stream pointer, or NULL on error.
*/
struct tnt_stream *tnt_xlog(struct tnt_stream *s)
{
	int allocated = s == NULL;
	s = tnt_stream_init(s);
	if (s == NULL)
		return NULL;
	/* allocating stream data */
	s->data = tnt_mem_alloc(sizeof(struct tnt_stream_xlog));
	if (s->data == NULL) {
		if (allocated)
			tnt_stream_free(s);
		return NULL;
	}
	memset(s->data, 0, sizeof(struct tnt_stream_xlog));
	/* initializing interfaces */
	s->read = NULL;
	s->read_request = tnt_xlog_request;
	s->read_reply = NULL;
	s->write = NULL;
	s->writev = NULL;
	s->free = tnt_xlog_free;
	/* initializing internal data */
	struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(s);
	sx->file = NULL;
	sx->fd = NULL;
	return s;
}

inline static int
tnt_xlog_open_err(struct tnt_stream_xlog *s, enum tnt_xlog_error e) {
	tnt_xlog_seterr(s, e);
	if (s->fd) {
		fclose(s->fd);
		s->fd = NULL;
	}
	return -1;
}

static int tnt_xlog_open_init(struct tnt_stream *s)
{
	struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(s);
	char filetype[32];
	char version[32];
	char *rc;
	/* trying to open file */
	sx->fd = fopen(sx->file, "r");
	if (sx->fd == NULL)
		return tnt_xlog_open_err(sx, TNT_XLOG_ESYSTEM);
	/* reading xlog filetype */
	rc = fgets(filetype, sizeof(filetype), sx->fd);
	if (rc == NULL)
		return tnt_xlog_open_err(sx, TNT_XLOG_ESYSTEM);
	/* reading xlog version */
	rc = fgets(version, sizeof(version), sx->fd);
	if (rc == NULL)
		return tnt_xlog_open_err(sx, TNT_XLOG_ESYSTEM);
	/* checking type */
	if (strcmp(filetype, "XLOG\n"))
		return tnt_xlog_open_err(sx, TNT_XLOG_ETYPE);
	/* checking version */
	if (strcmp(version, "0.11\n"))
		return tnt_xlog_open_err(sx, TNT_XLOG_EVERSION);
	for (;;) {
		char buf[256];
		rc = fgets(buf, sizeof(buf), sx->fd);
		if (rc == NULL)
			return tnt_xlog_open_err(sx, TNT_XLOG_EFAIL);
		if (strcmp(rc, "\n") == 0 || strcmp(rc, "\r\n") == 0)
			break;
	}
	/* getting current offset */
	sx->offset = ftello(sx->fd);
	return 0;
}

/*
 * tnt_xlog_open()
 *
 * open xlog file and associate it with stream;
 *
 * s - xlog stream pointer
 * 
 * returns 0 on success, or -1 on error.
*/
int tnt_xlog_open(struct tnt_stream *s, char *file) {
	struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(s);
	sx->file = tnt_mem_dup(file);
	if (sx->file == NULL)
		return tnt_xlog_seterr(sx, TNT_XLOG_EMEMORY);
	if (tnt_xlog_open_init(s) == -1) {
		tnt_mem_free(sx->file);
		sx->file = NULL;
		return -1;
	}
	tnt_xlog_seterr(sx, TNT_XLOG_EOK);
	return 0;
}

/*
 * tnt_xlog_close()
 *
 * close xlog stream; 
 *
 * s - xlog stream pointer
 * 
 * returns 0 on success, or -1 on error.
*/
void tnt_xlog_close(struct tnt_stream *s) {
	struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(s);
	if (sx->file) {
		tnt_mem_free(sx->file);
		sx->file = NULL;
	}
	if (sx->fd) {
		fclose(sx->fd);
		sx->fd = NULL;
	}
}

/*
 * tnt_xlog_error()
 *
 * get stream error status;
 *
 * s - xlog stream pointer
*/
enum tnt_xlog_error tnt_xlog_error(struct tnt_stream *s) {
	return TNT_SXLOG_CAST(s)->error;
}

/* must be in sync with enum tnt_xlog_error */

struct tnt_xlog_error_desc {
	enum tnt_xlog_error type;
	char *desc;
};

static struct tnt_xlog_error_desc tnt_xlog_error_list[] = 
{
	{ TNT_XLOG_EOK,      "ok"                                },
	{ TNT_XLOG_EFAIL,    "fail"                              },
	{ TNT_XLOG_EMEMORY,  "memory allocation failed"          },
	{ TNT_XLOG_ETYPE,    "xlog type mismatch"                },
	{ TNT_XLOG_EVERSION, "xlog version mismatch"             },
	{ TNT_XLOG_ECORRUPT, "xlog crc failed or bad eof marker" },
	{ TNT_XLOG_ESYSTEM,  "system error"                      },
	{ TNT_XLOG_LAST,      NULL                               }
};

/*
 * tnt_xlog_strerror()
 *
 * get stream error status description string;
 *
 * s - xlog stream pointer
*/
char *tnt_xlog_strerror(struct tnt_stream *s) {
	struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(s);
	if (sx->error == TNT_XLOG_ESYSTEM) {
		static char msg[256];
		snprintf(msg, sizeof(msg), "%s (errno: %d)",
			 strerror(sx->errno_), sx->errno_);
		return msg;
	}
	return tnt_xlog_error_list[(int)sx->error].desc;
}

/*
 * tnt_xlog_errno()
 *
 * get saved errno;
 *
 * s - xlog stream pointer
*/
int tnt_xlog_errno(struct tnt_stream *s) {
	return TNT_SXLOG_CAST(s)->errno_;
}
