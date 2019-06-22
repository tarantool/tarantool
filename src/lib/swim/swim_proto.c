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
#include "swim_proto.h"
#include "msgpuck.h"
#include "say.h"
#include "version.h"
#include "diag.h"
#include <sys/socket.h> /* AF_INET for FreeBSD. */

const char *swim_member_status_strs[] = {
	"alive",
	"suspected",
	"dead",
	"left",
};

const char *swim_fd_msg_type_strs[] = {
	"ping",
	"ack",
};

int
swim_decode_map(const char **pos, const char *end, uint32_t *size,
		const char *prefix, const char *param_name)
{
	if (mp_typeof(**pos) != MP_MAP || *pos == end ||
	    mp_check_map(*pos, end) > 0) {
		diag_set(SwimError, "%s %s should be a map", prefix,
			 param_name);
		return -1;
	}
	*size = mp_decode_map(pos);
	return 0;
}

int
swim_decode_array(const char **pos, const char *end, uint32_t *size,
		  const char *prefix, const char *param_name)
{
	if (mp_typeof(**pos) != MP_ARRAY || *pos == end ||
	    mp_check_array(*pos, end) > 0) {
		diag_set(SwimError, "%s %s should be an array", prefix,
			 param_name);
		return -1;
	}
	*size = mp_decode_array(pos);
	return 0;
}

int
swim_decode_uint(const char **pos, const char *end, uint64_t *value,
		 const char *prefix, const char *param_name)
{
	if (mp_typeof(**pos) != MP_UINT || *pos == end ||
	    mp_check_uint(*pos, end) > 0) {
		diag_set(SwimError, "%s %s should be a uint", prefix,
			 param_name);
		return -1;
	}
	*value = mp_decode_uint(pos);
	return 0;
}

static inline int
swim_decode_ip(struct sockaddr_in *address, const char **pos, const char *end,
	       const char *prefix, const char *param_name)
{
	uint64_t ip;
	if (swim_decode_uint(pos, end, &ip, prefix, param_name) != 0)
		return -1;
	if (ip > UINT32_MAX) {
		diag_set(SwimError, "%s %s is an invalid IP address", prefix,
			 param_name);
		return -1;
	}
	address->sin_addr.s_addr = htonl(ip);
	return 0;
}

static inline int
swim_decode_port(struct sockaddr_in *address, const char **pos, const char *end,
		 const char *prefix, const char *param_name)
{
	uint64_t port;
	if (swim_decode_uint(pos, end, &port, prefix, param_name) != 0)
		return -1;
	if (port > UINT16_MAX) {
		diag_set(SwimError, "%s %s is an invalid port", prefix,
			 param_name);
		return -1;
	}
	address->sin_port = htons(port);
	return 0;
}

static inline int
swim_decode_bin(const char **bin, uint32_t *size, const char **pos,
		const char *end, const char *prefix, const char *param_name)
{
	if (mp_typeof(**pos) != MP_BIN || *pos == end ||
	    mp_check_binl(*pos, end) > 0) {
		diag_set(SwimError, "%s %s should be bin", prefix,
			 param_name);
		return -1;
	}
	*bin = mp_decode_bin(pos, size);
	if (*pos > end) {
		diag_set(SwimError, "%s %s is invalid", prefix, param_name);
		return -1;
	}
	return 0;
}

int
swim_decode_uuid(struct tt_uuid *uuid, const char **pos, const char *end,
		 const char *prefix, const char *param_name)
{
	uint32_t size;
	const char *bin;
	if (swim_decode_bin(&bin, &size, pos, end, prefix, param_name) != 0)
		return -1;
	if (size != UUID_LEN) {
		diag_set(SwimError, "%s %s is invalid", prefix, param_name);
		return -1;
	}
	memcpy(uuid, bin, UUID_LEN);
	return 0;
}

/**
 * Create incarnation binary MessagePack structure. It expects
 * parent structure specific keys for incarnation parts.
 */
static inline void
swim_incarnation_bin_create(struct swim_incarnation_bin *bin,
			    uint8_t generation_key, uint8_t version_key)
{
	bin->k_generation = generation_key;
	bin->m_generation = 0xcf;
	bin->k_version = version_key;
	bin->m_version = 0xcf;
}

