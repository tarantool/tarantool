#ifndef TARANTOOL_SWIM_PROTO_H_INCLUDED
#define TARANTOOL_SWIM_PROTO_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "trivia/util.h"
#include "uuid/tt_uuid.h"
#include <arpa/inet.h>
#include <stdbool.h>

/**
 * SWIM binary protocol structures and helpers. Below is a picture
 * of a SWIM message template:
 *
 * +----------Meta section, handled by transport level-----------+
 * | {                                                           |
 * |     SWIM_META_TARANTOOL_VERSION: uint, Tarantool version ID,|
 * |     SWIM_META_SRC_ADDRESS: uint, ip,                        |
 * |     SWIM_META_SRC_PORT: uint, port                          |
 * | }                                                           |
 * +-------------------Protocol logic section--------------------+
 * | {                                                           |
 * |     SWIM_SRC_UUID: 16 byte UUID,                            |
 * |                                                             |
 * |                 AND                                         |
 * |                                                             |
 * |     SWIM_ANTI_ENTROPY: [                                    |
 * |         {                                                   |
 * |             SWIM_MEMBER_STATUS: uint, enum member_status,   |
 * |             SWIM_MEMBER_ADDRESS: uint, ip,                  |
 * |             SWIM_MEMBER_PORT: uint, port,                   |
 * |             SWIM_MEMBER_UUID: 16 byte UUID                  |
 * |         },                                                  |
 * |         ...                                                 |
 * |     ],                                                      |
 * | }                                                           |
 * +-------------------------------------------------------------+
 */

enum swim_member_status {
	/** The instance is ok, responds to requests. */
	MEMBER_ALIVE = 0,
	swim_member_status_MAX,
};

extern const char *swim_member_status_strs[];

/**
 * SWIM member attributes from anti-entropy and dissemination
 * messages.
 */
struct swim_member_def {
	struct tt_uuid uuid;
	struct sockaddr_in addr;
	enum swim_member_status status;
};

/** Initialize the definition with default values. */
void
swim_member_def_create(struct swim_member_def *def);

/**
 * Decode member definition from a MessagePack buffer.
 * @param[out] def Definition to decode into.
 * @param[in][out] pos Start of the MessagePack buffer.
 * @param end End of the MessagePack buffer.
 * @param prefix A prefix of an error message to use for
 *        diag_set, when something is wrong.
 *
 * @retval 0 Success.
 * @retval -1 Error.
 */
int
swim_member_def_decode(struct swim_member_def *def, const char **pos,
		       const char *end, const char *prefix);

/**
 * Main round messages can carry merged failure detection,
 * anti-entropy, dissemination messages. With these keys the
 * components can be distinguished from each other.
 */
enum swim_body_key {
	SWIM_SRC_UUID = 0,
	SWIM_ANTI_ENTROPY,
};

/**
 * One of SWIM packet body components - SWIM_SRC_UUID. It is not
 * in the meta section, handled by the transport, because the
 * transport has nothing to do with UUIDs - it operates by IP/port
 * only. This component shall be first in message's body.
 */
struct PACKED swim_src_uuid_bin {
	/** mp_encode_uint(SWIM_SRC_UUID) */
	uint8_t k_uuid;
	/** mp_encode_bin(UUID_LEN) */
	uint8_t m_uuid;
	uint8_t m_uuid_len;
	uint8_t v_uuid[UUID_LEN];
};

/** Initialize source UUID section. */
void
swim_src_uuid_bin_create(struct swim_src_uuid_bin *header,
			 const struct tt_uuid *uuid);

/** {{{                  Anti-entropy component                 */

/**
 * Attributes of each record of a broadcasted member table. Just
 * the same as some of struct swim_member attributes.
 */
enum swim_member_key {
	SWIM_MEMBER_STATUS = 0,
	SWIM_MEMBER_ADDRESS,
	SWIM_MEMBER_PORT,
	SWIM_MEMBER_UUID,
	swim_member_key_MAX,
};

/** SWIM anti-entropy MessagePack header template. */
struct PACKED swim_anti_entropy_header_bin {
	/** mp_encode_uint(SWIM_ANTI_ENTROPY) */
	uint8_t k_anti_entropy;
	/** mp_encode_array(...) */
	uint8_t m_anti_entropy;
	uint16_t v_anti_entropy;
};

/** Initialize SWIM_ANTI_ENTROPY header. */
void
swim_anti_entropy_header_bin_create(struct swim_anti_entropy_header_bin *header,
				    uint16_t batch_size);

/**
 * SWIM member MessagePack template. Represents one record in
 * anti-entropy section.
 */
struct PACKED swim_member_bin {
	/** mp_encode_map(4) */
	uint8_t m_header;

