#!/usr/bin/env tarantool

swim = require('swim')
fiber = require('fiber')
listen_uri = tostring(os.getenv("LISTEN"))
listen_port = require('uri').parse(listen_uri).service

box.cfg{}

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