/**
 * Fill a created incarnation binary structure with an incarnation
 * value.
 */
static inline void
swim_incarnation_bin_fill(struct swim_incarnation_bin *bin,
			  const struct swim_incarnation *incarnation)
{
	bin->v_generation = mp_bswap_u64(incarnation->generation);
	bin->v_version = mp_bswap_u64(incarnation->version);
}

/**
 * Check if @a addr is not empty, i.e. not nullified. Set an error
 * in the diagnostics area in case of emptiness.
 */
static inline int
swim_check_inaddr_not_empty(const struct sockaddr_in *addr, const char *prefix,
			    const char *addr_name)
{
	if (! swim_inaddr_is_empty(addr))
		return 0;
	diag_set(SwimError, "%s %s address is mandatory", prefix, addr_name);
	return -1;
}

/**
 * Create a binary address structure. It requires explicit IP and
 * port keys specification since the keys depend on the component
 * employing the address.
 */
static inline void
swim_inaddr_bin_create(struct swim_inaddr_bin *bin, uint8_t ip_key,
		       uint8_t port_key)
{
	assert(mp_sizeof_uint(ip_key) == 1 && mp_sizeof_uint(port_key) == 1);
	bin->k_addr = ip_key;
	bin->m_addr = 0xce;
	bin->k_port = port_key;
	bin->m_port = 0xcd;
}

/** Fill already created @a bin with a real inet address. */
static inline void
swim_inaddr_bin_fill(struct swim_inaddr_bin *bin,
		     const struct sockaddr_in *addr)
{
	bin->v_addr = mp_bswap_u32(ntohl(addr->sin_addr.s_addr));
	bin->v_port = mp_bswap_u16(ntohs(addr->sin_port));
}

void
swim_member_def_create(struct swim_member_def *def)
{
	memset(def, 0, sizeof(*def));
	def->addr.sin_family = AF_INET;
	def->status = MEMBER_ALIVE;
	def->payload_size = -1;
}

/**
 * Decode a MessagePack value of @a key and store it in @a def.
 * @param key Key to read value of.
 * @param[in][out] pos Where a value is stored.
 * @param end End of the buffer.
 * @param prefix Error message prefix.
 * @param[out] def Where to store the value.
 *
 * @retval 0 Success.
 * @retval -1 Error.
 */
static int
swim_decode_member_key(enum swim_member_key key, const char **pos,
		       const char *end, const char *prefix,
		       struct swim_member_def *def)
{
	uint64_t tmp;
	uint32_t len;
	switch (key) {
	case SWIM_MEMBER_STATUS:
		if (swim_decode_uint(pos, end, &tmp, prefix,
				     "member status") != 0)
			return -1;
		if (tmp >= swim_member_status_MAX) {
			diag_set(SwimError, "%s unknown member status", prefix);
			return -1;
		}
		def->status = (enum swim_member_status) tmp;
		break;
	case SWIM_MEMBER_ADDRESS:
		if (swim_decode_ip(&def->addr, pos, end, prefix,
				   "member address") != 0)
			return -1;
		break;
	case SWIM_MEMBER_PORT:
		if (swim_decode_port(&def->addr, pos, end, prefix,
				     "member port") != 0)
			return -1;
		break;
	case SWIM_MEMBER_UUID:
		if (swim_decode_uuid(&def->uuid, pos, end, prefix,
				     "member uuid") != 0)
			return -1;
		break;
	case SWIM_MEMBER_GENERATION:
		if (swim_decode_uint(pos, end, &def->incarnation.generation,
				     prefix, "member generation") != 0)
			return -1;
		break;
	case SWIM_MEMBER_VERSION:
		if (swim_decode_uint(pos, end, &def->incarnation.version,
				     prefix, "member version") != 0)
			return -1;
		break;
	case SWIM_MEMBER_PAYLOAD:
		if (swim_decode_bin(&def->payload, &len, pos, end, prefix,
				    "member payload") != 0)
			return -1;
		if (len > MAX_PAYLOAD_SIZE) {
			diag_set(SwimError, "%s member payload size should be "\
				 "<= %d", prefix, MAX_PAYLOAD_SIZE);
			return -1;
		}
		def->payload_size = (int) len;
		break;
	default:
		unreachable();
	}
	return 0;
}

