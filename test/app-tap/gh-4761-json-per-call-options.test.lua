#!/usr/bin/env tarantool

local json = require('json')
local tap = require('tap')

--
-- gh-4761: json.decode silently changes instance settings when
-- called with 2nd parameter.
--
-- Verify json.encode as well.
--
local res = tap.test('gh-4761-json-per-call-options', function(test)
    test:plan(2)

    -- Preparation code: call :decode() with a custom option.
    local ok = pcall(json.decode, '{"foo": {"bar": 1}}',
                     {decode_max_depth = 1})
    assert(not ok, 'expect "too many nested data structures" error')

    -- Verify that the instance option remains unchanged.
    local exp_res = {foo = {bar = 1}}
    local ok, res = pcall(json.decode, '{"foo": {"bar": 1}}')
    test:is_deeply({ok, res}, {true, exp_res},
                   'json instance settings remain unchanged after :decode()')

    -- Same check for json.encode.
    local nan = 1/0
    local ok = pcall(json.encode, {a = nan},
                     {encode_invalid_numbers = false})
    assert(not ok, 'expected "number must not be NaN or Inf" error')
    local exp_res = '{"a":inf}'
    local ok, res = pcall(json.encode, {a = nan})
    test:is_deeply({ok, res}, {true, exp_res},
                   'json instance settings remain unchanged after :encode()')
end)

os.exit(res and 0 or 1)