	/** mp_encode_uint(SWIM_MEMBER_STATUS) */
	uint8_t k_status;
	/** mp_encode_uint(enum member_status) */
	uint8_t v_status;

	/** mp_encode_uint(SWIM_MEMBER_ADDRESS) */
	uint8_t k_addr;
	/** mp_encode_uint(addr.sin_addr.s_addr) */
	uint8_t m_addr;
	uint32_t v_addr;

	/** mp_encode_uint(SWIM_MEMBER_PORT) */
	uint8_t k_port;
	/** mp_encode_uint(addr.sin_port) */
	uint8_t m_port;
	uint16_t v_port;

	/** mp_encode_uint(SWIM_MEMBER_UUID) */
	uint8_t k_uuid;
	/** mp_encode_bin(UUID_LEN) */
	uint8_t m_uuid;
	uint8_t m_uuid_len;
	uint8_t v_uuid[UUID_LEN];
};

/** Initialize antri-entropy record. */
void
swim_member_bin_create(struct swim_member_bin *header);

/**
 * Since usually there are many members, it is faster to reset a
 * few fields in an existing template, then each time create a
 * new template. So the usage pattern is create(), fill(),
 * fill() ... .
 */
void
swim_member_bin_fill(struct swim_member_bin *header,
		     const struct sockaddr_in *addr, const struct tt_uuid *uuid,
		     enum swim_member_status status);

/** }}}                  Anti-entropy component                 */

/** {{{                     Meta component                      */

/**
 * Meta component keys, completely handled by the transport level.
 */
enum swim_meta_key {
	/**
	 * Version is now unused, but in future can help in
	 * the protocol improvement, extension.
	 */
	SWIM_META_TARANTOOL_VERSION = 0,
	/**
	 * Source IP/port are stored in body of UDP packet despite
	 * the fact that UDP has them in its header. This is
	 * because
	 *     - packet body is going to be encrypted, but header
	 *       is still open and anybody can catch the packet,
	 *       change source IP/port, and therefore execute
	 *       man-in-the-middle attack;
	 *
	 *     - some network filters can change the address to an
	 *       address of a router or another device.
	 */
	SWIM_META_SRC_ADDRESS,
	SWIM_META_SRC_PORT,
};

/**
 * Each SWIM packet carries meta info, which helps to determine
 * SWIM protocol version, final packet destination and any other
 * internal details, not linked with etalon SWIM protocol.
 *
 * The meta header is mandatory, preceeds main protocol data as a
 * separate MessagePack map.
 */
struct PACKED swim_meta_header_bin {
	/** mp_encode_map(3) */
	uint8_t m_header;

	/** mp_encode_uint(SWIM_META_TARANTOOL_VERSION) */
	uint8_t k_version;
	/** mp_encode_uint(tarantool_version_id()) */
	uint8_t m_version;
	uint32_t v_version;

	/** mp_encode_uint(SWIM_META_SRC_ADDRESS) */
	uint8_t k_addr;
	/** mp_encode_uint(addr.sin_addr.s_addr) */
	uint8_t m_addr;
	uint32_t v_addr;

	/** mp_encode_uint(SWIM_META_SRC_PORT) */
	uint8_t k_port;
	/** mp_encode_uint(addr.sin_port) */
	uint8_t m_port;
	uint16_t v_port;
};

/** Initialize meta section. */
void
swim_meta_header_bin_create(struct swim_meta_header_bin *header,
			    const struct sockaddr_in *src);

/** Meta definition. */
struct swim_meta_def {
	/** Tarantool version. */
	uint32_t version;
	/** Source of the message. */
	struct sockaddr_in src;
};

/**
 * Decode meta section into its definition object.
 * @param[out] def Definition to decode into.
 * @param[in][out] pos MessagePack buffer to decode.
 * @param end End of the MessagePack buffer.
 *
 * @retval 0 Success.
 * @retval -1 Error.
 */
int
swim_meta_def_decode(struct swim_meta_def *def, const char **pos,
		     const char *end);

/** }}}                     Meta component                      */

/**
 * Helpers to decode some values - map, array, etc with
 * appropriate checks. All of them set diagnostics on an error
 * with a specified message prefix and a parameter name.
 */

int
swim_decode_map(const char **pos, const char *end, uint32_t *size,
		const char *prefix, const char *param_name);

int
swim_decode_array(const char **pos, const char *end, uint32_t *size,
		  const char *prefix, const char *param_name);

int
swim_decode_uint(const char **pos, const char *end, uint64_t *value,
		 const char *prefix, const char *param_name);

int
swim_decode_uuid(struct tt_uuid *uuid, const char **pos, const char *end,
		 const char *prefix, const char *param_name);

#endif /* TARANTOOL_SWIM_PROTO_H_INCLUDED */