int
swim_member_def_decode(struct swim_member_def *def, const char **pos,
		       const char *end, const char *prefix)
{
	uint32_t size;
	if (swim_decode_map(pos, end, &size, prefix, "member") != 0)
		return -1;
	swim_member_def_create(def);
	for (uint32_t j = 0; j < size; ++j) {
		uint64_t key;
		if (swim_decode_uint(pos, end, &key, prefix,
				     "member key") != 0)
			return -1;
		if (key >= swim_member_key_MAX) {
			diag_set(SwimError, "%s unknown member key", prefix);
			return -1;
		}
		if (swim_decode_member_key(key, pos, end, prefix, def) != 0)
			return -1;
	}
	if (tt_uuid_is_nil(&def->uuid)) {
		diag_set(SwimError, "%s member uuid is mandatory", prefix);
		return -1;
	}
	return swim_check_inaddr_not_empty(&def->addr, prefix, "member");
}

void
swim_src_uuid_bin_create(struct swim_src_uuid_bin *header,
			 const struct tt_uuid *uuid)
{
	header->k_uuid = SWIM_SRC_UUID;
	header->m_uuid = 0xc4;
	header->m_uuid_len = UUID_LEN;
	memcpy(header->v_uuid, uuid, UUID_LEN);
}

void
swim_fd_header_bin_create(struct swim_fd_header_bin *header,
			  enum swim_fd_msg_type type,
			  const struct swim_incarnation *incarnation)
{
	header->k_header = SWIM_FAILURE_DETECTION;
	int map_size = 1 + SWIM_INCARNATION_BIN_SIZE;
	assert(mp_sizeof_map(map_size) == 1);
	header->m_header = 0x80 | map_size;

	header->k_type = SWIM_FD_MSG_TYPE;
	header->v_type = type;

	swim_incarnation_bin_create(&header->incarnation, SWIM_FD_GENERATION,
				    SWIM_FD_VERSION);
	swim_incarnation_bin_fill(&header->incarnation, incarnation);
}

int
swim_failure_detection_def_decode(struct swim_failure_detection_def *def,
				  const char **pos, const char *end,
				  const char *prefix)
{
	uint32_t size;
	if (swim_decode_map(pos, end, &size, prefix, "root") != 0)
		return -1;
	memset(def, 0, sizeof(*def));
	def->type = swim_fd_msg_type_MAX;
	if (size != 1 + SWIM_INCARNATION_BIN_SIZE) {
		diag_set(SwimError, "%s root map should have %d keys - "\
			 "message type and version", prefix,
			 1 + SWIM_INCARNATION_BIN_SIZE);
		return -1;
	}
	for (int i = 0; i < (int) size; ++i) {
		uint64_t key;
		if (swim_decode_uint(pos, end, &key, prefix, "a key") != 0)
			return -1;
		switch(key) {
		case SWIM_FD_MSG_TYPE:
			if (swim_decode_uint(pos, end, &key, prefix,
					     "message type") != 0)
				return -1;
			if (key >= swim_fd_msg_type_MAX) {
				diag_set(SwimError, "%s unknown message type",
					 prefix);
				return -1;
			}
			def->type = key;
			break;
		case SWIM_FD_GENERATION:
			if (swim_decode_uint(pos, end,
					     &def->incarnation.generation,
					     prefix, "generation") != 0)
				return -1;
			break;
		case SWIM_FD_VERSION:
			if (swim_decode_uint(pos, end,
					     &def->incarnation.version, prefix,
					     "version") != 0)
				return -1;
			break;
		default:
			diag_set(SwimError, "%s unexpected key", prefix);
			return -1;
		}
	}
	if (def->type == swim_fd_msg_type_MAX) {
		diag_set(SwimError, "%s message type should be specified",
			 prefix);
		return -1;
	}
	return 0;
}

