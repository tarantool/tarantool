#include <bit/bit.h>
#include <lib/msgpuck/msgpuck.h>
#include <box/tuple.h>
#include <box/lua/index.h>
#include <box/lua/call.h>

/*
 * A special hack to cc/ld to keep symbols in an optimized binary.
 * Please add your symbols to this array if you plan to use it from LuaJIT FFI.
 */
void *ffi_symbols[] = {
	(void *) bswap_u32,
	(void *) bswap_u64,
	(void *) mp_bswap_float,
	(void *) mp_bswap_double,
	(void *) tuple_arity,
	(void *) tuple_field,
	(void *) tuple_rewind,
	(void *) tuple_seek,
	(void *) tuple_next,
	(void *) tuple_ref,
	(void *) boxffi_index_iterator,
	(void *) port_ffi_create,
	(void *) port_ffi_destroy,
	(void *) boxffi_select
};
