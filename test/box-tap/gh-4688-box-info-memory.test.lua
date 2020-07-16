#!/usr/bin/env tarantool

--
-- gh-4688: box.info:memory() displayed full content of box.info
--
local tap = require('tap')

box.cfg()

local test = tap.test('gh-4688-box.info:memory-wrong-result')
test:plan(1)

local function get_keys(t)
    local keys = {}
    for k, _ in pairs(t) do
        table.insert(keys, k)
    end
    return keys
end

local keys_1 = get_keys(box.info.memory())
local keys_2 = get_keys(box.info:memory())
test:is_deeply(keys_1, keys_2, "box.info:memory coincide with box.info.memory")

os.exit(test:check() and 0 or 1)
