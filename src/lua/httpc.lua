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

local driver = require('http.client')

local json = require('json')
local yaml = require('yaml')
local msgpack = require('msgpack')

local curl_mt

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

--
--  <http> - create a new curl instance.
--
--  Parameters:
--
--  max_connections -  Maximum number of entries in the connection cache
--  max_total_connections -  Maximum number of active connections
--
--  Returns:
--  curl object or raise error()
--

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

local function encode_body(body, content_type, encoders)
    local raw_body
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
        decoder = response.decoders[mime_type]
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

--
--  <request> This function does HTTP request
--
--  Parameters:
--
--  method  - HTTP method, like GET, POST, PUT and so on
--  url     - HTTP url, like https://tarantool.org/doc
--  body    - this parameter is optional, you may use it for passing
--            data to a server. Like 'My text string!'
--  options - this is a table of options.
--
--      ca_path - a path to ssl certificate dir;
--
--      ca_file - a path to ssl certificate file;
--
--      unix_socket - a path to Unix domain socket;
--
--      verify_host - set on/off verification of the certificate's name (CN)
--          against host;
--
--      verify_peer - set on/off verification of the peer's SSL certificate;
--
--      ssl_key - set path to the file with private key for TLS and SSL client
--          certificate;
--
--      ssl_cert - set path to the file with SSL client certificate;
--
--      proxy - set a proxy to use;
--
--      proxy_port - set a port number the proxy listens on;
--
--      proxy_user_pwd - set a user name and a password to use in authentication;
--
--      no_proxy - disable proxy use for specific hosts;
--
--      headers - a table of HTTP headers;
--
--      keepalive_idle & keepalive_interval -
--          non-universal keepalive knobs (Linux, AIX, HP-UX, more);
--
--      low_speed_time & low_speed_limit -
--          If the download receives less than "low speed limit" bytes/second
--          during "low speed time" seconds, the operations is aborted.
--          You could i.e if you have a pretty high speed connection, abort if
--          it is less than 2000 bytes/sec during 20 seconds;
--
--      timeout - time-out the read operation and
--          waiting for the curl api request after this amount of seconds;
--
--      max_header_name_length - maximum length of a response header;
--
--      verbose - set on/off verbose mode;
--
--      interface - source interface for outgoing traffic;
--
--      follow_location - whether the client will follow 'Location' header that
--          a server sends as part of an 3xx response;
--
--      accept_encoding - enables automatic decompression of HTTP responses;
--
--      params - a table with query parameters;
--
--  Returns:
--      {
--          status=NUMBER,
--          reason=ERRMSG
--          body=STRING,
--          headers=STRING,
--          errmsg=STRING
--      }
--
--  Raises error() on invalid arguments and OOM
--

curl_mt = {
    __index = {
        --
        --  <request> see above <request>
        --
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

            resp.url = url_with_params

            return setmetatable(resp, {
                __index = {
                    decode = decode_body,
                },
            })
        end,

        --
        -- <get> - see <request>
        --
        get = function(self, url, options)
            check_args(self, 'get')
            return self:request('GET', url, nil, options)
        end,

        --
        -- <post> - see <request>
        --
        post = function(self, url, body, options)
            check_args(self, 'post')
            return self:request('POST', url, body, options)
        end,

        --
        -- <put> - see <request>
        --
        put = function(self, url, body, options)
            check_args(self, 'put')
            return self:request('PUT', url, body, options)
        end,

        --
        -- <patch> - see <request>
        --
        patch = function(self, url, body, options)
            check_args(self, 'patch')
            return self:request('PATCH', url, body, options)
        end,

        --
        -- <options> see <request>
        --
        options = function(self, url, options)
            check_args(self, 'options')
            return self:request('OPTIONS', url, nil, options)
        end,

        --
        -- <head> see <request>
        --
        head = function(self, url, options)
            check_args(self, 'head')
            return self:request('HEAD', url, nil, options)
        end,
        --
        -- <delete> see <request>
        --
        delete = function(self, url, options)
            check_args(self, 'delete')
            return self:request('DELETE', url, nil, options)
        end,

        --
        -- <trace> see <request>
        --
        trace = function(self, url, options)
            check_args(self, 'trace')
            return self:request('TRACE', url, nil, options)
        end,

        --
        -- <connect> see <request>
        --
        connect = function(self, url, options)
            check_args(self, 'connect')
            return self:request('CONNECT', url, nil, options)
        end,

        --
        -- <stat> - this function returns a table with many values of statistic.
        --
        -- Returns {
        --
        --  active_requests - this is number of currently executing requests
        --
        --  sockets_added -
        --  this is a total number of added sockets into libev loop
        --
        --  sockets_deleted -
        --  this is a total number of deleted sockets from libev
        --                      loop
        --
        --  total_requests - this is a total number of requests
        --
        --  http_200_responses -
        --  this is a total number of requests which have
        --              returned a code HTTP 200
        --
        --  http_other_responses -
        --      this is a total number of requests which have
        --      requests not a HTTP 200
        --
        --  failed_requests - this is a total number of requests which have
        --      failed (included systeme erros, curl errors, HTTP
        --      errors and so on)
        --  }
        --  or error()
        --
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

local function http_default_wrap(fname)
    return function(...) return http_default[fname](http_default, ...) end
end

for _, name in ipairs({ 'get', 'delete', 'trace', 'options', 'head',
                     'connect', 'post', 'put', 'patch', 'request'}) do
    this_module[name] = http_default_wrap(name)
end

return this_module
