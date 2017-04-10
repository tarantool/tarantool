fiber = require('fiber')
env = require('test_run')
test_run = env.new()

-- channel methods ignore extra arguments, as regular Lua functions do
ignored_args = {'Extra', 'arguments', 'are', 'ignored'}

ch = fiber.channel(1)
ch:size()
ch:count()
ch:is_full()
ch:is_empty()
ch:size(unpack(ignored_args))
ch:count(unpack(ignored_args))
ch:is_full(unpack(ignored_args))
ch:is_empty(unpack(ignored_args))
ch:get(.1)
ch:get(.1, nil)
ch:get(.1, nil, unpack(ignored_args))
tostring(ch)
ch:put()
ch:count()
ch:put('test')
tostring(ch)
ch:get()
ch:put('test', nil), ch:get()
ch:put('test', nil, unpack(ignored_args)), ch:get()
ch:get('wrong timeout')
ch:get(-10)
ch:put(234)
ch:put(345, .1)
ch:count()
ch:is_full()
ch:is_empty()
buffer = {}
test_run:cmd("setopt delimiter ';'")
tfbr = fiber.create(
    function()
        while true do
            table.insert(buffer, ch:get())
        end
    end
);
t = {};
for i = 1, 10 do
    table.insert(t, {i, ch:put(i, 0.1)})
end;
test_run:cmd("setopt delimiter ''");
t
ch:has_readers()
ch:has_writers()
fiber.cancel(tfbr)

ch:has_readers()
ch:has_writers()
ch:count()
ch:put(box.info.pid)
ch:count()
ch:is_full()
ch:is_empty()
ch:get(box.info.pid) == box.info.pid
buffer

ch:is_empty()

ch:is_full()
ch:is_empty()
test_run:cmd("setopt delimiter ';'")
tfbr = fiber.create(
    function()
        while true do
            local v = ch:get()
            table.insert(buffer, v)
        end
    end
);
tfbr2 = fiber.create(
    function()
        while true do
            local v = ch:get()
            table.insert(buffer, v)
        end
    end
);
test_run:cmd("setopt delimiter ''");

buffer = {}

buffer
ch:is_full()
ch:is_empty()
ch:put(1)
ch:put(2)
ch:put(3)
ch:put(4)
ch:put(5)
t = {}
for i = 35, 45 do table.insert(t, ch:put(i)) end
t
while #buffer < 15 do fiber.sleep(0.001) end
table.sort(buffer)
buffer

ch = fiber.channel(1)
ch:is_closed()
passed = false
type(fiber.create(function() if ch:get() == nil then passed = true end end))
ch:close()
fiber.yield()
passed
ch:get()
ch:get()
ch:put(10)
ch:is_closed()
tostring(ch)

ch = fiber.channel(1)
ch:put(true)
ch:is_closed()
passed = false
type(fiber.create(function() if ch:put(true) == false then passed = true end end))
ch:close()
fiber.yield()
passed
ch:get()
ch:get()
ch:put(10)
ch:is_closed()



-- race conditions
chs, test_res, count = {}, {}, 0
test_run:cmd("setopt delimiter ';'")
for i = 1, 10 do table.insert(chs, fiber.channel()) end;

for i = 1, 10 do
    fiber.create(function(no)
        fiber.self():name('pusher')
        while true do
            chs[no]:put({no})
            fiber.sleep(0.001 * math.random())
        end
    end, i)
end;

for i = 1, 10 do
    fiber.create(function(no)
        fiber.self():name('receiver')
        while true do
            local r = chs[no]:get(math.random() * .001)
            if r ~= nil and r[1] == no then
                test_res[no] = true
            elseif r ~= nil then
                break
            end
            fiber.sleep(0.001 * math.random())
            count = count + 1
        end
        test_res[no] = false
    end, i)
end;

for i = 1, 100 do fiber.sleep(0.01) if count > 2000 then break end end;

count > 2000, #test_res, test_res;
test_run:cmd("setopt delimiter ''");

--
-- gh-756: channel:close() leaks memory
--

ffi = require('ffi')
ffi.cdef[[ struct gh756 { int k; }; ]]
ct = ffi.metatype('struct gh756', { __gc = function() refs = refs - 1; end })

-- create 10 objects and put they to a channel
refs = 10
ch = fiber.channel(refs)
for i=1,refs do ch:put(ffi.new(ct, i)) end

-- get an object from the channel, run GC and check the number of objects
ch:get().k == 1
collectgarbage('collect')
refs
ch:get().k == 2
collectgarbage('collect')
refs

-- close the channel and check the number of objects
ch:close()
collectgarbage('collect')
refs -- must be zero

-- fiber.cond
c = fiber.cond()
tostring(c)
-- args validation
c.wait()
c.wait('1')
c:wait('1')
c:wait(-1)
-- timeout
c:wait(0.1)
-- wait success
fiber.create(function() fiber.sleep(.5); c:broadcast() end) and c:wait(.6)
-- signal
t = {}
for i = 1,4 do fiber.create(function() c:wait(); table.insert(t, '#') end) end
c:signal()
fiber.sleep(0.1)
t
-- broadcast
c:broadcast()
fiber.sleep(0.1)
t
