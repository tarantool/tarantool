#include <errcode.h>

#define ERRCODE_RECORD_MEMBER(s, f, d) {	\
	.errstr = #s,				\
	.errflags = f,				\
	.errdesc = #d				\
},

struct errcode_record tnt_error_codes[tnt_error_codes_enum_MAX] = {
	ERROR_CODES(ERRCODE_RECORD_MEMBER)
};