void
swim_anti_entropy_header_bin_create(struct swim_anti_entropy_header_bin *header,
				    uint16_t batch_size)
{
	header->k_anti_entropy = SWIM_ANTI_ENTROPY;
	header->m_anti_entropy = 0xdc;
	header->v_anti_entropy = mp_bswap_u16(batch_size);
}

void
swim_member_payload_bin_create(struct swim_member_payload_bin *bin)
{
	bin->k_payload = SWIM_MEMBER_PAYLOAD;
	bin->m_payload_size = 0xc5;
}

void
swim_member_payload_bin_fill(struct swim_member_payload_bin *bin, uint16_t size)
{
	bin->v_payload_size = mp_bswap_u16(size);
}

void
swim_passport_bin_create(struct swim_passport_bin *passport)
{
	passport->k_status = SWIM_MEMBER_STATUS;
	swim_inaddr_bin_create(&passport->addr, SWIM_MEMBER_ADDRESS,
			       SWIM_MEMBER_PORT);
	passport->k_uuid = SWIM_MEMBER_UUID;
	passport->m_uuid = 0xc4;
	passport->m_uuid_len = UUID_LEN;
	swim_incarnation_bin_create(&passport->incarnation,
				    SWIM_MEMBER_GENERATION,
				    SWIM_MEMBER_VERSION);
}

void
swim_passport_bin_fill(struct swim_passport_bin *passport,
		       const struct sockaddr_in *addr,
		       const struct tt_uuid *uuid,
		       enum swim_member_status status,
		       const struct swim_incarnation *incarnation,
		       bool encode_payload)
{
	int map_size = 2 + SWIM_INCARNATION_BIN_SIZE + SWIM_INADDR_BIN_SIZE +
		       encode_payload;
	assert(mp_sizeof_map(map_size) == 1);
	passport->m_header = 0x80 | map_size;
	passport->v_status = status;
	swim_inaddr_bin_fill(&passport->addr, addr);
	memcpy(passport->v_uuid, uuid, UUID_LEN);
	swim_incarnation_bin_fill(&passport->incarnation, incarnation);
}

void
swim_diss_header_bin_create(struct swim_diss_header_bin *header,
			    uint16_t batch_size)
{
	header->k_header = SWIM_DISSEMINATION;
	header->m_header = 0xdc;
	header->v_header = mp_bswap_u16(batch_size);
}

void
swim_meta_header_bin_create(struct swim_meta_header_bin *header,
			    const struct sockaddr_in *src, bool has_routing)
{
	int map_size = 1 + SWIM_INADDR_BIN_SIZE + has_routing;
	assert(mp_sizeof_map(map_size) == 1);
	header->m_header = 0x80 | map_size;
	header->k_version = SWIM_META_TARANTOOL_VERSION;
	header->m_version = 0xce;
	header->v_version = mp_bswap_u32(tarantool_version_id());
	swim_inaddr_bin_create(&header->src_addr, SWIM_META_SRC_ADDRESS,
			       SWIM_META_SRC_PORT);
	swim_inaddr_bin_fill(&header->src_addr, src);
}

/**
 * Decode meta routing section into meta definition object.
 * @param[out] def Definition to decode into.
 * @param[in][out] pos MessagePack buffer to decode.
 * @param end End of the MessagePack buffer.
 *
 * @retval 0 Success.
 * @retval -1 Error.
 */
