#!/usr/bin/env tarantool

local swim = require('swim')
listen_uri = tostring(os.getenv("LISTEN"))
listen_port = require('uri').parse(listen_uri).service

box.cfg{}

--
-- SWIM instances, using broadcast, should protect themselves
-- with encryption. Otherwise they can accidentally discover
-- SWIM instances from other tests.
--
local enc_key = box.info.uuid
local enc_algo = 'aes128'

--
-- Wrap swim.new with a codec to prevent test workers affecting
-- each other.
--
local original_new = swim.new
swim.new = function(...)
    local s, err = original_new(...)
    if s == nil then
        return s, err
    end
    assert(s:set_codec({algo = enc_algo, key = enc_key, key_size = 16}))
    return s
end

function uuid(i)
    local min_valid_prefix = '00000000-0000-1000-8000-'
    if i < 10 then
        return min_valid_prefix..'00000000000'..tostring(i)
    end
    assert(i < 100)
    return min_valid_prefix..'0000000000'..tostring(i)
end

function uri(port)
    port = port or 0
    return '127.0.0.1:'..tostring(port)
end

require('console').listen(os.getenv('ADMIN'))
