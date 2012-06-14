
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

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_io.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_rpl.h>

static const uint32_t tnt_rpl_version = 11;

static void tnt_rpl_free(struct tnt_stream *s) {
	struct tnt_stream_rpl *sr = TNT_RPL_CAST(s);
	if (sr->net) {
		/* network stream should not be free'd here */
		sr->net = NULL;
	}
	tnt_mem_free(s->data);
}

static ssize_t
tnt_rpl_recv_cb(struct tnt_stream *s, char *buf, ssize_t size) {
	struct tnt_stream_net *sn = TNT_SNET_CAST(s);
	return tnt_io_recv(sn, buf, size);
}

static int
tnt_rpl_request(struct tnt_stream *s, struct tnt_request *r)
{
	struct tnt_stream_rpl *sr = TNT_RPL_CAST(s);
	struct tnt_stream_net *sn = TNT_SNET_CAST(sr->net);
	/* fetching header */
	if (tnt_io_recv(sn, (char*)&sr->hdr, sizeof(sr->hdr)) == -1)
		return -1;
	/* fetching row header */
	if (tnt_io_recv(sn, (char*)&sr->row, sizeof(sr->row)) == -1)
		return -1;
	/* preparing pseudo iproto header */
	struct tnt_header hdr_iproto;
	hdr_iproto.type = sr->row.op;
	hdr_iproto.len = sr->hdr.len - sizeof(struct tnt_xlog_row_v11);
	hdr_iproto.reqid = 0;
	/* deserializing operation */
	if (tnt_request_from(r, (tnt_request_t)tnt_rpl_recv_cb,
			     sr->net,
			     &hdr_iproto) == -1)
		return -1;
	return 0;
}

/*
 * tnt_rpl()
 *
 * create and initialize replication stream;
 *
 * s - stream pointer, maybe NULL
 * 
 * if stream pointer is NULL, then new stream will be created. 
 *
 * returns stream pointer, or NULL on error.
*/
struct tnt_stream *tnt_rpl(struct tnt_stream *s)
{
	int allocated = s == NULL;
	s = tnt_stream_init(s);
	if (s == NULL)
		return NULL;
	/* allocating stream data */
	s->data = tnt_mem_alloc(sizeof(struct tnt_stream_rpl));
	if (s->data == NULL)
		goto error;
	memset(s->data, 0, sizeof(struct tnt_stream_rpl));
	/* initializing interfaces */
	s->read = NULL;
	s->read_request = tnt_rpl_request;
	s->read_reply = NULL;
	s->write = NULL;
	s->writev = NULL;
	s->free = tnt_rpl_free;
	/* initializing internal data */
	struct tnt_stream_rpl *sr = TNT_RPL_CAST(s);
	sr->net = NULL;
	return s;
error:
	if (s->data) {
		tnt_mem_free(s->data);
		s->data = NULL;
	}
	if (allocated)
		tnt_stream_free(s);
	return NULL;
}

/*
 * tnt_rpl_open()
 *
 * connect to a server and initialize handshake;
 *
 * s   - replication stream pointer
 * lsn - start lsn 
 *
 * network stream must be properly initialized before
 * this function called (see ttnt_rpl_net, tnt_set).
 * 
 * returns 0 on success, or -1 on error.
*/
int tnt_rpl_open(struct tnt_stream *s, uint64_t lsn)
{
	struct tnt_stream_rpl *sr = TNT_RPL_CAST(s);
	/* intializing connection */
	if (tnt_init(sr->net) == -1)
		return -1;
	if (tnt_connect(sr->net) == -1)
		return -1;
	/* sending initial lsn */
	struct tnt_stream_net *sn = TNT_SNET_CAST(sr->net);
	if (tnt_io_send_raw(sn, (char*)&lsn, sizeof(lsn), 1) == -1)
		return -1;
	/* reading and checking version */
	uint32_t version = 0;
	if (tnt_io_recv_raw(sn, (char*)&version, sizeof(version), 1) == -1)
		return -1;
	if (version != tnt_rpl_version)
		return -1;
	return 0;
}

/*
 * tnt_rpl_close()
 *
 * close a connection; 
 *
 * s - replication stream pointer
 * 
 * returns 0 on success, or -1 on error.
*/
void tnt_rpl_close(struct tnt_stream *s) {
	struct tnt_stream_rpl *sr = TNT_RPL_CAST(s);
	if (sr->net)
		tnt_close(s);
}

/*
 * tnt_rpl_net()
 *
 * get network stream (tnt_stream_net object);
 *
 * s - replication stream pointer
*/
void tnt_rpl_attach(struct tnt_stream *s, struct tnt_stream *net) {
	TNT_RPL_CAST(s)->net = net;
}
