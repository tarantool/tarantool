#!/usr/bin/env tarantool

test_run = require('test_run').new()

s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('test')

str = string.rep('!', 100)

for i = 1,1000 do s:insert{i, str} end

t = s:select{}

#t

t = s:replace{100, str}

for i = 1,10 do t = s:select{} end

t = s:replace{200, str}

s:drop()
