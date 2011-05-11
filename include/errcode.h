#ifndef TARANTOOL_ERRCODE_H
#define TARANTOOL_ERRCODE_H

#include <stdint.h>

#include <util.h>

struct errcode_record {
	const char *errstr;
	uint32_t errval;
	const char *errdesc;
};

#define ERRCODE_RECORD_MEMBER(s, v, d) {	\
	.errstr = #s,				\
	.errval = v,				\
	.errdesc = #d				\
},

#define ERRCODE_RECORDS(enum_name, enum_members)			\
	struct errcode_record enum_name##_records[enum_name##_MAX] = {	\
		enum_members(ERRCODE_RECORD_MEMBER)			\
	}

#define ERRCODE_STR(enum_name, err) (enum_name##_records[err].errstr)
#define ERRCODE_VAL(enum_name, err) (enum_name##_records[err].errval)
#define ERRCODE_DESC(enum_name, err) (enum_name##_records[err].errdesc)

#define ERROR_CODES(_)					    \
	_(ERR_CODE_OK,                    0x00000000, "OK") \
	_(ERR_CODE_NONMASTER,             0x00000102, "Non master connection, but it should be") \
	_(ERR_CODE_ILLEGAL_PARAMS,        0x00000202, "Illegal parametrs") \
	_(ERR_CODE_BAD_UID,               0x00000302, "Uid is not from this storage range") \
	_(ERR_CODE_NODE_IS_RO,            0x00000401, "Node is marked as read-only") \
	_(ERR_CODE_NODE_IS_NOT_LOCKED,    0x00000501, "Node isn't locked") \
	_(ERR_CODE_NODE_IS_LOCKED,        0x00000601, "Node is locked") \
	_(ERR_CODE_MEMORY_ISSUE,          0x00000701, "Some memory issue") \
	_(ERR_CODE_BAD_INTEGRITY,         0x00000802, "Bad graph integrity") \
	_(ERR_CODE_UNSUPPORTED_COMMAND,   0x00000a02, "Unsupported command") \
	/* gap due to silverproxy */					\
	_(ERR_CODE_CANNOT_REGISTER,       0x00001801, "Can't register new user") \
	_(ERR_CODE_CANNOT_INIT_ALERT_ID,  0x00001a01, "Can't generate alert id") \
	_(ERR_CODE_CANNOT_DEL,            0x00001b02, "Can't del node") \
	_(ERR_CODE_USER_NOT_REGISTERED,   0x00001c02, "User isn't registered") \
	/* silversearch error codes */					\
	_(ERR_CODE_SYNTAX_ERROR,          0x00001d02, "Syntax error in query") \
	_(ERR_CODE_WRONG_FIELD,           0x00001e02, "Unknown field") \
	_(ERR_CODE_WRONG_NUMBER,          0x00001f02, "Number value is out of range") \
	_(ERR_CODE_DUPLICATE,             0x00002002, "Insert already existing object") \
	_(ERR_CODE_UNSUPPORTED_ORDER,     0x00002202, "Can not order result") \
	_(ERR_CODE_MULTIWRITE,            0x00002302, "Multiple to update/delete") \
	_(ERR_CODE_NOTHING,               0x00002400, "Nothing to do (not an error)") \
	_(ERR_CODE_UPDATE_ID,             0x00002502, "Id's update") \
	_(ERR_CODE_WRONG_VERSION,         0x00002602, "Unsupported version of protocol") \
	/* other generic error codes */					\
	_(ERR_CODE_UNKNOWN_ERROR,         0x00002702, "Unknown error") \
        _(ERR_CODE_NODE_NOT_FOUND,	  0x00003102, "Node isn't found") \
	_(ERR_CODE_NODE_FOUND,		  0x00003702, "Node is found") \
	_(ERR_CODE_INDEX_VIOLATION,	  0x00003802, "Some index violation occur") \
	_(ERR_CODE_NO_SUCH_NAMESPACE,	  0x00003902, "There is no such namespace")

ENUM0(error_codes, ERROR_CODES);
extern struct errcode_record error_codes_records[];

#endif
