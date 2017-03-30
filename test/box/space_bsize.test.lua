env = require('test_run')
test_run = env.new()

utils = dofile('utils.lua')

s = box.schema.space.create('space_bsize')
idx = s:create_index('primary')

for i = 1, 13 do s:insert{ i, string.rep('x', i) } end

s:bsize()
utils.space_bsize(s)

for i = 1, 13, 2 do s:delete{ i } end

s:bsize()
utils.space_bsize(s)

for i = 2, 13, 2 do s:update( { i }, {{ ":", 2, i, 0, string.rep('y', i) }} ) end

s:bsize()
utils.space_bsize(s)

box.snapshot()

test_run:cmd("restart server default")

utils = dofile('utils.lua')

s = box.space['space_bsize']

s:bsize()
utils.space_bsize(s)

for i = 1, 13, 2 do s:insert{ i, string.rep('y', i) } end

s:bsize()
utils.space_bsize(s)

s:truncate()

s:bsize()
utils.space_bsize(s)

for i = 1, 13 do s:insert{ i, string.rep('x', i) } end

s:bsize()
utils.space_bsize(s)

s:drop()
