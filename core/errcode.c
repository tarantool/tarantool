#include <errcode.h>

#define ERRCODE_RECORD_MEMBER(s, f, d) {	\
	.errstr = #s,				\
	.errflags = f,				\
	.errdesc = #d				\
},

struct errcode_record error_codes_records[error_codes_MAX] = {
	ERROR_CODES(ERRCODE_RECORD_MEMBER)
};

