-- uri.lua (internal file)

local ffi = require('ffi')
local buffer = require('buffer')
local internal = require('uri.lib')

local uri_cdef = [[
struct uri_param {
    const char *name;
    int values_capacity;
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
    int params_capacity;
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

size_t
uri_escape(const char *src, size_t src_size, char *dst,
           const unsigned char unreserved[256], bool encode_plus);

size_t
uri_unescape(const char *src, size_t src_size, char *dst, bool decode_plus);
]]

pcall(ffi.cdef, uri_cdef) -- Required for running unit tests.

local builtin = ffi.C;
local uri_stash = buffer.ffi_stash_new('struct uri')
local uri_stash_take = uri_stash.take
local uri_stash_put = uri_stash.put
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

local uri_set_stash = buffer.ffi_stash_new('struct uri_set')
local uri_set_stash_take = uri_set_stash.take
local uri_set_stash_put = uri_set_stash.put

local function unreserved(str)
    if type(str) ~= "string" then
        error("Usage: uri.unreserved(str)")
    end
    local pattern = "^[" .. str .. "]$"
    local unreserved_tbl = ffi.new("unsigned char[256]")
    for i = 0, 255 do
        -- By default symbol is reserved.
        unreserved_tbl[i] = 0
        -- i-th symbol in the extended ASCII table.
        local ch = string.char(i)
        if ch:match(pattern) then
            unreserved_tbl[i] = 1
        end
    end
    return unreserved_tbl
end

-- Some characters, called magic characters, have special meanings when used in
-- a pattern. The magic characters are: % - ^
-- These characters must be escaped in patterns with unreserved symbols.

local ALPHA = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
local DIGIT = "0123456789"
local ALNUM = ALPHA .. DIGIT

local RFC3986 = {
    unreserved = unreserved(ALNUM .. "%-._~"),
    plus = false,
}

local PATH = {
    unreserved = unreserved(ALNUM .. "%-._~" .. "!$&'()*+,;=" .. "/" .. ":@"),
    plus = false,
}

local PATH_PART = {
    unreserved = unreserved(ALNUM .. "%-._~" .. "!$&'()*+,;=" .. ":@"),
    plus = false,
}

local QUERY = {
    unreserved = unreserved(ALNUM .. "%-._~" .. "!$&'()*+,;=" .. "/?"),
    plus = false,
}

local QUERY_PART = {
    unreserved = unreserved(ALNUM .. "%-._~" .. "!$'()*+,;" .. "/?"),
    plus = false,
}

local FRAGMENT = {
    unreserved = unreserved(ALNUM .. "%-._~" .. "!$&'()*+,;=" .. "/?"),
    plus = false,
}

local FORM_URLENCODED = {
    unreserved = unreserved(ALNUM .. "%-._" .. "*"),
    plus = true,
}

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
    local status, errmsg = pcall(internal.uri_create, uribuf, str)
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
    local status, errmsg = pcall(internal.uri_set_create, uri_set_buf, str)
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

local function build_opts(opts)
    if opts and type(opts) ~= "table" then
        error("opts must be a table")
    end
    -- Attention: setting RFC3986.plus to true will break expression with
    -- choosing user and default value below.
    assert(RFC3986.plus == false)
    local options = {
        unreserved = (opts and opts.unreserved) or RFC3986.unreserved,
        plus = (opts and opts.plus) or RFC3986.plus,
    }
    if options.unreserved ~= nil and type(options.unreserved) ~= "cdata" then
        error("use uri.unreserved for building opts.unreserved")
    end
    if type(options.plus) ~= "boolean" then
        error("opts.plus must be a boolean")
    end
    return options
end

-- Encodes a string into its escaped hexadecimal representation.
local function escape(buf, opts)
    if type(buf) ~= "string" then
        error("Usage: uri.escape(string, [opts])")
    end
    local options = build_opts(opts)

    -- The worst case is when all characters are encoded.
    local dst = ffi.new("char[?]", #buf * 3)
    local dst_size = builtin.uri_escape(buf, #buf, dst,
                                        options.unreserved,
                                        options.plus)
    return ffi.string(dst, dst_size)
end

-- Decodes an escaped hexadecimal string into its binary representation.
local function unescape(buf, opts)
    if type(buf) ~= "string" then
        error("Usage: uri.unescape(string, [opts])")
    end
    local options = build_opts(opts)

    -- The worst case is when all characters were not decoded.
    local dst = ffi.new("char[?]", #buf)
    local dst_size = builtin.uri_unescape(buf, #buf, dst, options.plus)
    return ffi.string(dst, dst_size)
end

local function encode_kv(key, values, res, escape_opts)
    local val = values
    if type(val) ~= "table" then
        val = { val }
    end
    local key_escaped= escape(tostring(key), escape_opts)

    -- { a = {} } --> "a"
    if next(val) == nil then
        table.insert(res, key_escaped)
    end

    -- { a = { "b" } } --> "a=c"
    for _, v in pairs(val) do
        local val_escaped = escape(tostring(v), escape_opts)
        local kv = ("%s=%s"):format(key_escaped, val_escaped)
        table.insert(res, kv)
    end
end

-- Encode map to a string and perform escaping of keys and values in
-- parameters.
local function params(opts, escape_opts)
    if opts == nil then
        return ""
    end
    if type(opts) ~= "table" then
        error("Usage: uri.params(table[, escape_opts])")
    end
    if next(opts) == nil then
        return ""
    end
    local res = {}
    for key, value in pairs(opts) do
        if type(key) ~= "string" and
           type(key) ~= "number" then
            error("uri.params: keys must have a type 'string' or 'number'")
        end
        encode_kv(key, value, res, escape_opts)
    end

    return table.concat(res, '&')
end

-- Function allows specify multivalue keys to be added to the query string with
-- params option.
-- { key = uri.values("param1", "param2") }
local function values(...)
    return {...}
end

return {
    parse_many = parse_many,
    parse = parse,
    format = format,
    values = values,
    _internal = {
        params = params,
        encode_kv = encode_kv,
    },
    escape = escape,
    unescape = unescape,
    unreserved = unreserved,

    RFC3986 = RFC3986,
    PATH = PATH,
    PATH_PART = PATH_PART,
    QUERY = QUERY,
    QUERY_PART = QUERY_PART,
    FRAGMENT = FRAGMENT,
    FORM_URLENCODED = FORM_URLENCODED,
};
