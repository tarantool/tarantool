env = require('test_run')
test_run = env.new()
test_run:cmd("create server bg_index_mem_leak with script='box/alter-primary-index-tuple-leak-long.lua'")
test_run:cmd("start server bg_index_mem_leak")
test_run:cmd("switch bg_index_mem_leak")

fiber = require('fiber')

very_big_str = "VERY BIG STRING" .. string.rep('b', 105)
str78 = string.rep('a', 78)
str38 = string.rep('a', 38)

-- Create a huge table to occupy a lot of memory.
h = box.schema.space.create('heavy', {engine = 'memtx'})
_ = h:create_index('pk')
for i = 1, 150000 do h:replace{i, str78} end

-- Create table with enough tuples to make primary index alter in background.
s = box.schema.space.create('test', {engine = 'memtx'})
_ = s:create_index('pk',  {type='tree', parts={{1, 'uint'}}})
for i = 0, 7000 do s:replace{i, i, str38} end

started = false
a = 0
fno = 1

test_run:cmd("setopt delimiter ';'")

function joinable(fib)
	fib:set_joinable(true)
	return fib
end;

-- Wait for fiber yield during altering of primary index and replace a tuple.
function disturb()
	while not started do fiber.sleep(0) end
	s:replace{0, 0, a, very_big_str}
end;

-- Alter primary index.
-- If the index will not be altered in background, test will not be passed
-- because it will run out of time (disturber:join() will wait forever).
function create()
    started = true
    s.index.pk:alter({parts = {{field = fno, type = 'unsigned'}}})
	started = false
end;

-- Make a lot of replaces of the same tuple to check if there is a memory leak
for i = 1, 1000 do
	fno = fno % 2 + 1
	disturber = joinable(fiber.new(disturb))
	creator = joinable(fiber.new(create))
	a = i
	disturber:join()
	creator:join()
	collectgarbage("collect")
end;

-- If everything is OK, tuple will be replaced.
_ = s:replace{0, 0, a, very_big_str};

-- Wait for fiber yield during altering of primary index and replace a tuple and then rollback the change.
function disturb()
	while not started do fiber.sleep(0) end
	box.begin()
	s:replace{0, 0, a, very_big_str}
	box.rollback()
end;

-- Make a lot of rolled back replaces of the same tuple to check if there is a memory leak
for i = 1, 1000 do
	fno = fno % 2 + 1
	disturber = joinable(fiber.new(disturb))
	creator = joinable(fiber.new(create))
	a = i
	disturber:join()
	creator:join()
	collectgarbage("collect")
end;

-- If everything is OK, tuple will be replaced.
_ = s:replace{0, 0, a, very_big_str};

test_run:cmd("setopt delimiter ''");
s:drop()
h:drop()

test_run:cmd("switch default")
test_run:cmd("stop server bg_index_mem_leak")
test_run:cmd("cleanup server bg_index_mem_leak")
