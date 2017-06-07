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
#include "trivia/config.h"
#include "iproto_port.h"
#include "iproto_constants.h"
#include "small/obuf.h"
#include "error.h"
#include <unistd.h>
#include <fcntl.h>
#include "xrow.h"


struct PACKED iproto_body_bin {
	uint8_t m_body;                    /* MP_MAP */
	uint8_t k_data;                    /* IPROTO_DATA or IPROTO_ERROR */
	uint8_t m_data;                    /* MP_STR or MP_ARRAY */
	uint32_t v_data_len;               /* string length of array size */
};

static const struct iproto_body_bin iproto_body_bin = {
	0x81, IPROTO_DATA, 0xdd, 0
};

static const struct iproto_body_bin iproto_error_bin = {
	0x81, IPROTO_ERROR, 0xdb, 0
};

/** Return a 4-byte numeric error code, with status flags. */
static inline uint32_t
iproto_encode_error(uint32_t error)
{
	return error | IPROTO_TYPE_ERROR;
}

void
iproto_reply_ok(struct obuf *out, uint64_t sync, uint32_t schema_version)
{
	char *buf = (char *)obuf_alloc_xc(out, IPROTO_HEADER_LEN + 1);
	iproto_header_encode(buf, IPROTO_OK, sync, schema_version, 1);
	buf[IPROTO_HEADER_LEN] = 0x80; /* empty MessagePack Map */
}

int
iproto_reply_error(struct obuf *out, const struct error *e, uint64_t sync,
		   uint32_t schema_version)
{
	uint32_t msg_len = strlen(e->errmsg);
	uint32_t errcode = ClientError::get_errcode(e);

	struct iproto_body_bin body = iproto_error_bin;
	char *header = (char *)obuf_alloc(out, IPROTO_HEADER_LEN);
	if (header == NULL)
		return -1;

	iproto_header_encode(header, iproto_encode_error(errcode), sync,
			     schema_version, sizeof(body) + msg_len);
	body.v_data_len = mp_bswap_u32(msg_len);
	/* Malformed packet appears to be a lesser evil than abort. */
	return obuf_dup(out, &body, sizeof(body)) != sizeof(body) ||
	       obuf_dup(out, e->errmsg, msg_len) != msg_len ? -1 : 0;

}

void
iproto_write_error(int fd, const struct error *e, uint32_t schema_version)
{
	uint32_t msg_len = strlen(e->errmsg);
	uint32_t errcode = ClientError::get_errcode(e);

	char header[IPROTO_HEADER_LEN];
	struct iproto_body_bin body = iproto_error_bin;

	iproto_header_encode(header, iproto_encode_error(errcode), 0,
			     schema_version, sizeof(body) + msg_len);

	body.v_data_len = mp_bswap_u32(msg_len);

	/* Set to blocking to write the error. */
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return;

	(void) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	size_t r = write(fd, header, sizeof(header));
	r = write(fd, &body, sizeof(body));
	r = write(fd, e->errmsg, msg_len);
	(void) r;

	(void) fcntl(fd, F_SETFL, flags);
}

enum { SVP_SIZE = IPROTO_HEADER_LEN  + sizeof(iproto_body_bin) };

int
iproto_prepare_select(struct obuf *buf, struct obuf_svp *svp)
{
	/**
	 * Reserve memory before taking a savepoint.
	 * This ensures that we get a contiguous chunk of memory
	 * and the savepoint is pointing at the beginning of it.
	 */
	void *ptr = obuf_reserve(buf, SVP_SIZE);
	if (ptr == NULL) {
		diag_set(OutOfMemory, SVP_SIZE, "obuf", "reserve");
		return -1;
	}
	*svp = obuf_create_svp(buf);
	ptr = obuf_alloc(buf, SVP_SIZE);
	assert(ptr !=  NULL);
	return 0;
}

void
iproto_reply_select(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		    uint32_t schema_version, uint32_t count)
{
	char *pos = (char *) obuf_svp_to_ptr(buf, svp);
	iproto_header_encode(pos, IPROTO_OK, sync, schema_version,
			        obuf_size(buf) - svp->used -
				IPROTO_HEADER_LEN);

	struct iproto_body_bin body = iproto_body_bin;
	body.v_data_len = mp_bswap_u32(count);

	memcpy(pos + IPROTO_HEADER_LEN, &body, sizeof(body));
}
