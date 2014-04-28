#include "bsdsocket.h"
/*
 * A special hack to cc/ld to keep symbols in an optimized binary.
 * Please add your symbols to this array if you plan to use it from LuaJIT FFI.
 */
void *ffi_symbols[] = {
	(void *)bsdsocket_protocol,
	(void *)bsdsocket_sysconnect,
	(void *)bsdsocket_bind,
	(void *)bsdsocket_nonblock
};
