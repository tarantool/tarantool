#ifndef TARANTOOL_ERRCODE_H
#define TARANTOOL_ERRCODE_H

#include <stdint.h>

#include <util.h>

struct errcode_record {
	const char *errstr;
	uint32_t errflags;
	const char *errdesc;
};

#define ERRCODE_RECORD_MEMBER(s, f, d) {	\
	.errstr = #s,				\
	.errflags = f,				\
	.errdesc = #d				\
},

#define ERRCODE_RECORDS(enum_name, enum_members)			\
	struct errcode_record enum_name##_records[enum_name##_MAX] = {	\
		enum_members(ERRCODE_RECORD_MEMBER)			\
	}

#define ERRCODE_STR(enum_name, err) (enum_name##_records[err].errstr)
#define ERRCODE_VAL(enum_name, err) (((err) << 8) | enum_name##_records[err].errflags)
#define ERRCODE_DESC(enum_name, err) (enum_name##_records[err].errdesc)

#define ERROR_CODES(_)					    \
	/*  0 */_(ERR_CODE_OK,				0, "OK") \
	/*  1 */_(ERR_CODE_NONMASTER,			2, "Non master connection, but it should be") \
	/*  2 */_(ERR_CODE_ILLEGAL_PARAMS,		2, "Illegal parametrs") \
	/*  3 */_(ERR_CODE_BAD_UID,			2, "Uid is not from this storage range") \
	/*  4 */_(ERR_CODE_NODE_IS_RO,			1, "Node is marked as read-only") \
	/*  5 */_(ERR_CODE_NODE_IS_NOT_LOCKED,		1, "Node isn't locked") \
	/*  6 */_(ERR_CODE_NODE_IS_LOCKED,		1, "Node is locked") \
	/*  7 */_(ERR_CODE_MEMORY_ISSUE,		1, "Some memory issue") \
	/*  8 */_(ERR_CODE_BAD_INTEGRITY,		2, "Bad graph integrity") \
	/*  9 */_(ERR_CODE_UNUSED9,			0, "Unused9") \
	/* 10 */_(ERR_CODE_UNSUPPORTED_COMMAND,		2, "Unsupported command") \
		/* silverproxy error codes */ \
	/* 11 */_(ERR_CODE_RESERVED11,			0, "Reserved11") \
	/* 12 */_(ERR_CODE_RESERVED12,			0, "Reserved12") \
	/* 13 */_(ERR_CODE_RESERVED13,			0, "Reserved13") \
	/* 14 */_(ERR_CODE_RESERVED14,			0, "Reserved14") \
	/* 15 */_(ERR_CODE_RESERVED15,			0, "Reserved15") \
	/* 16 */_(ERR_CODE_RESERVED16,			0, "Reserved16") \
	/* 17 */_(ERR_CODE_RESERVED17,			0, "Reserved17") \
	/* 18 */_(ERR_CODE_RESERVED18,			0, "Reserved18") \
	/* 19 */_(ERR_CODE_RESERVED19,			0, "Reserved19") \
	/* 20 */_(ERR_CODE_RESERVED20,			0, "Reserved20") \
	/* 21 */_(ERR_CODE_RESERVED21,			0, "Reserved21") \
	/* 22 */_(ERR_CODE_RESERVED22,			0, "Reserved22") \
	/* 23 */_(ERR_CODE_RESERVED23,			0, "Reserved23") \
		/* end of silverproxy error codes */ \
	/* 24 */_(ERR_CODE_CANNOT_REGISTER,		1, "Can't register new user") \
	/* 25 */_(ERR_CODE_UNUSED25,			0, "Unused25") \
	/* 26 */_(ERR_CODE_CANNOT_INIT_ALERT_ID,	1, "Can't generate alert id") \
	/* 27 */_(ERR_CODE_CANNOT_DEL,			2, "Can't del node") \
	/* 28 */_(ERR_CODE_USER_NOT_REGISTERED,		2, "User isn't registered") \
		/* silversearch error codes */ \
	/* 29 */_(ERR_CODE_SYNTAX_ERROR,		2, "Syntax error in query") \
	/* 30 */_(ERR_CODE_WRONG_FIELD,			2, "Unknown field") \
	/* 31 */_(ERR_CODE_WRONG_NUMBER,		2, "Number value is out of range") \
	/* 32 */_(ERR_CODE_DUPLICATE,			2, "Insert already existing object") \
	/* 33 */_(ERR_CODE_UNUSED32,			0, "Unused33") \
	/* 34 */_(ERR_CODE_UNSUPPORTED_ORDER,		2, "Can not order result") \
	/* 35 */_(ERR_CODE_MULTIWRITE,			2, "Multiple to update/delete") \
	/* 36 */_(ERR_CODE_NOTHING,			0, "Nothing to do (not an error)") \
	/* 37 */_(ERR_CODE_UPDATE_ID,			2, "Id's update") \
	/* 38 */_(ERR_CODE_WRONG_VERSION,		2, "Unsupported version of protocol") \
		/* end of silversearch error codes */					\
	/* 39 */_(ERR_CODE_UNKNOWN_ERROR,		2, "Unknown error") \
	/* 40 */_(ERR_CODE_UNUSED40,			0, "Unused40") \
	/* 41 */_(ERR_CODE_UNUSED41,			0, "Unused41") \
	/* 42 */_(ERR_CODE_UNUSED42,			0, "Unused42") \
	/* 43 */_(ERR_CODE_UNUSED43,			0, "Unused43") \
	/* 44 */_(ERR_CODE_UNUSED44,			0, "Unused44") \
	/* 45 */_(ERR_CODE_UNUSED45,			0, "Unused45") \
	/* 46 */_(ERR_CODE_UNUSED46,			0, "Unused46") \
	/* 47 */_(ERR_CODE_UNUSED47,			0, "Unused47") \
	/* 48 */_(ERR_CODE_UNUSED48,			0, "Unused48") \
        /* 49 */_(ERR_CODE_NODE_NOT_FOUND,		2, "Node isn't found") \
	/* 50 */_(ERR_CODE_UNUSED50,			0, "Unused50") \
	/* 51 */_(ERR_CODE_UNUSED51,			0, "Unused51") \
	/* 52 */_(ERR_CODE_UNUSED52,			0, "Unused52") \
	/* 53 */_(ERR_CODE_UNUSED53,			0, "Unused53") \
	/* 54 */_(ERR_CODE_UNUSED54,			0, "Unused54") \
	/* 55 */_(ERR_CODE_NODE_FOUND,			2, "Node is found") \
	/* 56 */_(ERR_CODE_INDEX_VIOLATION,		2, "Some index violation occur") \
	/* 57 */_(ERR_CODE_NO_SUCH_NAMESPACE,		2, "There is no such namespace")

ENUM0(error_codes, ERROR_CODES);
extern struct errcode_record error_codes_records[];

#endif