static int
swim_meta_def_decode_route(struct swim_meta_def *def, const char **pos,
			   const char *end)
{
	const char *prefix = "invalid routing section:";
	uint32_t size;
	def->route.src.sin_family = AF_INET;
	def->route.dst.sin_family = AF_INET;
	if (swim_decode_map(pos, end, &size, prefix, "route") != 0)
		return -1;
	for (uint32_t i = 0; i < size; ++i) {
		uint64_t key;
		if (swim_decode_uint(pos, end, &key, prefix, "a key") != 0)
			return -1;
		switch (key) {
		case SWIM_ROUTE_SRC_ADDRESS:
			if (swim_decode_ip(&def->route.src, pos, end, prefix,
					   "source address") != 0)
				return -1;
			break;
		case SWIM_ROUTE_SRC_PORT:
			if (swim_decode_port(&def->route.src, pos, end,
					     prefix, "source port") != 0)
				return -1;
			break;
		case SWIM_ROUTE_DST_ADDRESS:
			if (swim_decode_ip(&def->route.dst, pos, end, prefix,
					   "destination address") != 0)
				return -1;
			break;
		case SWIM_ROUTE_DST_PORT:
			if (swim_decode_port(&def->route.dst, pos, end,
					     prefix, "destination port") != 0)
				return -1;
			break;
		default:
			diag_set(SwimError, "%s unknown key", prefix);
			return -1;
		}
	}
	if (swim_check_inaddr_not_empty(&def->route.src, prefix,
					"source") != 0 ||
	    swim_check_inaddr_not_empty(&def->route.dst, prefix,
					"destination") != 0)
		return -1;
	def->is_route_specified = true;
	return 0;
}

int
swim_meta_def_decode(struct swim_meta_def *def, const char **pos,
		     const char *end)
{
	const char *prefix = "invalid meta section:";
	uint32_t size;
	if (swim_decode_map(pos, end, &size, prefix, "root") != 0)
		return -1;
	memset(def, 0, sizeof(*def));
	def->src.sin_family = AF_INET;
	for (uint32_t i = 0; i < size; ++i) {
		uint64_t key;
		if (swim_decode_uint(pos, end, &key, prefix, "a key") != 0)
			return -1;
		switch (key) {
		case SWIM_META_ROUTING:
			if (swim_meta_def_decode_route(def, pos, end) != 0)
				return -1;
			break;
		case SWIM_META_TARANTOOL_VERSION:
			if (swim_decode_uint(pos, end, &key, prefix,
					     "version") != 0)
				return -1;
			if (key > UINT32_MAX) {
				diag_set(SwimError, "%s invalid version, too "\
					 "big", prefix);
				return -1;
			}
			def->version = key;
			break;
		case SWIM_META_SRC_ADDRESS:
			if (swim_decode_ip(&def->src, pos, end, prefix,
					   "source address") != 0)
				return -1;
			break;
		case SWIM_META_SRC_PORT:
			if (swim_decode_port(&def->src, pos, end, prefix,
					     "source port") != 0)
				return -1;
			break;
		default:
			diag_set(SwimError, "%s unknown key", prefix);
			return -1;
		}
	}
	if (def->version == 0) {
		diag_set(SwimError, "%s version is mandatory", prefix);
		return -1;
	}
	return swim_check_inaddr_not_empty(&def->src, prefix, "source");
}

void
swim_quit_bin_create(struct swim_quit_bin *header,
		     const struct swim_incarnation *incarnation)
{
	header->k_quit = SWIM_QUIT;
	assert(mp_sizeof_map(SWIM_INCARNATION_BIN_SIZE) == 1);
	header->m_quit = 0x80 | SWIM_INCARNATION_BIN_SIZE;
	swim_incarnation_bin_create(&header->incarnation, SWIM_QUIT_GENERATION,
				    SWIM_QUIT_VERSION);
	swim_incarnation_bin_fill(&header->incarnation, incarnation);
}

void
swim_route_bin_create(struct swim_route_bin *route,
		      const struct sockaddr_in *src,
		      const struct sockaddr_in *dst)
{
	int map_size = SWIM_INADDR_BIN_SIZE * 2;
	assert(mp_sizeof_map(map_size) == 1);
	route->k_routing = SWIM_META_ROUTING;
	route->m_routing = 0x80 | map_size;
	swim_inaddr_bin_create(&route->src_addr, SWIM_ROUTE_SRC_ADDRESS,
			       SWIM_ROUTE_SRC_PORT);
	swim_inaddr_bin_create(&route->dst_addr, SWIM_ROUTE_DST_ADDRESS,
			       SWIM_ROUTE_DST_PORT);
	swim_inaddr_bin_fill(&route->src_addr, src);
	swim_inaddr_bin_fill(&route->dst_addr, dst);
}
