-- uri.lua (internal file)

local ffi = require('ffi')
local buffer = require('buffer')

ffi.cdef[[
struct uri {
    const char *scheme;
    size_t scheme_len;
    const char *login;
    size_t login_len;
    const char *password;
    size_t password_len;
    const char *host;
    size_t host_len;
    const char *service;
    size_t service_len;
    const char *path;
    size_t path_len;
    const char *query;
    size_t query_len;
    const char *fragment;
    size_t fragment_len;
    int host_hint;
};

int
uri_parse(struct uri *uri, const char *str);

int
uri_format(char *str, size_t len, struct uri *uri, bool write_password);
]]

local builtin = ffi.C;
local uri_stash = buffer.ffi_stash_new('struct uri')
local uri_stash_take = uri_stash.take
local uri_stash_put = uri_stash.put
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

local function parse(str)
    if str == nil then
        error("Usage: uri.parse(string)")
    end
    local uribuf = uri_stash_take()
    if builtin.uri_parse(uribuf, str) ~= 0 then
        uri_stash_put(uribuf)
        return nil
    end
    local result = {}
    for _, k in ipairs({ 'scheme', 'login', 'password', 'host', 'service',
        'path', 'query', 'fragment'}) do
        if uribuf[k] ~= nil then
            result[k] = ffi.string(uribuf[k], uribuf[k..'_len'])
        end
    end
    if uribuf.host_hint == 1 then
        result.ipv4 = result.host
    elseif uribuf.host_hint == 2 then
        result.ipv6 = result.host
    elseif uribuf.host_hint == 3 then
        result.unix = result.service
    end
    uri_stash_put(uribuf)
    return result
end

local function format(uri, write_password)
    local uribuf = uri_stash_take()
    uribuf.scheme = uri.scheme
    uribuf.scheme_len = string.len(uri.scheme or '')
    uribuf.login = uri.login
    uribuf.login_len = string.len(uri.login or '')
    uribuf.password = uri.password
    uribuf.password_len = string.len(uri.password or '')
    uribuf.host = uri.host
    uribuf.host_len = string.len(uri.host or '')
    uribuf.service = uri.service
    uribuf.service_len = string.len(uri.service or '')
    uribuf.path = uri.path
    uribuf.path_len = string.len(uri.path or '')
    uribuf.query = uri.query
    uribuf.query_len = string.len(uri.query or '')
    uribuf.fragment = uri.fragment
    uribuf.fragment_len = string.len(uri.fragment or '')
    local ibuf = cord_ibuf_take()
    local str = ibuf:alloc(1024)
    local len = builtin.uri_format(str, 1024, uribuf, write_password and 1 or 0)
    uri_stash_put(uribuf)
    str = ffi.string(str, len)
    cord_ibuf_put(ibuf)
    return str
end

return {
    parse = parse,
    format = format,
};
