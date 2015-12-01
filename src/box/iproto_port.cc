/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "iproto_port.h"
#include "iproto_constants.h"

/* m_ - msgpack meta, k_ - key, v_ - value */
struct iproto_header_bin {
	uint8_t m_len;                          /* MP_UINT32 */
	uint32_t v_len;                         /* length */
	uint8_t m_header;                       /* MP_MAP */
	uint8_t k_code;                         /* IPROTO_REQUEST_TYPE */
	uint8_t m_code;                         /* MP_UINT32 */
	uint32_t v_code;                        /* response status */
	uint8_t k_sync;                         /* IPROTO_SYNC */
	uint8_t m_sync;                         /* MP_UINT64 */
	uint64_t v_sync;                        /* sync */
	uint8_t k_schema_id;                    /* IPROTO_SCHEMA_ID */
	uint8_t m_schema_id;                    /* MP_UINT32 */
	uint32_t v_schema_id;                   /* schema_id */
} __attribute__((packed));

static const struct iproto_header_bin iproto_header_bin = {
	0xce, 0, 0x83,
	IPROTO_REQUEST_TYPE, 0xce, 0,
	IPROTO_SYNC, 0xcf, 0,
	IPROTO_SCHEMA_ID, 0xce, 0
};

struct iproto_body_bin {
	uint8_t m_body;                    /* MP_MAP */
	uint8_t k_data;                    /* IPROTO_DATA or IPROTO_ERROR */
	uint8_t m_data;                    /* MP_STR or MP_ARRAY */
	uint32_t v_data_len;               /* string length of array size */
} __attribute__((packed));

static const struct iproto_body_bin iproto_body_bin = {
	0x81, IPROTO_DATA, 0xdd, 0
};

static const struct iproto_body_bin iproto_error_bin = {
	0x81, IPROTO_ERROR, 0xdb, 0
};

extern int sc_version;

/** Return a 4-byte numeric error code, with status flags. */
static inline uint32_t
iproto_encode_error(uint32_t error)
{
	return error | IPROTO_TYPE_ERROR;
}

void
iproto_reply_ok(struct obuf *out, uint64_t sync)
{
	struct iproto_header_bin reply = iproto_header_bin;
	reply.v_len = mp_bswap_u32(sizeof(iproto_header_bin) - 5 + 1);
	reply.v_sync = mp_bswap_u64(sync);
	reply.v_schema_id = mp_bswap_u32(sc_version);
	uint8_t empty_map[1] = { 0x80 };
	obuf_dup_xc(out, &reply, sizeof(reply));
	obuf_dup_xc(out, &empty_map, sizeof(empty_map));
}

void
iproto_reply_error(struct obuf *out, const struct error *e, uint64_t sync)
{
	uint32_t msg_len = strlen(e->errmsg);
	uint32_t errcode = ClientError::get_errcode(e);

	struct iproto_header_bin header = iproto_header_bin;
	struct iproto_body_bin body = iproto_error_bin;

	uint32_t len = sizeof(header) - 5 + sizeof(body) + msg_len;
	header.v_len = mp_bswap_u32(len);
	header.v_code = mp_bswap_u32(iproto_encode_error(errcode));
	header.v_sync = mp_bswap_u64(sync);
	header.v_schema_id = mp_bswap_u32(sc_version);

	body.v_data_len = mp_bswap_u32(msg_len);

	obuf_dup_xc(out, &header, sizeof(header));
	obuf_dup_xc(out, &body, sizeof(body));
	obuf_dup_xc(out, e->errmsg, msg_len);
}

static inline struct iproto_port *
iproto_port(struct port *port)
{
	return (struct iproto_port *) port;
}

enum { SVP_SIZE = sizeof(iproto_header_bin) + sizeof(iproto_body_bin) };

extern "C" void
iproto_port_eof(struct port *ptr)
{
	struct iproto_port *port = iproto_port(ptr);
	/* found == 0 means add_tuple wasn't called at all. */
	if (port->found == 0) {
		if (iproto_prepare_select(port->buf, &port->svp) != 0)
			diag_raise();
	}

	iproto_reply_select(port->buf, &port->svp, port->sync, port->found);
}

int
iproto_prepare_select(struct obuf *buf, struct obuf_svp *svp)
{
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
			uint32_t count)
{
	uint32_t len = obuf_size(buf) - svp->used - 5;

	struct iproto_header_bin header = iproto_header_bin;
	header.v_len = mp_bswap_u32(len);
	header.v_sync = mp_bswap_u64(sync);
	header.v_schema_id = mp_bswap_u32(sc_version);

	struct iproto_body_bin body = iproto_body_bin;
	body.v_data_len = mp_bswap_u32(count);

	char *pos = (char *) obuf_svp_to_ptr(buf, svp);
	memcpy(pos, &header, sizeof(header));
	memcpy(pos + sizeof(header), &body, sizeof(body));
}

extern "C" void
iproto_port_add_tuple(struct port *ptr, struct tuple *tuple)
{
	struct iproto_port *port = iproto_port(ptr);
	if (port->found == 0) {
		/* Found the first tuple, add header. */
		if (iproto_prepare_select(port->buf, &port->svp) != 0)
			diag_raise();
	}
	port->found++;
	tuple_to_obuf(tuple, port->buf);
}

struct port_vtab iproto_port_vtab = {
	iproto_port_add_tuple,
	iproto_port_eof,
};
