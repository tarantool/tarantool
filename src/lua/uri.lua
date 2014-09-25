-- uri.lua (internal file)

local ffi = require('ffi')

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
]]

local builtin = ffi.C;

local uribuf = ffi.new('struct uri')

local function parse(str)
    if str == nil then
        error("Usage: uri.parse(string)")
    end
    if builtin.uri_parse(uribuf, str) ~= 0 then
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
    return result
end

return {
    parse = parse;
};
