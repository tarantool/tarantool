-- uri.lua (internal file)

---The [URI](https://en.wikipedia.org/wiki/Uniform_Resource_Identifier)
---module provides functions that convert URI strings into their
---components, or turn components into URI strings, for example:
---
---```lua
---local uri = require('uri')
---
---parsed_uri = uri.parse('https://www.tarantool.io/doc/latest/' ..
---    'reference/reference_lua/http/#api-reference')
-----[[
----
--- - host: www.tarantool.io
---   fragment: api-reference
---   scheme: https
---   path: /doc/latest/reference/reference_lua/http/
--- ...
--- --]]
---
---formatted_uri = uri.format({
---    scheme = 'https',
---    host = 'www.tarantool.io',
---    path = '/doc/latest/reference/reference_lua/http/',
---    fragment = 'api-reference'
---})
-----[[
----
--- - https://www.tarantool.io/doc/latest/reference/reference_lua/http/#api-reference
--- ...
--- --]]
---```
---
---To escape and unescape special characters, corresponding functions
---must be used:
---
---```lua
---local uri = require('uri')
---
---escaped_string = uri.escape('C++')
----- C%2B%2B
---
---escaped_string_url_enc = uri.escape('John Smith', uri.FORM_URLENCODED)
----- John+Smith
---
---local escape_opts = {
---    plus = true,
---    unreserved = uri.unreserved('a-z')
---}
---escaped_string_custom = uri.escape('Hello World', escape_opts)
----- '%48ello+%57orld'
---```
---
---You can also use this module to encode and decode arbitrary strings
---using the specified encoding options.
local module = {}

---@class uri
---@field scheme? string a URI scheme. Examples: 'https', 'http'.
---@field login? string a user name, which is a part of the
---    'userinfo' subcomponent.
---@field password? string a password, which is a part of the
---    'userinfo' subcomponent.
---@field host? string a host subcomponent. Example: 'www.tarantool.io'.
---@field service? string a service subcomponent: a port value, or a
---    Unix socket path.
---@field path? string a path component.
---@field query? string a query component. Example: 'key1=value1&key2=value2'.
---@field params? table<string, string[]> parameters of a query component,
---    overrides 'query'.
---@field fragment? string a fragment component. Example: 'api-reference'.
---@field ipv4? string an IPv4 address. Example: '127.0.0.1'.
---@field ipv6? string an IPv6 address. Example:
---    '2a00:1148:b0ba:2016:12bf:48ff:fe78:fd10'.
---@field unix? string a Unix domain socket. Example: '/tmp/unix.sock'.

---@alias uri_like uri | string

---@class uri.encoding_opt
---@field plus? boolean enable encoding of '+' as the space character. By
---    default this property is set to false.
---@field unreserved? ffi.cdata* a Lua pattern defining unreserved symbols
---    that are not encoded. Must be built with `uri.unreserved()`.

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
uri_format(char *str, int len, struct uri *uri, bool write_password);

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

---Build a table of unreserved symbols for use in custom encoding options.
---
---The returned value can be assigned to the `unreserved` field of the
---`uri_encoding_opts` object.
---
---@param str string a Lua pattern defining the unreserved symbols.
---@return ffi.cdata* unreserved_tbl a table of unreserved symbols.
function module.unreserved(str)
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

---Encoding options that use unreserved symbols defined in RFC 3986.
---These are default options used to encode and decode using the
---`uri.escape()` and `uri.unescape()` functions, respectively.
---@type uri.encoding_opt
module.RFC3986 = {
    unreserved = module.unreserved(ALNUM .. "%-._~"),
    plus = false,
}

---Options used to encode the `path` URI component.
---@type uri.encoding_opt
module.PATH = {
    unreserved = module.unreserved(ALNUM .. "%-._~" ..
        "!$&'()*+,;=" .. "/" .. ":@"),
    plus = false,
}

---Options used to encode specific `path` parts.
---@type uri.encoding_opt
module.PATH_PART = {
    unreserved = module.unreserved(ALNUM .. "%-._~" .. "!$&'()*+,;=" .. ":@"),
    plus = false,
}

---Options used to encode the `query` URI component.
---@type uri.encoding_opt
module.QUERY = {
    unreserved = module.unreserved(ALNUM .. "%-._~" .. "!$&'()*+,;=" .. "/?"),
    plus = false,
}

---Options used to encode specific `query` parts.
---@type uri.encoding_opt
module.QUERY_PART = {
    unreserved = module.unreserved(ALNUM .. "%-._~" .. "!$'()*+,;" .. "/?"),
    plus = false,
}

---Options used to encode the `fragment` URI component.
---@type uri.encoding_opt
module.FRAGMENT = {
    unreserved = module.unreserved(ALNUM .. "%-._~" .. "!$&'()*+,;=" .. "/?"),
    plus = false,
}

---Options used to encode `application/x-www-form-urlencoded` form parameters.
---@type uri.encoding_opt
module.FORM_URLENCODED = {
    unreserved = module.unreserved(ALNUM .. "%-._" .. "*"),
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

---Parse a URI string (or a URI table) into components.
---
---The table form has a URI string and an optional override of URI query
---parameters. The URI string table key must be `'uri'` or `1` (the first
---array-like element). The override of URI query parameters must be given
---in the `'params'` element of the table.
---
---**Example:**
---
--- ```lua
--- local uri = require('uri')
---
--- parsed_uri = uri.parse('https://www.tarantool.io/doc/latest/' ..
---     'reference/reference_lua/http/#api-reference')
--- --[[
--- ---
--- - host: www.tarantool.io
---   fragment: api-reference
---   scheme: https
---   path: /doc/latest/reference/reference_lua/http/
--- ...
--- --]]
--- ```
---
---@param str uri_like a Uniform Resource Identifier, or a URI table.
---@return uri? parsed the parsed URI components.
---@return any? err an error message if a parsing error occurs.
function module.parse(str)
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

---Parse a string containing many URIs into an array of components.
---
---@param str uri_like a string containing one or more URIs.
---@return uri[]? parsed the array of parsed URI components.
---@return any? err an error message if a parsing error occurs.
function module.parse_many(str)
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
        uribuf.params[i].name = param_name
        if type(param) == "table" then
            uribuf.params[i].value_count = #param
            uribuf.params[i].values = ffi.new("const char *[?]", #param)
            for j = 1, uribuf.params[i].value_count do
                uribuf.params[i].values[j - 1] = param[j]
            end
        elseif param == nil then
            uribuf.params[i].value_count = 0
        else
            uribuf.params[i].value_count = 1
            uribuf.params[i].values = ffi.new("const char *[?]", 1)
            uribuf.params[i].values[0] = param
        end
        i = i + 1
    end
end

---Construct a URI from the specified components.
---
---**Example:**
---
--- ```lua
--- formatted_uri = uri.format({
---     scheme = 'https',
---     host = 'www.tarantool.io',
---     path = '/doc/latest/reference/' .. 'reference_lua/http/',
---     fragment = 'api-reference'
--- })
--- --[[
--- ---
--- - https://www.tarantool.io/doc/latest/reference/reference_lua/http/#api-reference
--- ...
--- --]]
--- ```
---
---@param components uri a series of name=value pairs, one for each component.
---@param write_password? boolean specify whether the password component is
---    rendered in clear text; otherwise, it is omitted.
---@return string result the resulting URI string.
function module.format(components, write_password)
    local uribuf = uri_stash_take()
    uribuf.scheme = components.scheme
    uribuf.login = components.login
    uribuf.password = components.password
    uribuf.host = components.host
    uribuf.service = components.service
    uribuf.path = components.path
    uribuf.query = components.query
    uribuf.fragment = components.fragment
    -- The `uri_format` function needs the hint to enclose an IPv6
    -- address into square brackets. So we must set the `host_hint`
    -- to determine the type of `components.host`.
    if components.ipv4 ~= nil then
        uribuf.host_hint = 1
    elseif components.ipv6 ~= nil then
        uribuf.host_hint = 2
    else
        uribuf.host_hint = 3
    end
    fill_uribuf_params(uribuf, components)
    local ibuf = cord_ibuf_take()
    local default_len = 1024
    local str = ibuf:alloc(default_len)
    local write_sensitive = write_password and 1 or 0
    local len = builtin.uri_format(str, default_len, uribuf, write_sensitive)
    if len >= default_len then
        ibuf:reset()
        str = ibuf:alloc(len + 1)
        len = builtin.uri_format(str, len + 1, uribuf, write_sensitive)
    end
    uri_stash_put(uribuf)
    str = ffi.string(str, len)
    cord_ibuf_put(ibuf)
    return str
end

local function build_opts(opts)
    if opts and type(opts) ~= "table" then
        error("opts must be a table")
    end
    -- Attention: setting module.RFC3986.plus to true will break expression with
    -- choosing user and default value below.
    assert(module.RFC3986.plus == false)
    local options = {
        unreserved = (opts and opts.unreserved) or module.RFC3986.unreserved,
        plus = (opts and opts.plus) or module.RFC3986.plus,
    }
    if options.unreserved ~= nil and type(options.unreserved) ~= "cdata" then
        error("use uri.unreserved for building opts.unreserved")
    end
    if type(options.plus) ~= "boolean" then
        error("opts.plus must be a boolean")
    end
    return options
end

---Encode a string using the specified encoding options.
---
---*Since 2.11.0*
---
---By default, `uri.escape()` uses encoding options defined by the
---[`uri.RFC3986`](lua://uri.RFC3986) table. If required, you can customize
---encoding options using the `uri_encoding_opts` optional parameter, for
---example:
---
---* Pass the predefined set of options targeted for encoding a specific URI
---  part (for example, [`uri.PATH`](lua://uri.PATH) or
---  [`uri.QUERY`](lua://uri.QUERY)).
---* Pass custom encoding options using the
---  [uri.encoding_opts](uri.encoding_opts) object.
---
---**Examples:**
---
--- ```lua
--- escaped_string = uri.escape('C++')
--- --[[
--- ---
--- - C%2B%2B
--- ...
--- --]]
--- ```
---
--- ```lua
--- escaped_string_url_enc = uri.escape('John Smith', uri.FORM_URLENCODED)
--- --[[
--- ---
--- - John+Smith
--- ...
--- --]]
--- ```
---
--- ```lua
--- local escape_opts = {
---     plus = true,
---     unreserved = uri.unreserved("a-z")
--- }
--- escaped_string_custom = uri.escape('Hello World', escape_opts)
--- --[[
--- ---
--- - '%48ello+%57orld'
--- ...
--- --]]
--- ```
---
---@param buf string the string to encode.
---@param opts? uri.encoding_opt encoding options. Default is `uri.RFC3986`.
---@return string result the encoded string.
function module.escape(buf, opts)
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

---Decode a string using the specified encoding options.
---
---*Since 2.11.0*
---
---By default, `uri.unescape()` uses encoding options defined by the
---[`uri.RFC3986`](lua://uri.RFC3986) table. If required, you can customize
---encoding options using the `uri_encoding_opts` optional parameter, for
---example:
---
---* Pass the predefined set of options targeted for encoding a specific URI
---  part (for example, [`uri.PATH`](lua://uri.PATH) or
---  [`uri.QUERY`](lua://uri.QUERY)).
---* Pass custom encoding options using the
---  [uri.encoding_opts](uri.encoding_opts) object.
---
---**Examples:**
---
--- ```lua
--- unescaped_string = uri.unescape('C%2B%2B')
--- --[[
--- ---
--- - C++
--- ...
--- --]]
--- ```
---
--- ```lua
--- unescaped_string_url_enc = uri.unescape('John+Smith', uri.FORM_URLENCODED)
--- --[[
--- ---
--- - John Smith
--- ...
--- --]]
--- ```
---
--- ```lua
--- local escape_opts = {
---     plus = true,
---     unreserved = uri.unreserved("a-z")
--- }
--- unescaped_string_custom = uri.unescape('%48ello+%57orld', escape_opts)
--- --[[
--- ---
--- - Hello World
--- ...
--- --]]
--- ```
---
---@param buf string the string to decode.
---@param opts? uri.encoding_opt encoding options. Default is `uri.RFC3986`.
---@return string result the decoded string.
function module.unescape(buf, opts)
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
    local key_escaped= module.escape(tostring(key), escape_opts)

    -- { a = {} } --> "a"
    if next(val) == nil then
        table.insert(res, key_escaped)
    end

    -- { a = { "b" } } --> "a=c"
    for _, v in pairs(val) do
        local val_escaped = module.escape(tostring(v), escape_opts)
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

---Allow specifying multivalue keys to be added to the query string with the
---`params` option.
---
---**Example:**
---
--- ```lua
--- -- { key = uri.values("param1", "param2") }
--- ```
---
---@param ... any the values.
---@return any[] values the array of the specified values.
function module.values(...)
    return {...}
end

module._internal = {
    params = params,
    encode_kv = encode_kv,
}

return module
