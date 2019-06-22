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
#include "tt_static.h"
#include "uuid/tt_uuid.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdbool.h>
#include "swim_constants.h"

enum {
	/** Reserve 272 bytes for headers. */
	MAX_PAYLOAD_SIZE = 1200,
};

/**
 * SWIM binary protocol structures and helpers. Below is a picture
 * of a SWIM message template:
 *
 * +-----------------Public data, not encrypted------------------+
 * |                                                             |
 * |      Initial vector, size depends on chosen algorithm.      |
 * |                   Next data is encrypted.                   |
 * |                                                             |
 * +----------Meta section, handled by transport level-----------+
 * | {                                                           |
 * |     SWIM_META_TARANTOOL_VERSION: uint, Tarantool version ID,|
 * |     SWIM_META_SRC_ADDRESS: uint, ip,                        |
 * |     SWIM_META_SRC_PORT: uint, port,                         |
 * |     SWIM_META_ROUTING: {                                    |
 * |         SWIM_ROUTE_SRC_ADDRESS: uint, ip,                   |
 * |         SWIM_ROUTE_SRC_PORT: uint, port,                    |
 * |         SWIM_ROUTE_DST_ADDRESS: uint, ip,                   |
 * |         SWIM_ROUTE_DST_PORT: uint, port                     |
 * |     }                                                       |
 * | }                                                           |
 * +-------------------Protocol logic section--------------------+
 * | {                                                           |
 * |     SWIM_SRC_UUID: 16 byte UUID,                            |
 * |                                                             |
 * |                 AND                                         |
 * |                                                             |
 * |     SWIM_FAILURE_DETECTION: {                               |
 * |         SWIM_FD_MSG_TYPE: uint, enum swim_fd_msg_type,      |
 * |         SWIM_FD_GENERATION: uint,                           |
 * |         SWIM_FD_VERSION: uint                               |
 * |     },                                                      |
 * |                                                             |
 * |               OR/AND                                        |
 * |                                                             |
 * |     SWIM_DISSEMINATION: [                                   |
 * |         {                                                   |
 * |             SWIM_MEMBER_STATUS: uint, enum member_status,   |
 * |             SWIM_MEMBER_ADDRESS: uint, ip,                  |
 * |             SWIM_MEMBER_PORT: uint, port,                   |
 * |             SWIM_MEMBER_UUID: 16 byte UUID,                 |
 * |             SWIM_MEMBER_GENERATION: uint,                   |
 * |             SWIM_MEMBER_VERSION: uint,                      |
 * |             SWIM_MEMBER_PAYLOAD: bin                        |
 * |         },                                                  |
 * |         ...                                                 |
 * |     ],                                                      |
 * |                                                             |
 * |               OR/AND                                        |
 * |                                                             |
 * |     SWIM_ANTI_ENTROPY: [                                    |
 * |         {                                                   |
 * |             SWIM_MEMBER_STATUS: uint, enum member_status,   |
 * |             SWIM_MEMBER_ADDRESS: uint, ip,                  |
 * |             SWIM_MEMBER_PORT: uint, port,                   |
 * |             SWIM_MEMBER_UUID: 16 byte UUID,                 |
 * |             SWIM_MEMBER_GENERATION: uint,                   |
 * |             SWIM_MEMBER_VERSION: uint,                      |
 * |             SWIM_MEMBER_PAYLOAD: bin                        |
 * |         },                                                  |
 * |         ...                                                 |
 * |     ],                                                      |
 * |                                                             |
 * |               OR/AND                                        |
 * |                                                             |
 * |     SWIM_QUIT: {                                            |
 * |         SWIM_QUIT_GENERATION: uint,                         |
 * |         SWIM_QUIT_VERSION: uint                             |
 * |     }                                                       |
 * | }                                                           |
 * +-------------------------------------------------------------+
 */

enum {
	/**
	 * Number of keys in the incarnation binary structure.
	 * Structures storing an incarnation should use this size
	 * so as to correctly encode MessagePack map header.
	 */
	SWIM_INCARNATION_BIN_SIZE = 2,
};

/**
 * Prepared binary MessagePack representation of an incarnation
 * value. It expects its owner is a map.
 */
