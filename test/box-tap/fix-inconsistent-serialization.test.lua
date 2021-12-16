#!/usr/bin/env tarantool

local msgpack = require('msgpack')
local msgpackffi = require('msgpackffi')
local yaml = require('yaml')
local json = require('json')

local tap = require('tap')
local test = tap.test('fix-inconsistent-serialization')

test:plan(4)

local map = setmetatable({1, 2, 3, 4, 5}, { __serialize = 'map'})
local msgpack_dec = msgpack.decode(msgpack.encode(map))
local msgpackffi_dec = msgpackffi.decode(msgpackffi.encode(map))

test:is(yaml.encode(map), yaml.encode(msgpack_dec), "yaml")
test:is(yaml.encode(map), yaml.encode(msgpackffi_dec), "yaml")
test:is(json.encode(map), json.encode(msgpack_dec), "json")
test:is(json.encode(map), json.encode(msgpackffi_dec), "json")

os.exit(test:check() and 0 or 1)
