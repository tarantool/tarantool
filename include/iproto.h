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

#define ERROR_CODES(_)					    \
	_(ERR_CODE_OK,                    0x00000000, "ok") \
	_(ERR_CODE_NONMASTER,             0x00000102, "non master connection, but it should be") \
	_(ERR_CODE_ILLEGAL_PARAMS,        0x00000202, "illegal parametrs") \
	_(ERR_CODE_BAD_UID,               0x00000302, "uid not from this storage range") \
	_(ERR_CODE_NODE_IS_RO,            0x00000401, "node is marked as read-only") \
	_(ERR_CODE_NODE_IS_NOT_LOCKED,    0x00000501, "node isn't locked") \
	_(ERR_CODE_NODE_IS_LOCKED,        0x00000601, "node is locked") \
	_(ERR_CODE_MEMORY_ISSUE,          0x00000701, "some memory issues") \
	_(ERR_CODE_BAD_INTEGRITY,         0x00000802, "bad graph integrity") \
	_(ERR_CODE_UNSUPPORTED_COMMAND,   0x00000a02, "unsupported command") \
	/* gap due to silverproxy */					\
	_(ERR_CODE_CANNOT_REGISTER,       0x00001801, "can't register new user") \
	_(ERR_CODE_CANNOT_INIT_ALERT_ID,  0x00001a01, "can't generate alert id") \
	_(ERR_CODE_CANNOT_DEL,            0x00001b02, "can't del node") \
	_(ERR_CODE_USER_NOT_REGISTERED,   0x00001c02, "user isn't registered") \
	/* silversearch error codes */					\
	_(ERR_CODE_SYNTAX_ERROR,          0x00001d02, "syntax error in query") \
	_(ERR_CODE_WRONG_FIELD,           0x00001e02, "unknown field") \
	_(ERR_CODE_WRONG_NUMBER,          0x00001f02, "number value is out of range") \
	_(ERR_CODE_DUPLICATE,             0x00002002, "insert already existing object") \
	_(ERR_CODE_UNSUPPORTED_ORDER,     0x00002202, "can not order result") \
	_(ERR_CODE_MULTIWRITE,            0x00002302, "multiple to update/delete") \
	_(ERR_CODE_NOTHING,               0x00002400, "nothing to do (not an error)") \
	_(ERR_CODE_UPDATE_ID,             0x00002502, "id's update") \
	_(ERR_CODE_WRONG_VERSION,         0x00002602, "unsupported version of protocol") \
	/* other generic error codes */					\
	_(ERR_CODE_UNKNOWN_ERROR,         0x00002702, "unknown error") \
        _(ERR_CODE_NODE_NOT_FOUND,	  0x00003102, "node isn't found") \
	_(ERR_CODE_NODE_FOUND,		  0x00003702, "node is found") \
	_(ERR_CODE_INDEX_VIOLATION,	  0x00003802, "some index violation occur") \
	_(ERR_CODE_NO_SUCH_NAMESPACE,	  0x00003902, "there is no such namespace")

ENUM(error_codes, ERROR_CODES);
extern char *error_codes_strs[];
extern char *error_codes_desc_strs[];
#endif