struct PACKED swim_incarnation_bin {
	/** mp_encode_uint(generation key) */
	uint8_t k_generation;
	/** mp_encode_uint(64bit generation) */
	uint8_t m_generation;
	uint64_t v_generation;

	/** mp_encode_uint(version key) */
	uint8_t k_version;
	/** mp_encode_uint(64bit version) */
	uint8_t m_version;
	uint64_t v_version;
};

/**
 * SWIM member attributes from anti-entropy and dissemination
 * messages.
 */
struct swim_member_def {
	struct tt_uuid uuid;
	struct sockaddr_in addr;
	struct swim_incarnation incarnation;
	enum swim_member_status status;
	const char *payload;
	int payload_size;
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
	SWIM_FAILURE_DETECTION,
	SWIM_DISSEMINATION,
	SWIM_QUIT,
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

/** {{{                Failure detection component              */

/** Failure detection component keys. */
enum swim_fd_key {
	/** Type of the failure detection message: ping or ack. */
	SWIM_FD_MSG_TYPE,
	/**
	 * Incarnation of the sender. To make the member alive if
	 * it was considered dead, but ping/ack with greater
	 * incarnation was received from it.
	 */
	SWIM_FD_GENERATION,
	SWIM_FD_VERSION,
};

/** Failure detection message type. */
enum swim_fd_msg_type {
	SWIM_FD_MSG_PING,
	SWIM_FD_MSG_ACK,
	swim_fd_msg_type_MAX,
};

extern const char *swim_fd_msg_type_strs[];

/** SWIM failure detection MessagePack header template. */
struct PACKED swim_fd_header_bin {
	/** mp_encode_uint(SWIM_FAILURE_DETECTION) */
	uint8_t k_header;
	/** mp_encode_map(3) */
	uint8_t m_header;

	/** mp_encode_uint(SWIM_FD_MSG_TYPE) */
	uint8_t k_type;
	/** mp_encode_uint(enum swim_fd_msg_type) */
	uint8_t v_type;

	/** SWIM_FD_GENERATION, SWIM_FD_VERSION */
	struct swim_incarnation_bin incarnation;
};

/** Initialize failure detection section. */
void
swim_fd_header_bin_create(struct swim_fd_header_bin *header,
			  enum swim_fd_msg_type type,
			  const struct swim_incarnation *incarnation);

/** A decoded failure detection message. */
struct swim_failure_detection_def {
	/** Type of the message. */
	enum swim_fd_msg_type type;
	/** Incarnation of the sender. */
	struct swim_incarnation incarnation;
};

/**
 * Decode failure detection from a MessagePack buffer.
 * @param[out] def Definition to decode into.
 * @param[in][out] pos Start of the MessagePack buffer.
 * @param end End of the MessagePack buffer.
 * @param prefix A prefix of an error message to use for diag_set,
 *        when something is wrong.
 *
 * @retval 0 Success.
 * @retval -1 Error.
 */
int
swim_failure_detection_def_decode(struct swim_failure_detection_def *def,
				  const char **pos, const char *end,
				  const char *prefix);

/** }}}               Failure detection component               */

/** {{{                  Anti-entropy component                 */

enum {
	/**
	 * Number of keys in the address binary structure.
	 * Structures storing an address should use this size so
	 * as to correctly encode MessagePack map header.
	 */
	SWIM_INADDR_BIN_SIZE = 2,
};

/**
 * Binary inet address structure. It contains two fields at this
 * moment - IP and port encoded as usual numbers. It means that
 * after mp_decode_uint() it is necessary to use htonl/htons()
 * functions to assign the values to struct sockaddr_in.
 */
struct PACKED swim_inaddr_bin {
	/** mp_encode_uint(address key) */
	uint8_t k_addr;
	/** mp_encode_uint(ntohl(addr.sin_addr.s_addr)) */
	uint8_t m_addr;
	uint32_t v_addr;

