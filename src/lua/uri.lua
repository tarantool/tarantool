-- uri.lua (internal file)

local ffi = require('ffi')
local buffer = require('buffer')
local uri = require('uri')

ffi.cdef[[
struct uri_param {
    const char *name;
    int value_count;
    const char **values;
};

/**
 * We define all strings inside the `struct uri` as const, despite
 * the fact that they are not constant in the C structure. This is
 * necessary for the `uri_format` function work: there we cannot
 * assign lua strings to a non-constant pointer. It's not a problem
 * since uri_format doesn't change something in `struct uri`.
 */
struct uri {
    const char *scheme;
    const char *login;
    const char *password;
    const char *host;
    const char *service;
    const char *path;
    const char *query;
    const char *fragment;
    int host_hint;
    int param_count;
    struct uri_param *params;
};

struct uri_set {
    int uri_count;
    struct uri *uris;
};

void
uri_destroy(struct uri *uri);

void
uri_set_destroy(struct uri_set *uri_set);

int
uri_format(char *str, size_t len, struct uri *uri, bool write_password);
]]

local builtin = ffi.C;
local uri_stash = buffer.ffi_stash_new('struct uri')
local uri_stash_take = uri_stash.take
local uri_stash_put = uri_stash.put
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

local uri_set_stash = buffer.ffi_stash_new('struct uri_set')
local uri_set_stash_take = uri_set_stash.take
local uri_set_stash_put = uri_set_stash.put

local function parse_uribuf(uribuf)
    local result = {}
    for _, k in ipairs({ 'scheme', 'login', 'password', 'host', 'service',
        'path', 'query', 'fragment'}) do
        if uribuf[k] ~= nil then
            result[k] = ffi.string(uribuf[k])
        end
    end
    if uribuf.param_count > 0 then
        result.params = {}
        for param_idx = 0, uribuf.param_count - 1 do
            local param = uribuf.params[param_idx]
            local name = ffi.string(param.name)
            result.params[name] = {}
            for val_idx = 0, param.value_count - 1 do
                result.params[name][val_idx + 1] =
                    ffi.string(param.values[val_idx])
            end
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

local function parse(str)
    if str == nil then
        error("Usage: uri.parse(string|table)")
    end
    local uribuf = uri_stash_take()
    local status, errmsg = pcall(uri.internal.uri_create, uribuf, str)
    if not status then
        uri_stash_put(uribuf)
        return nil, errmsg
    end
    local result = parse_uribuf(uribuf)
    builtin.uri_destroy(uribuf)
    uri_stash_put(uribuf)
    return result
end

local function parse_many(str)
    if str == nil then
        error("Usage: uri.parse_many(string|table)")
    end
    local uri_set_buf = uri_set_stash_take()
    local status, errmsg = pcall(uri.internal.uri_set_create, uri_set_buf, str)
    if not status then
        uri_set_stash_put(uri_set_buf)
        return nil, errmsg
    end
    local result = {}
    for i = 0, uri_set_buf.uri_count - 1 do
        result[i + 1] = parse_uribuf(uri_set_buf.uris[i])
    end
    builtin.uri_set_destroy(uri_set_buf)
    uri_set_stash_put(uri_set_buf)
    return result
end

local function fill_uribuf_params(uribuf, uri)
    uribuf.param_count = 0
    uribuf.params = nil
    if not uri.params then
        return
    end
    for _, _ in pairs(uri.params) do
        uribuf.param_count = uribuf.param_count + 1
    end
    if uribuf.param_count == 0 then
        return
    end
    uribuf.params = ffi.new("struct uri_param[?]", uribuf.param_count)
    local i = 0
    for param_name, param in pairs(uri.params) do
        uribuf.params[i].value_count = #param
        uribuf.params[i].name = param_name
        uribuf.params[i].values =
            ffi.new("const char *[?]", uribuf.params[i].value_count)
        for j = 1, uribuf.params[i].value_count do
            uribuf.params[i].values[j - 1] = param[j]
        end
        i = i + 1
    end
end

local function format(uri, write_password)
    local uribuf = uri_stash_take()
    uribuf.scheme = uri.scheme
    uribuf.login = uri.login
    uribuf.password = uri.password
    uribuf.host = uri.host
    uribuf.service = uri.service
    uribuf.path = uri.path
    uribuf.query = uri.query
    uribuf.fragment = uri.fragment
    fill_uribuf_params(uribuf, uri)
    local ibuf = cord_ibuf_take()
    local str = ibuf:alloc(1024)
    local len = builtin.uri_format(str, 1024, uribuf, write_password and 1 or 0)
    uri_stash_put(uribuf)
    str = ffi.string(str, len)
    cord_ibuf_put(ibuf)
    return str
end

return {
    parse_many = parse_many,
    parse = parse,
    format = format,
};
