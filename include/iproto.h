#ifndef TARANTOOL_IPROTO_H
#define TARANTOOL_IPROTO_H

#include <stdint.h>

#include <tbuf.h>
#include <util.h>

/*
 * struct iproto_header and struct iproto_header_retcode
 * share common prefix {msg_code, len, sync}
 */

struct iproto_header {
	uint32_t msg_code;
	uint32_t len;
	uint32_t sync;
	uint8_t data[];
} __packed__;

struct iproto_header_retcode {
	uint32_t msg_code;
	uint32_t len;
	uint32_t sync;
	uint32_t ret_code;
	uint8_t data[];
} __packed__;

static inline struct iproto_header *iproto(const struct tbuf *t)
{
	return (struct iproto_header *)t->data;
}

struct iproto_interactor;

struct iproto_interactor
*iproto_interactor(uint32_t (*interact) (uint32_t msg, uint8_t *data, size_t len));

void iproto_interact(void *);

#define ERROR_CODES(_)							\
	_(ERR_CODE_OK,                    0x00000000) /* OK */		\
	_(ERR_CODE_NONMASTER,             0x00000102) /* Non master connection, but it should be */ \
	_(ERR_CODE_ILLEGAL_PARAMS,        0x00000202) /* Illegal parametrs */ \
	_(ERR_CODE_BAD_UID,               0x00000302) /* Uid not from this storage range */ \
	_(ERR_CODE_NODE_IS_RO,            0x00000401) /* Node is marked as read-only */ \
	_(ERR_CODE_NODE_IS_NOT_LOCKED,    0x00000501) /* Node isn't locked */ \
	_(ERR_CODE_NODE_IS_LOCKED,        0x00000601) /* Node is locked */ \
	_(ERR_CODE_MEMORY_ISSUE,          0x00000701) /* Some memory issues */ \
	_(ERR_CODE_BAD_INTEGRITY,         0x00000802) /* Bad graph integrity */ \
	_(ERR_CODE_UNSUPPORTED_COMMAND,   0x00000a02) /* Unsupported command */ \
	/* gap due to silverproxy */					\
	_(ERR_CODE_CANNOT_REGISTER,       0x00001801) /* Can't register new user */ \
	_(ERR_CODE_CANNOT_INIT_ALERT_ID,  0x00001a01) /* Can't generate alert id */ \
	_(ERR_CODE_CANNOT_DEL,            0x00001b02) /* Can't del node */ \
	_(ERR_CODE_USER_NOT_REGISTERED,   0x00001c02) /* User isn't registered */ \
	/* silversearch error codes */					\
	_(ERR_CODE_SYNTAX_ERROR,          0x00001d02) /* Syntax error in query */ \
	_(ERR_CODE_WRONG_FIELD,           0x00001e02) /* Unknown field */ \
	_(ERR_CODE_WRONG_NUMBER,          0x00001f02) /* Number value is out of range */ \
	_(ERR_CODE_DUPLICATE,             0x00002002) /* Insert already existing object */ \
	_(ERR_CODE_UNSUPPORTED_ORDER,     0x00002202) /* Can not order result */ \
	_(ERR_CODE_MULTIWRITE,            0x00002302) /* Multiple to update/delete */ \
	_(ERR_CODE_NOTHING,               0x00002400) /* nothing to do (not an error) */ \
	_(ERR_CODE_UPDATE_ID,             0x00002502) /* id's update */ \
	_(ERR_CODE_WRONG_VERSION,         0x00002602)	/* unsupported version of protocol */ \
	_(ERR_CODE_UNKNOWN_ERROR,         0x00002702)

ENUM(error_codes, ERROR_CODES);
extern char *error_codes_strs[];
#endif