	/** mp_encode_uint(port key) */
	uint8_t k_port;
	/** mp_encode_uint(ntohs(addr.sin_port)) */
	uint8_t m_port;
	uint16_t v_port;
};

/**
 * Attributes of each record of a broadcasted member table. Just
 * the same as some of struct swim_member attributes.
 */
enum swim_member_key {
	SWIM_MEMBER_STATUS = 0,
	SWIM_MEMBER_ADDRESS,
	SWIM_MEMBER_PORT,
	SWIM_MEMBER_UUID,
	SWIM_MEMBER_GENERATION,
	SWIM_MEMBER_VERSION,
	SWIM_MEMBER_PAYLOAD,
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
 * The structure represents a passport of a member. It consists of
 * some vital necessary member attributes, allowing to detect its
 * state, exact address. The whole passport is necessary for each
 * info related to a member: for anti-entropy records, for
 * dissemination events. The components can inherit that structure
 * and add more attributes. Or just encode new attributes after
 * the passport. For example, anti-entropy can add a payload when
 * it is up to date; dissemination adds a payload when it is up to
 * date and TTD is > 0.
 */
struct PACKED swim_passport_bin {
	/** mp_encode_map(6 or 7) */
	uint8_t m_header;

	/** mp_encode_uint(SWIM_MEMBER_STATUS) */
	uint8_t k_status;
	/** mp_encode_uint(enum member_status) */
	uint8_t v_status;

	/** SWIM_MEMBER_ADDRESS and SWIM_MEMBER_PORT. */
	struct swim_inaddr_bin addr;

	/** mp_encode_uint(SWIM_MEMBER_UUID) */
	uint8_t k_uuid;
	/** mp_encode_bin(UUID_LEN) */
	uint8_t m_uuid;
	uint8_t m_uuid_len;
	uint8_t v_uuid[UUID_LEN];

	/** SWIM_MEMBER_GENERATION, SWIM_MEMBER_VERSION */
	struct swim_incarnation_bin incarnation;
};

/**
 * SWIM member's payload header. Payload data should be encoded
 * right after it.
 */
struct PACKED swim_member_payload_bin {
	/** mp_encode_uint(SWIM_MEMBER_PAYLOAD) */
	uint8_t k_payload;
	/** mp_encode_bin(16bit bin header) */
	uint8_t m_payload_size;
	uint16_t v_payload_size;
	/** Payload data ... */
};

/** Initialize payload record. */
void
swim_member_payload_bin_create(struct swim_member_payload_bin *bin);

/**
 * Fill a previously created payload record with an actual size.
 */
void
swim_member_payload_bin_fill(struct swim_member_payload_bin *bin,
			     uint16_t size);

/** Initialize a member's binary passport. */
void
swim_passport_bin_create(struct swim_passport_bin *passport);

/**
 * Since usually there are many members, it is faster to reset a
 * few fields in an existing template, then each time create a
 * new template. So the usage pattern is create(), fill(),
 * fill() ... .
 */
void
swim_passport_bin_fill(struct swim_passport_bin *passport,
		       const struct sockaddr_in *addr,
		       const struct tt_uuid *uuid,
		       enum swim_member_status status,
		       const struct swim_incarnation *incarnation,
		       bool encode_payload);

/** }}}                  Anti-entropy component                 */

/** {{{                 Dissemination component                 */

/** SWIM dissemination MessagePack template. */
struct PACKED swim_diss_header_bin {
	/** mp_encode_uint(SWIM_DISSEMINATION) */
	uint8_t k_header;
	/** mp_encode_array() */
	uint8_t m_header;
	uint16_t v_header;
};

/** Initialize dissemination header. */
void
swim_diss_header_bin_create(struct swim_diss_header_bin *header,
			    uint16_t batch_size);

/** }}}                 Dissemination component                 */

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
	/**
	 * Routing section allows to specify routes of up to 3
	 * nodes: source, proxy, and destination. It contains two
	 * addresses - the message originator and the final
	 * destination. Here is an example of an indirect message
	 * transmission. Assume, there are 3 nodes: S1, S2, S3.
	 * S1 sends a message to S3 via S2. The following steps
	 * are executed in order to deliver the message:
	 *
	 * S1 -> S2
	 * { src: S1, routing: {src: S1, dst: S3}, body: ... }
	 *
	 * S2 receives the message and sees: routing.dst != S2 -
	 * it is a foreign packet. S2 forwards it to S3 preserving
	 * all the data - body and routing sections.
	 *
	 *
	 * S2 -> S3
	 * {src: S2, routing: {src: S1, dst: S3}, body: ...}
	 *
	 * S3 receives the message and sees: routing.dst == S3 -
	 * the message is delivered. If S3 wants to answer, it
	 * sends a response via the same proxy. It knows, that the
	 * message was delivered from S2, and sends an answer via
	 * S2.
	 */
	SWIM_META_ROUTING,
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
	/** mp_encode_map(3 or 4) */
	uint8_t m_header;

