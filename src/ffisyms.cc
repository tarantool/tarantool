#include "lua/bsdsocket.h"

/*
 * A special hack to cc/ld to keep symbols in an optimized binary.
 * Please add your symbols to this array if you plan to use it from LuaJIT FFI.
 */
void *ffi_symbols[] = {
	(void *)bsdsocket_local_resolve,
	(void *)bsdsocket_nonblock
};
