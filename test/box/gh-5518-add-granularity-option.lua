#!/usr/bin/env tarantool

local granularity = 8
if arg[1] then
    granularity = tonumber(arg[1])
end

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = os.getenv("LISTEN"),
    granularity = granularity,
})

if box.space.test ~= nil then
    box.space.test:drop()
end
local s = box.schema.space.create('test')
local _ = s:create_index('test')
for i = 1, 1000 do s:insert{i, i + 1} end