	/** mp_encode_uint(SWIM_META_TARANTOOL_VERSION) */
	uint8_t k_version;
	/** mp_encode_uint(tarantool_version_id()) */
	uint8_t m_version;
	uint32_t v_version;

	/** SWIM_META_SRC_ADDRESS and SWIM_META_SRC_PORT. */
	struct swim_inaddr_bin src_addr;
};

/** Initialize meta section. */
void
swim_meta_header_bin_create(struct swim_meta_header_bin *header,
			    const struct sockaddr_in *src, bool has_routing);

/** Meta definition. */
struct swim_meta_def {
	/** Tarantool version. */
	uint32_t version;
	/** Source of the message. */
	struct sockaddr_in src;
	/** Route source and destination. */
	struct {
		struct sockaddr_in src;
		struct sockaddr_in dst;
	} route;
	/**
	 * True, if both @a src and @a dst are not empty. This
	 * flag is just sugar so as not to check both addresses
	 * manually. Also in future more fields could be added
	 * here.
	 */
	bool is_route_specified;
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

enum swim_route_key {
	/**
	 * True source of the packet. Can be different from the
	 * packet sender. It is expected that a response should be
	 * sent back to this address. Maybe indirectly through the
	 * same proxy.
	 */
	SWIM_ROUTE_SRC_ADDRESS = 0,
	SWIM_ROUTE_SRC_PORT,
	/**
	 * True destination of the packet. Can be different from
	 * this instance, receiver. If it is for another instance,
	 * then this packet is forwarded to the latter.
	 */
	SWIM_ROUTE_DST_ADDRESS,
	SWIM_ROUTE_DST_PORT,
	swim_route_key_MAX,
};

/** Route section template. Describes source, destination. */
struct PACKED swim_route_bin {
	/** mp_encode_uint(SWIM_ROUTING) */
	uint8_t k_routing;
	/** mp_encode_map(4) */
	uint8_t m_routing;
	/** SWIM_ROUTE_SRC_ADDRESS and SWIM_ROUTE_SRC_PORT. */
	struct swim_inaddr_bin src_addr;
	/** SWIM_ROUTE_DST_ADDRESS and SWIM_ROUTE_DST_PORT. */
	struct swim_inaddr_bin dst_addr;
};

/** Initialize routing section. */
void
swim_route_bin_create(struct swim_route_bin *route,
		      const struct sockaddr_in *src,
		      const struct sockaddr_in *dst);

/** }}}                     Meta component                      */

enum swim_quit_key {
	/** Incarnation to ignore old quit messages. */
	SWIM_QUIT_GENERATION = 0,
	SWIM_QUIT_VERSION,
};

/** Quit section. Describes voluntary quit from the cluster. */
struct PACKED swim_quit_bin {
	/** mp_encode_uint(SWIM_QUIT) */
	uint8_t k_quit;
	/** mp_encode_map(2) */
	uint8_t m_quit;

	/** SWIM_QUIT_GENERATION, SWIM_QUIT_VERSION */
	struct swim_incarnation_bin incarnation;
};

/** Initialize quit section. */
void
swim_quit_bin_create(struct swim_quit_bin *header,
		     const struct swim_incarnation *incarnation);

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

/**
 * Check if @a addr is not empty, i.e. not nullified. Empty
 * addresses are considered invalid and normally can not appear in
 * packets just like any other errors can't. But since the SWIM
 * protocol is public, there can be outlandish drivers and they
 * can contain errors. Check for nullified address is a protection
 * from malicious and invalid packets.
 */
static inline bool
swim_inaddr_is_empty(const struct sockaddr_in *addr)
{
	return addr->sin_port == 0 || addr->sin_addr.s_addr == 0;
}

/** Check if two AF_INET addresses are equal. */
static inline bool
swim_inaddr_eq(const struct sockaddr_in *a1, const struct sockaddr_in *a2)
{
	return a1->sin_port == a2->sin_port &&
	       a1->sin_addr.s_addr == a2->sin_addr.s_addr;
}

#endif /* TARANTOOL_SWIM_PROTO_H_INCLUDED */
