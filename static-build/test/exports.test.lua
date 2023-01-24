#!/usr/bin/env tarantool

local tap = require('tap')
local ffi = require('ffi')
ffi.cdef([[
    void *dlsym(void *handle, const char *symbol);
]])

local test = tap.test('exports')

-- See `man 3 dlsym`:
-- RTLD_DEFAULT
--   Find the first occurrence of the desired symbol using the default
--   shared object search order. The search will include global symbols
--   in the executable and its dependencies, as well as symbols in shared
--   objects that were dynamically loaded with the RTLD_GLOBAL flag.
local RTLD_DEFAULT = ffi.cast("void *", jit.os == "OSX" and -2LL or 0LL)

local function check_symbol(sym)
    test:ok(ffi.C.dlsym(RTLD_DEFAULT, sym) ~= nil, ('Symbol %q found'):format(sym))
end

local check_symbols = {
    -- FFI

    'guava',
    'base64_decode',
    'base64_encode',
    'cord_ibuf_drop',
    'cord_ibuf_put',
    'cord_ibuf_take',
    'random_bytes',
    'fiber_time',
    'ibuf_create',
    'ibuf_destroy',
    'port_destroy',
    'csv_create',
    'csv_destroy',
    'title_get',
    'title_update',
    'tnt_iconv',
    'tnt_iconv_open',
    'tnt_iconv_close',
    'tnt_mp_decode_double',
    'tnt_mp_decode_extl',
    'tnt_mp_decode_float',
    'tnt_mp_encode_decimal',
    'tnt_mp_encode_double',
    'tnt_mp_encode_error',
    'tnt_mp_encode_float',
    'tnt_mp_encode_uuid',
    'tnt_mp_sizeof_error',
    'tnt_mp_sizeof_decimal',
    'tnt_mp_sizeof_uuid',

    'uuid_nil',
    'tt_uuid_create',
    'tt_uuid_is_equal',
    'tt_uuid_is_nil',
    'tt_uuid_bswap',
    'tt_uuid_from_string',
    'tt_uuid_to_string',
    'log_level',
    'log_format',
    'uri_destroy',
    'uri_format',
    'uri_set_destroy',
    'PMurHash32',
    'PMurHash32_Process',
    'PMurHash32_Result',
    'crc32_calc',
    'decimal_unpack',

    'say_set_log_level',
    'say_logrotate',
    'say_set_log_format',
    'tarantool_uptime',
    'tarantool_exit',
    'log_pid',
    'space_by_id',
    'space_run_triggers',
    'space_bsize',
    'box_schema_version',

    'crypto_ERR_error_string',
    'crypto_ERR_get_error',
    'crypto_EVP_DigestInit_ex',
    'crypto_EVP_DigestUpdate',
    'crypto_EVP_DigestFinal_ex',
    'crypto_EVP_get_digestbyname',
    'crypto_EVP_MD_CTX_new',
    'crypto_EVP_MD_CTX_free',
    'crypto_HMAC_CTX_new',
    'crypto_HMAC_CTX_free',
    'crypto_HMAC_Init_ex',
    'crypto_HMAC_Update',
    'crypto_HMAC_Final',
    'crypto_stream_new',
    'crypto_stream_begin',
    'crypto_stream_append',
    'crypto_stream_commit',
    'crypto_stream_delete',

    -- Module API

    '_say',
    'swim_cfg',
    'swim_quit',
    'fiber_new',
    'fiber_cancel',
    'coio_wait',
    'coio_close',
    'coio_call',
    'coio_getaddrinfo',
    'luaT_call',
    'box_txn',
    'clock_realtime',
    'string_strip_helper',

    -- Lua / LuaJIT

    'lua_newstate',
    'lua_close',
    'luaL_loadstring',
    'luaJIT_profile_start',
    'luaJIT_profile_stop',
    'luaJIT_profile_dumpstack',
}

-- TODO gh-7640 LuaJIT: ffi.C.dlsym() doesn't work on FreeBSD
jit.off()

test:plan(#check_symbols)
for _, sym in ipairs(check_symbols) do
    check_symbol(sym)
end

os.exit(test:check() and 0 or 1)
