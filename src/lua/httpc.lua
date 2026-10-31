--
--  Copyright (C) 2016-2017 Tarantool AUTHORS: please see AUTHORS file.
--
--  Redistribution and use in source and binary forms, with or
--  without modification, are permitted provided that the following
--  conditions are met:
--
--  1. Redistributions of source code must retain the above
--   copyright notice, this list of conditions and the
--   following disclaimer.
--
--  2. Redistributions in binary form must reproduce the above
--   copyright notice, this list of conditions and the following
--   disclaimer in the documentation and/or other materials
--   provided with the distribution.
--
--  THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
--  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
--  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
--  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
--  <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
--  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
--  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
--  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
--  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
--  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
--  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
--  THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
--  SUCH DAMAGE.
--

---# Builtin `http` module
---
---The `http` module, specifically the `http.client` submodule, provides the
---functionality of an HTTP client with support for HTTPS and keepalive.
---
---The HTTP client uses the [libcurl](https://curl.haxx.se/libcurl/) library
---under the hood and takes into account the environment variables libcurl
---understands.
---@class http.client
---The `http.client` module provides the API for making HTTP requests. The
---module itself acts as the default client instance: request methods can be
---called with the dot syntax, for example `http.get(url, opts)`. To create a
---configured client instance, use `http.client.new()`.
---
---@field new fun(options?: http.client.options): http.client_object
---
---Encoders used to serialize request bodies.
---@field encoders table<string, fun(body: any, content_type?: string): string>
---
---Decoders used to deserialize response bodies.
---@field decoders table<string, fun(body: string, content_type?: string): any>
---
---Make an HTTP request and receive a response.
---@field request fun(method: string, url: string, body?: any,
---    options?: http.client.request.options): http.client.result
---
---Make a `GET` request and receive a response.
---@field get fun(url: string, options?: http.client.request.options):
---    http.client.result
---
---Make a `POST` request and receive a response.
---@field post fun(url: string, body?: any,
---    options?: http.client.request.options): http.client.result
---
---Make a `PUT` request and receive a response.
---@field put fun(url: string, body?: any,
---    options?: http.client.request.options): http.client.result
---
---Make a `PATCH` request and receive a response.
---@field patch fun(url: string, body?: any,
---    options?: http.client.request.options): http.client.result
---
---Make an `OPTIONS` request and receive a response.
---@field options fun(url: string, options?: http.client.request.options):
---    http.client.result
---
---Make a `HEAD` request and receive a response.
---@field head fun(url: string, options?: http.client.request.options):
---    http.client.result
---
---Make a `DELETE` request and receive a response.
---@field delete fun(url: string, options?: http.client.request.options):
---    http.client.result
---
---Make a `TRACE` request and receive a response.
---@field trace fun(url: string, options?: http.client.request.options):
---    http.client.result
---
---Make a `CONNECT` request and receive a response.
---@field connect fun(url: string, options?: http.client.request.options):
---    http.client.result

---@alias http.client.result http.response|http.io

---@class http.client_object
---An HTTP client instance that exposes the API for making requests. To
---create a client, call `http.client.new()`. Request methods are called
---with the colon syntax, for example `client:get(url, opts)`.
---
---@field request fun(self: http.client_object, method: string, url: string,
---    body?: any, options?: http.client.request.options): http.client.result
---
---Make a `GET` request and receive a response.
---@field get fun(self: http.client_object, url: string,
---    options?: http.client.request.options): http.client.result
---
---Make a `POST` request and receive a response.
---@field post fun(self: http.client_object, url: string, body?: any,
---    options?: http.client.request.options): http.client.result
---
---Make a `PUT` request and receive a response.
---@field put fun(self: http.client_object, url: string, body?: any,
---    options?: http.client.request.options): http.client.result
---
---Make a `PATCH` request and receive a response.
---@field patch fun(self: http.client_object, url: string, body?: any,
---    options?: http.client.request.options): http.client.result
---
---Make an `OPTIONS` request and receive a response.
---@field options fun(self: http.client_object, url: string,
---    options?: http.client.request.options): http.client.result
---
---Make a `HEAD` request and receive a response.
---@field head fun(self: http.client_object, url: string,
---    options?: http.client.request.options): http.client.result
---
---Make a `DELETE` request and receive a response.
---@field delete fun(self: http.client_object, url: string,
---    options?: http.client.request.options): http.client.result
---
---Make a `TRACE` request and receive a response.
---@field trace fun(self: http.client_object, url: string,
---    options?: http.client.request.options): http.client.result
---
---Make a `CONNECT` request and receive a response.
---@field connect fun(self: http.client_object, url: string,
---    options?: http.client.request.options): http.client.result
---
---Get a table with statistics for the HTTP client.
---@field stat fun(self: http.client_object): http.client.stat
---
---Encoders used to serialize request bodies.
---@field encoders table<string, fun(body: any, content_type?: string): string>
---
---Decoders used to deserialize response bodies.
---@field decoders table<string, fun(body: string, content_type?: string): any>

---@class http.client.options
---Configuration options of the client. These options can be passed to the
---`http.client.new()` function.
---
---@field max_connections? integer The maximum number of entries in the
---    connection cache. The default is -1.
---@field max_total_connections? integer The maximum number of active
---    connections. The default is 0, which allows libcurl to scale
---    accordingly.

---@class http.client.request.options
---Options passed to a request method (`request`, `get`, `post`, and so on).
---
---@field ca_file? string The path to an SSL certificate file to verify the
---    peer with.
---@field ca_path? string The path to a directory holding one or more
---    certificates to verify the peer with.
---@field chunked? boolean Whether to return an IO object for streaming
---    download/upload instead of the full response. The default is false.
---@field headers? table<string, string|string[]> A table of HTTP headers.
---@field params? table<string, any> A table of parameters passed to a
---    request. For GET, HEAD, and DELETE requests, these are query string
---    parameters. For POST, PUT, and PATCH requests, they are sent as form
---    parameters using the application/x-www-form-urlencoded type.
---@field keepalive_idle? integer A delay (in seconds) the operating system
---    waits while the connection is idle before sending keepalive probes.
---@field keepalive_interval? integer The interval (in seconds) the operating
---    system waits between sending keepalive probes. If both keepalive_idle
---    and keepalive_interval are set, then Tarantool also sets the HTTP
---    keepalive headers: `Connection:Keep-Alive` and
---    `Keep-Alive:timeout=<keepalive_idle>`. Otherwise, Tarantool sends
---    `Connection:close`.
---@field low_speed_limit? integer The average transfer speed in bytes per
---    second that the transfer should be below during "low speed time"
---    seconds for the library to consider it to be too slow and abort.
---@field low_speed_time? integer The time that the transfer speed should be
---    below the "low speed limit" for the library to consider it too slow
---    and abort.
---@field max_header_name_length? integer The maximum length of a header
---    name. If a header name length exceeds this value, it is truncated to
---    this length. The default value is 32.
---@field follow_location? boolean Whether the client will follow the
---    'Location' header that a server sends as part of a 3xx response. The
---    default is true.
---@field no_proxy? string A comma-separated list of hosts that do not
---    require proxies, or '*', or ''.
---@field proxy? string A proxy server host or IP address, or ''.
---@field proxy_port? integer A proxy server port. The default is 443 for
---    an HTTPS proxy and 1080 for a non-HTTPS proxy.
---@field proxy_user_pwd? string A proxy server username and password.
---@field ssl_cert? string The path to an SSL client certificate file.
---@field ssl_key? string The path to a private key file for a TLS and SSL
---    client certificate.
---@field timeout? integer The number of seconds to wait for a curl API read
---    request before timing out. The default is 36586400100.
---@field unix_socket? string A socket name to use instead of an Internet
---    address for a local connection.
---@field verbose? boolean Turn on/off a verbose mode.
---@field verify_host? boolean Enable verification of the certificate's name
---    (CN) against the specified host.
---@field verify_peer? boolean Set on/off verification of the peer's SSL
---    certificate.
---@field accept_encoding? string Enables automatic decompression of HTTP
---    responses by setting the contents of the `Accept-Encoding` header sent
---    in an HTTP request and enabling decoding of a response when the
---    `Content-Encoding` header is received.
---@field http_version? string Describes an HTTP version.
---@field interface? string A source interface for outgoing traffic.

---@class http.response
---A response object returned by a request method (`request`, `get`, `post`,
---and so on).
---
---@field status integer A response status code.
---@field reason string A response status text.
---@field headers table<string, string|string[]> Response headers.
---@field cookies? table<string, [string, string[]]> Response cookies, where
---    the first element is the cookie value and the second one is an array
---    with the cookie's options.
---@field body? string A response body.
---@field proto? table The HTTP protocol version, `{1: major, 2: minor}`.
---@field url string The request URL.
---@field decode fun(self: http.response): any Decode the response body to a
---    Lua object based on the content type.

---@class http.io
---An IO object used to read or write data in chunks. To get an IO object
---instead of the full response, set the `chunked` request option to true.
---
---@field read fun(self: http.io, opts: integer|string|
---    {chunk?: integer, delimiter?: string|string[]},
---    timeout?: integer): string?
---
---Write the specified chunk of data. `timeout` is the number of seconds
---to wait; the default is 10.
---@field write fun(self: http.io, data: string, timeout?: integer): integer
---
---Finish reading or writing data. `timeout` is the number of seconds to
---wait; the default is 10.
---@field finish fun(self: http.io, timeout?: integer)
---@field status? integer A response status code, set after `finish()`.
---@field reason? string A response status text, set after `finish()`.

---@class http.client.stat
---Statistics of the HTTP client.
---
---@field active_requests integer The number of currently executing requests.
---@field sockets_added integer The total number of sockets added into an
---    event loop.
---@field sockets_deleted integer The total number of sockets deleted from an
---    event loop.
---@field total_requests integer The total number of requests.
---@field http_200_responses integer The total number of requests that
---    returned HTTP 200 OK responses.
---@field http_other_responses integer The total number of requests that
---    returned non-200 OK responses.
---@field failed_requests integer The total number of failed requests,
---    including system, curl, and HTTP errors.

local driver = require('http.client.lib')

local ffi = require('ffi')
local buffer = require('buffer')
local fiber = require('fiber')
local json = require('json')
local yaml = require('yaml')
local msgpack = require('msgpack')

local curl_mt

local IO_TIMEOUT_DEFAULT = 10
local IO_CHUNK_DEFAULT = 2147483647

local default_content_type = 'application/json'

local encoders = {
    ['application/json'] = function(body, _content_type) return json.encode(body) end,
    ['application/yaml'] = function(body, _content_type) return yaml.encode(body) end,
    ['application/msgpack'] = function(body, _content_type) return msgpack.encode(body) end,
}

local decoders = {
    ['application/json'] = function(body, _content_type) return json.decode(body) end,
    ['application/yaml'] = function(body, _content_type) return yaml.decode(body) end,
    ['application/msgpack'] = function(body, _content_type) return msgpack.decode(body) end,
}

-- Extract all fields from a table except ones that start from
-- the underscore.
-- Copy-pasted from src/lua/init.lua.
--
-- Useful for __serialize.
local function filter_out_private_fields(t)
    local res = {}
    for k, v in pairs(t) do
        if not k:startswith('_') then
            res[k] = v
        end
    end
    return res
end

---Create an HTTP client instance.
---
---@param opts? http.client.options configuration options of the client.
---@return http.client_object client a new HTTP client instance.
local http_new = function(opts)

    opts = opts or {}

    opts.max_connections = opts.max_connections or -1
    opts.max_total_connections = opts.max_total_connections or 0

    local curl = driver.new(opts.max_connections, opts.max_total_connections)
    return setmetatable({
        curl = curl,
        encoders = table.copy(encoders),
        decoders = table.copy(decoders),
    }, curl_mt)
end

local check_args_fmt = 'Use client:%s(...) instead of client.%s(...):'

local function check_args(self, method)
    if type(self) ~= 'table' then
        error(check_args_fmt:format(method, method), 2)
    end
end

--
-- RFC2616: http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2
--
-- Multiple message-header fields with the same field-name MAY be present
-- in a message if and only if the entire field-value for that header field
-- is defined as a comma-separated list [i.e., #(values)]. It MUST be possible
-- to combine the multiple header fields into one "field-name: field-value"
-- pair, without changing the semantics of the message, by appending each
-- subsequent field-value to the first, each separated by a comma. The order
-- in which header fields with the same field-name are received is therefore
-- significant to the interpretation of the combined field value, and thus
-- a proxy MUST NOT change the order of these field values when a message
-- is forwarded.
--
-- Tarantool implementation concatenates all headers by default except
-- the blacklist below.
--

local special_headers = {
    ["age"] = true,
    ["authorization"] = true,
    ["content-length"] = true,
    ["content-type"] = true,
    ["etag"] = true,
    ["expires"] = true,
    ["from"] = true,
    ["host"] = true,
    ["if-modified-since"] = true,
    ["if-unmodified-since"] = true,
    ["last-modified"] = true,
    ["location"] = true,
    ["max-forwards"] = true,
    ["proxy-authorization"] = true,
    ["referer"] = true,
    ["retry-after"] = true,
    ["user-agent"] = true,
}

local special_characters = {
    ['('] = true,
    [')'] = true,
    ['['] = true,
    [']'] = true,
    ['<'] = true,
    ['>'] = true,
    ['@'] = true,
    [','] = true,
    [';'] = true,
    [':'] = true,
    ['\\'] = true,
    ['\"'] = true,
    ['/'] = true,
    ['?'] = true,
    ['='] = true,
    ['{'] = true,
    ['}'] = true,
    [' '] = true,
    ['\t'] = true
}

local option_keys = {
    ["Expires"] = true,
    ["Max-Age"] = true,
    ["Domain"] = true,
    ["Path"] = true,
    ["Secure"] = true,
    ["HttpOnly"] = true,
    ["SameSite"] = true,
}

local function process_cookie(cookie, result)
    local vals = cookie:split(';')
    local val = vals[1]:split('=')
    if #val < 2 then
        return
    end
    val[1] = string.strip(val[1])
    for c in val[1]:gmatch('.') do
        if special_characters[c] then
            return
        end
    end

    local options = {}
    table.remove(vals, 1)
    for _, opt in pairs(vals) do
        local splitted = opt:split('=')
        splitted = string.strip(splitted[1])
        if option_keys[splitted] then
            table.insert(options, string.strip(opt))
        end
    end
    result[val[1]] = {string.strip(val[2]), options}
end

local function process_cookies(cookies)
    local result = {}
    for _, val in pairs(cookies) do
        process_cookie(val, result)
    end
    return result
end

local function process_headers(headers)
    for header, value in pairs(headers) do
        if type(value) == 'table' then
            if special_headers[header] then
                headers[header] = value[1]
            else
                headers[header] = table.concat(value, ',')
            end
        end
    end
    return headers
end

-- In RFC 1521, the Content-Type entity-header field indicates the media type
-- of the Entity-Body:
--
--   Content-Type   = "Content-Type" ":" media-type
--
-- Where media-type is defined as follows:
--
--   media-type     = type "/" subtype *( ";" parameter )
--
-- See RFC 1521, https://www.w3.org/Protocols/HTTP/1.0/spec.html#Media-Types
-- Function splits header with content type for part with type, part with
-- subtype and part with parameters and returns a string "type/subtype".
---
---@param content_type? string a Content-Type header value.
---@return string? mime_type a "type/subtype" part, or nil.
local function extract_mime_type(content_type)
    if content_type == nil then
        return nil
    end
    if type(content_type) ~= "string" then
        error('content type must be a string')
    end
    return content_type:split(';', 1)[1]:strip()
end

local function string_lower(str)
    if type(str) == 'string' then
        str = str:lower()
    end
    return str
end

-- Get a value from map by case-insensitive key.
-- Returns the value in the lower case on the first match.
---
---@param t table a map to look up the key in.
---@param key string a key to look up.
---@return string? value the value in the lower case on the first match, or
---    nil.
local function get_icase(t, key)
    key = string_lower(key)
    for k, v in pairs(t) do
        k = string_lower(k)
        if k == key then
            v = string_lower(v)
            return v
        end
    end

    return nil
end

---Serialize a request body according to the content type.
---
---@param body any a request body.
---@param content_type string a Content-Type header value.
---@param encoders table<string, fun(body: any, content_type: string): string>
---    a table of encoders.
---@return string raw_body a serialized body.
local function encode_body(body, content_type, encoders)
    local raw_body = ''
    local body_type = type(body)
    local mime_type
    if body == nil then
        raw_body = ''
    elseif body_type == 'cdata' or
           body_type == 'userdata' or
           body_type == 'table' then
        mime_type = extract_mime_type(content_type)
        mime_type = string_lower(mime_type)
        local encoder = encoders[mime_type]
        if encoder == nil then
            local msg = 'Unable to encode body: encode function is not found (%s)'
            error(msg:format(content_type))
        end
        local ok, res = pcall(encoder, body, content_type)
        if not ok then
            error(('Unable to encode body: %s'):format(res))
        end
        raw_body = res
    elseif body_type == 'number' or
           body_type == 'string' or
           body_type == 'boolean' then
        raw_body = tostring(body)
    else
        error(('Unsupported body type: %s'):format(body_type))
    end

    return raw_body
end

local default_decoder = function(raw_body, _content_type)
    return json.decode(raw_body)
end

---Deserialize a response body to a Lua object based on the Content-Type
---header value.
---
---@param response http.response a response object.
---@return any result a decoded body.
local function decode_body(response)
    if response.body == nil then
        error('Unable to decode body: body is empty')
    end
    local headers = response.headers or {}
    local content_type = get_icase(headers, 'content-type')
    local mime_type = extract_mime_type(content_type)
    local decoder = default_decoder
    if mime_type ~= nil then
        mime_type = string_lower(mime_type)
        decoder = response._decoders[mime_type]
    end
    if decoder == nil then
        local msg = 'Unable to decode body: decode function is not found (%s)'
        error(msg:format(content_type))
    end
    if type(decoder) ~= 'function' then
        local msg = 'Unable to decode body: decode function is not a function (%s)'
        error(msg:format(content_type))
    end
    local ok, res = pcall(decoder, response.body, content_type)
    if not ok then
        error(('Unable to decode body: %s'):format(res))
    end

    return res
end

local function encode_url_params(params, http_method)
    assert(http_method ~= nil)
    local uri = require("uri")
    local uri_escape_opts = uri.FORM_URLENCODED
    if http_method == "GET" or
       http_method == "HEAD" or
       http_method == "DELETE" then
        uri_escape_opts = uri.QUERY_PART
    end

    local query_encoder = uri._internal.params
    local ok, res = pcall(query_encoder, params, uri_escape_opts)
    if not ok then
        error(res)
    end
    return res
end

local function check_limit(self, limit)
    if self._internal.rbuf:size() >= limit then
        return limit
    end
    return nil
end

local function check_delimiter(self, limit, eols)
    if limit == 0 then
        return 0
    end
    local rbuf = self._internal.rbuf
    if rbuf:size() == 0 then
        return nil
    end

    local shortest
    for _, eol in ipairs(eols) do
        local data = ffi.C.memmem(rbuf.rpos, rbuf:size(), eol, #eol)
        if data ~= nil then
            local len = ffi.cast('char *', data) - rbuf.rpos + #eol
            if shortest == nil or shortest > len then
                shortest = len
            end
        end
    end
    if shortest ~= nil and shortest <= limit then
        return shortest
    elseif limit <= rbuf:size() then
        return limit
    end
    return nil
end

---Read request data in chunks of a specified length or up to a specific
---delimiter.
---
---@overload fun(self: http.io, chunk: integer, timeout?: integer): string?
---@overload fun(self: http.io, delimiter: string, timeout?: integer): string?
---@overload fun(self: http.io, opts: {chunk?: integer,
---    delimiter?: string|string[]}, timeout?: integer): string?
---@param self http.io
---@param opts integer|string|{chunk?: integer, delimiter?: string|string[]}
---    the maximum number of bytes to read, the delimiter used to stop
---    reading, or a table with `chunk` and `delimiter` fields.
---@param timeout? integer the number of seconds to wait. The default is 10.
---@return string? data a chunk of read data, an empty string if there is
---    nothing more to read, or nil on timeout.
local function io_read(self, opts, timeout)
    timeout = timeout or IO_TIMEOUT_DEFAULT
    local chunk
    local delimiter
    local check
    if type(opts) == 'number' then
        chunk = opts
        check = check_limit
    elseif type(opts) == 'string' then
        chunk = IO_CHUNK_DEFAULT
        delimiter = {opts}
        check = check_delimiter
    elseif type(opts) == 'table' then
        chunk = opts.chunk or IO_CHUNK_DEFAULT
        delimiter = opts.delimiter
        if delimiter == nil then
            check = check_limit
        elseif type(delimiter) == 'string' then
            delimiter = {delimiter}
            check = check_delimiter
        elseif type(delimiter) == 'table' then
            check = check_delimiter
        end
    else
        error('Usage: io:read(delimiter|chunk|{delimiter = x, chunk = x}, timeout)')
    end

    if chunk < 0 then
        error('io:read(): chunk size can not be negative')
    end

    chunk = math.min(chunk, IO_CHUNK_DEFAULT)
    local rbuf = self._internal.rbuf
    if rbuf == nil then
        rbuf = buffer.ibuf()
        self._internal.rbuf = rbuf
    end

    local len = check(self, chunk, delimiter)
    if len ~= nil then
        local data = ffi.string(rbuf.rpos, len)
        rbuf:consume(len)
        return data
    end

    local deadline = fiber.clock() + timeout
    repeat
        local to_read = math.min(chunk - rbuf:size(), buffer.READAHEAD)
        local data = rbuf:reserve(to_read)

        local res = self._internal.io:read(data, rbuf:unused(), timeout)
        if res == 0 then -- eof
            self._errno = nil
            local len = rbuf:size()
            local data = ffi.string(rbuf.rpos, len)
            rbuf:consume(len)
            return data
        else
            rbuf:alloc(res)
            local len = check(self, chunk, delimiter)
            if len ~= nil then
                self._errno = nil
                local data = ffi.string(rbuf.rpos, len)
                rbuf:consume(len)
                return data
            end
        end
        timeout = deadline - fiber.clock()
    until timeout < 0
    return nil
end

---Write a chunk of data.
---
---@param self http.io
---@param data string data to be written.
---@param timeout? integer the number of seconds to wait. The default is 10.
local function io_write(self, data, timeout)
    timeout = timeout or IO_TIMEOUT_DEFAULT
    return self._internal.io:write(data, #data, timeout)
end

---Finish reading or writing data.
---
---@param self http.io
---@param timeout? integer the number of seconds to wait. The default is 10.
local function io_finish(self, timeout)
    timeout = timeout or IO_TIMEOUT_DEFAULT
    self["status"], self["reason"] = self._internal.io:finish(timeout)
end

curl_mt = {
    __index = {
        ---Make an HTTP request and receive a response.
        ---
        ---The response object is returned if the `chunked` option is false
        ---(the default), otherwise an IO object is returned for streaming
        ---download/upload.
        ---
        ---@param self http.client_object
        ---@param method string an HTTP method, possible values: 'GET',
        ---    'POST', 'PUT', 'PATCH', 'OPTIONS', 'HEAD', 'DELETE', 'TRACE',
        ---    'CONNECT'.
        ---@param url string a request URL, for example,
        ---    'https://httpbin.org/get'.
        ---@param body? any a request body.
        ---@param opts? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `opts.chunked` is set to true.
        request = function(self, method, url, body, opts)
            if not method or not url then
                error('request(method, url[, body, [options]])')
            end
            method = method:upper()

            opts = opts or {}
            opts.headers = opts.headers or {}

            local encoded_params
            if opts.params then
                encoded_params = encode_url_params(opts.params, method)
            end
            local url_with_params = url
            if encoded_params then
                if method == "GET" or
                   method == "HEAD" or
                   method == "DELETE" then
                    url_with_params = ("%s?%s"):format(url, encoded_params)
                elseif body then
                    error('use either body or http params')
                else
                    body = encoded_params
                end
            end

            if method == 'PATCH' or
               method == 'POST' or
               method == 'PUT' then
                local content_type = get_icase(opts.headers, 'content-type')
                if content_type == nil then
                    content_type = default_content_type
                    local body_type = type(body)
                    if body_type == 'cdata' or
                       body_type == 'userdata' or
                       body_type == 'table' then
                        opts.headers['content-type'] = default_content_type
                    end
                end
                body = encode_body(body, content_type, self.encoders)
            end
            local resp = self.curl:request(method, url_with_params, body, opts or {})

            if resp and resp.headers then
                if resp.headers['set-cookie'] ~= nil then
                    resp.cookies = process_cookies(resp.headers['set-cookie'])
                end
                resp.headers = process_headers(resp.headers)
            end

            if not opts.chunked then
                resp.url = url_with_params
                resp._decoders = self.decoders
                return setmetatable(resp, {
                    __serialize = filter_out_private_fields,
                    __index = {
                        decode = decode_body,
                    },
                })
            else
                return setmetatable(resp, {
                    __index = {
                        read = io_read,
                        write = io_write,
                        finish = io_finish,
                    },
                    _internal = {
                        httpc = self,
                    },
                })
            end
        end,

        ---Shortcut for `request('GET', url, nil, options)`.
        ---
        ---@param self http.client_object
        ---@param url string a request URL.
        ---@param options? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `options.chunked` is set to true.
        get = function(self, url, options)
            check_args(self, 'get')
            return self:request('GET', url, nil, options)
        end,

        ---Shortcut for `request('POST', url, body, options)`.
        ---
        ---@param self http.client_object
        ---@param url string a request URL.
        ---@param body? any a request body.
        ---@param options? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `options.chunked` is set to true.
        post = function(self, url, body, options)
            check_args(self, 'post')
            return self:request('POST', url, body, options)
        end,

        ---Shortcut for `request('PUT', url, body, options)`.
        ---
        ---@param self http.client_object
        ---@param url string a request URL.
        ---@param body? any a request body.
        ---@param options? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `options.chunked` is set to true.
        put = function(self, url, body, options)
            check_args(self, 'put')
            return self:request('PUT', url, body, options)
        end,

        ---Shortcut for `request('PATCH', url, body, options)`.
        ---
        ---@param self http.client_object
        ---@param url string a request URL.
        ---@param body? any a request body.
        ---@param options? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `options.chunked` is set to true.
        patch = function(self, url, body, options)
            check_args(self, 'patch')
            return self:request('PATCH', url, body, options)
        end,

        ---Shortcut for `request('OPTIONS', url, nil, options)`.
        ---
        ---@param self http.client_object
        ---@param url string a request URL.
        ---@param options? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `options.chunked` is set to true.
        options = function(self, url, options)
            check_args(self, 'options')
            return self:request('OPTIONS', url, nil, options)
        end,

        ---Shortcut for `request('HEAD', url, nil, options)`.
        ---
        ---@param self http.client_object
        ---@param url string a request URL.
        ---@param options? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `options.chunked` is set to true.
        head = function(self, url, options)
            check_args(self, 'head')
            return self:request('HEAD', url, nil, options)
        end,

        ---Shortcut for `request('DELETE', url, nil, options)`.
        ---
        ---@param self http.client_object
        ---@param url string a request URL.
        ---@param options? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `options.chunked` is set to true.
        delete = function(self, url, options)
            check_args(self, 'delete')
            return self:request('DELETE', url, nil, options)
        end,

        ---Shortcut for `request('TRACE', url, nil, options)`.
        ---
        ---@param self http.client_object
        ---@param url string a request URL.
        ---@param options? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `options.chunked` is set to true.
        trace = function(self, url, options)
            check_args(self, 'trace')
            return self:request('TRACE', url, nil, options)
        end,

        ---Shortcut for `request('CONNECT', url, nil, options)`.
        ---
        ---@param self http.client_object
        ---@param url string a request URL.
        ---@param options? http.client.request.options request options.
        ---@return http.client.result result a response object, or an IO
        ---    object if `options.chunked` is set to true.
        connect = function(self, url, options)
            check_args(self, 'connect')
            return self:request('CONNECT', url, nil, options)
        end,

        ---Get a table with statistics for the HTTP client.
        ---
        ---@param self http.client_object
        ---@return http.client.stat stat a table with statistics.
        stat = function(self)
            return self.curl:stat()
        end,

    },
}

--
-- Export
--
local http_default = http_new()
local this_module = {
    new = http_new,
    encoders = table.copy(encoders),
    decoders = table.copy(decoders),
    _internal = {
        default_content_type = default_content_type,
        encode_body = encode_body,
        decode_body = decode_body,
        extract_mime_type = extract_mime_type,
        get_icase = get_icase,
    }
}

---Wrap a method of the default HTTP client instance to call it with the
---dot syntax.
---
---@param fname string a method name of the default client instance.
---@return function wrapped function.
local function http_default_wrap(fname)
    return function(...) return http_default[fname](http_default, ...) end
end

for _, name in ipairs({ 'get', 'delete', 'trace', 'options', 'head',
                     'connect', 'post', 'put', 'patch', 'request'}) do
    this_module[name] = http_default_wrap(name)
end
this_module.curl = http_default.curl

---@type http.client
return this_module
