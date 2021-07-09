#!/usr/bin/env tarantool

local tap = require('tap')
local ffi = require('ffi')
ffi.cdef([[
    void *dlsym(void *handle, const char *symbol);
    struct curl_version_info_data {
        int age;                  /* see description below */
        const char *version;      /* human readable string */
        unsigned int version_num; /* numeric representation */
        const char *host;         /* human readable string */
        int features;             /* bitmask, see below */
        char *ssl_version;        /* human readable string */
        long ssl_version_num;     /* not used, always zero */
        const char *libz_version; /* human readable string */
        const char * const *protocols; /* protocols */

        /* when 'age' is CURLVERSION_SECOND or higher, the members below exist */
        const char *ares;         /* human readable string */
        int ares_num;             /* number */

        /* when 'age' is CURLVERSION_THIRD or higher, the members below exist */
        const char *libidn;       /* human readable string */

        /* when 'age' is CURLVERSION_FOURTH or higher (>= 7.16.1), the members
           below exist */
        int iconv_ver_num;       /* '_libiconv_version' if iconv support enabled */

        const char *libssh_version; /* human readable string */

        /* when 'age' is CURLVERSION_FIFTH or higher (>= 7.57.0), the members
           below exist */
        unsigned int brotli_ver_num; /* Numeric Brotli version
                                        (MAJOR << 24) | (MINOR << 12) | PATCH */
        const char *brotli_version; /* human readable string. */

        /* when 'age' is CURLVERSION_SIXTH or higher (>= 7.66.0), the members
           below exist */
        unsigned int nghttp2_ver_num; /* Numeric nghttp2 version
                                         (MAJOR << 16) | (MINOR << 8) | PATCH */
        const char *nghttp2_version; /* human readable string. */

        const char *quic_version;    /* human readable quic (+ HTTP/3) library +
                                        version or NULL */

        /* when 'age' is CURLVERSION_SEVENTH or higher (>= 7.70.0), the members
           below exist */
        const char *cainfo;          /* the built-in default CURLOPT_CAINFO, might
                                        be NULL */
        const char *capath;          /* the built-in default CURLOPT_CAPATH, might
                                        be NULL */
    };

    struct curl_version_info_data *curl_version_info(int age);
]])

local info = ffi.C.curl_version_info(7)
local test = tap.test('curl-features')
test:plan(5)

if test:ok(info.ssl_version ~= nil, 'Curl built with SSL support') then
    test:diag('ssl_version: ' .. ffi.string(info.ssl_version))
end
if test:ok(info.libz_version ~= nil, 'Curl built with LIBZ') then
    test:diag('libz_version: ' .. ffi.string(info.libz_version))
end

local RTLD_DEFAULT
-- See `man 3 dlsym`:
-- RTLD_DEFAULT
--   Find  the  first occurrence of the desired symbol using the default
--   shared object search order.  The search will include global symbols
--   in the executable and its dependencies, as well as symbols in shared
--   objects that were dynamically loaded with the RTLD_GLOBAL flag.
if jit.os == "OSX" then
    RTLD_DEFAULT = ffi.cast("void *", -2LL)
else
    RTLD_DEFAULT = ffi.cast("void *", 0LL)
end

--
-- gh-5223: Check if all curl symbols are exported.
--
-- The following list was obtained by parsing libcurl.a static library:
-- nm libcurl.a | grep -oP 'T \K(curl_.+)$' | sort
local curl_symbols = {
    'curl_easy_cleanup',
    'curl_easy_duphandle',
    'curl_easy_escape',
    'curl_easy_getinfo',
    'curl_easy_init',
    'curl_easy_pause',
    'curl_easy_perform',
    'curl_easy_recv',
    'curl_easy_reset',
    'curl_easy_send',
    'curl_easy_setopt',
    'curl_easy_strerror',
    'curl_easy_unescape',
    'curl_easy_upkeep',
    'curl_escape',
    'curl_formadd',
    'curl_formfree',
    'curl_formget',
    'curl_free',
    'curl_getdate',
    'curl_getenv',
    'curl_global_cleanup',
    'curl_global_init',
    'curl_global_init_mem',
    'curl_global_sslset',
    'curl_maprintf',
    'curl_mfprintf',
    'curl_mime_addpart',
    'curl_mime_data',
    'curl_mime_data_cb',
    'curl_mime_encoder',
    'curl_mime_filedata',
    'curl_mime_filename',
    'curl_mime_free',
    'curl_mime_headers',
    'curl_mime_init',
    'curl_mime_name',
    'curl_mime_subparts',
    'curl_mime_type',
    'curl_mprintf',
    'curl_msnprintf',
    'curl_msprintf',
    'curl_multi_add_handle',
    'curl_multi_assign',
    'curl_multi_cleanup',
    'curl_multi_fdset',
    'curl_multi_info_read',
    'curl_multi_init',
    'curl_multi_perform',
    'curl_multi_poll',
    'curl_multi_remove_handle',
    'curl_multi_setopt',
    'curl_multi_socket',
    'curl_multi_socket_action',
    'curl_multi_socket_all',
    'curl_multi_strerror',
    'curl_multi_timeout',
    'curl_multi_wait',
    'curl_mvaprintf',
    'curl_mvfprintf',
    'curl_mvprintf',
    'curl_mvsnprintf',
    'curl_mvsprintf',
    'curl_pushheader_byname',
    'curl_pushheader_bynum',
    'curl_share_cleanup',
    'curl_share_init',
    'curl_share_setopt',
    'curl_share_strerror',
    'curl_slist_append',
    'curl_slist_free_all',
    'curl_strequal',
    'curl_strnequal',
    'curl_unescape',
    'curl_url',
    'curl_url_cleanup',
    'curl_url_dup',
    'curl_url_get',
    'curl_url_set',
    'curl_version',
    'curl_version_info',
}

test:test('curl_symbols', function(t)
    t:plan(#curl_symbols)
    for _, sym in ipairs(curl_symbols) do
        t:ok(
            ffi.C.dlsym(RTLD_DEFAULT, sym) ~= nil,
            ('Symbol %q found'):format(sym)
        )
    end
end)

local function has_protocol(protocol_str)
    local i = 0
    -- curl_version_info_data.protocols is a null terminated array
    -- of pointers to char.
    -- See curl/lib/version.c:
    --   static const char * const protocols[]
    local info = ffi.C.curl_version_info(7)
    local protocol = info.protocols[i]
    while protocol ~= nil do
        if ffi.string(protocol) == protocol_str then
            return true
        end
        i = i + 1
        protocol = info.protocols[i]
    end

    return false
end

--
-- gh-4559: check if smtp and smtps protocols are enabled.
--
test:ok(has_protocol('smtp'), 'smtp protocol is supported')
test:ok(has_protocol('smtps'), 'smtps protocol is supported')

os.exit(test:check() and 0 or 1)
