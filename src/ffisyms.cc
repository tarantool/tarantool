#include <bit/bit.h>
#include <lib/msgpuck/msgpuck.h>

/*
 * A special hack to cc/ld to keep symbols in an optimized binary.
 * Please add your symbols to this array if you plan to use it from LuaJIT FFI.
 */
void *ffi_symbols[] = {
	(void *) bswap_u32,
	(void *) bswap_u64,
	(void *) mp_bswap_float,
	(void *) mp_bswap_double
};
