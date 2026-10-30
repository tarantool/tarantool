env = require('test_run')
test_run = env.new()
fiber = require('fiber')

-- Test consists of two reproducers from a corresponding issue.

s = box.schema.space.create('test', {engine = 'memtx'})
i = s:create_index('pk',  {type='hash', parts={{1, 'uint'}}})

known = {}

-- Get seed randomly and remember it to reproduce easily.
math.randomseed(os.time())
seed = math.random(10000)
math.randomseed(seed)

test_run:cmd("setopt delimiter ';'");

function replace()
    local r = 42
    while known[r] do r = math.random(1000000) end
    known[r] = true
    s:replace{r, r}
end;

-- Insert enough tuples to enable build in background.
for i = 0, 6000 do replace() end;

function joinable(fib)
    fib:set_joinable(true)
    return fib
end;

-- Make some replaces.
function disturb()
    for j = 1,10 do
        box.begin()
        for i = 1,10 do
            replace()
        end
        box.commit()
    end
end;

-- Create secondary index
function create(index_type)
    s:create_index('sk', {type=index_type, parts={{2, 'uint'}}})
end;

-- Call create() and disturb() concurrently and then
-- check consistency of secondary index.
-- Print random seed on fail.
function process_test(index_type)
    local disturber = joinable(fiber.new(disturb))
    local creator = joinable(fiber.new(create, index_type))
    disturber:join()
    creator:join()
    assert(s.index.pk:count() == s.index.sk:count(), 'Test failed with seed = ' .. seed)
end;

test_run:cmd("setopt delimiter ''");

process_test('hash')
s.index.sk:drop()

process_test('tree')
s:drop()
known = nil

s = box.schema.space.create('test', {engine = 'memtx'})
i = s:create_index('pk',  {type='hash', parts={{1, 'uint'}}})

space_size = 6000

test_run:cmd("setopt delimiter ';'");

for i = 0, space_size do
    s:replace{i, i}
end;

for i = 0, 9 do
    s:delete{i}
end;

function disturb()
    fiber.sleep(0)
    for i = 1, 10 do
        s:replace{space_size + i, space_size + i}
    end
end;

function create(index_type)
    s:create_index('sk', {type=index_type, parts={{2, 'uint'}}})
end;

test_run:cmd("setopt delimiter ''");

process_test('hash')
s.index.sk:drop()

process_test('tree')
s:drop()
seed = nil
space_size = nil
