ch = box.ipc.channel()
ch:is_full()
ch:is_empty()
ch:get(.1)
ch:put()
ch:put('test')
ch:get()
ch:get('wrong timeout')
ch:get(-10)
ch:put(234)
ch:put(345, .5)
ch:is_full()
ch:is_empty()
buffer = {}
--# setopt delimiter ';'
tfbr = box.fiber.create(
    function()
        box.fiber.detach()
        while true do
            table.insert(buffer, ch:get())
        end
    end
);
box.fiber.resume(tfbr);
t = {};
for i = 1, 10 do
    table.insert(t, {i, ch:put(i, 0.1)})
end;
--# setopt delimiter ''
t
ch:has_readers()
ch:has_writers()
box.fiber.cancel(tfbr)

ch:has_readers()
ch:has_writers()
ch:put(box.info.pid)
ch:is_full()
ch:is_empty()
ch:get(box.info.pid) == box.info.pid
buffer

ch:is_empty()
ch:broadcast()
ch:broadcast(123)
ch:get()

ch:is_full()
ch:is_empty()
--# setopt delimiter ';'
tfbr = box.fiber.create(
    function()
        box.fiber.detach()
        while true do
            local v = ch:get()
            table.insert(buffer, {'tfbr', tostring(v)})
        end
    end
);
tfbr2 = box.fiber.create(
    function()
        box.fiber.detach()
        while true do
            local v = ch:get()
            table.insert(buffer, {'tfbr2', tostring(v)})
        end
    end
);
--# setopt delimiter ''
box.fiber.resume(tfbr)
box.fiber.resume(tfbr2)

buffer = {}

buffer
ch:is_full()
ch:is_empty()
ch:put(1)
ch:put(2)
ch:put(3)
ch:put(4)
ch:put(5)
ch:broadcast('broadcast message!')
t = {}
for i = 35, 45 do table.insert(t, ch:put(i)) end
t
buffer

ch = box.ipc.channel(1)
ch:is_closed()
passed = false
type(box.fiber.wrap(function() if ch:get() == nil then passed = true end end))
ch:close()
passed
ch:get()
ch:get()
ch:put(10)
ch:is_closed()

ch = box.ipc.channel(1)
ch:put(true)
ch:is_closed()
passed = false
type(box.fiber.wrap(function() if ch:put(true) == false then passed = true end end))
ch:close()
passed
ch:get()
ch:get()
ch:put(10)
ch:is_closed()
