space = box.schema.create_space('tweedledum')
space:create_index('primary', 'hash', { parts = { 0, 'num' }})
-- A test case for a race condition between ev_schedule
-- and wal_schedule fiber schedulers.
-- The same fiber should not be scheduled by ev_schedule (e.g.
-- due to cancellation) if it is within th wal_schedule queue.
-- The test case is dependent on rows_per_wal, since this is when
-- we reopen the .xlog file and thus wal_scheduler takes a long
-- pause
box.cfg.rows_per_wal
space:insert(1, 'testing', 'lua rocks')
space:delete(1)
space:insert(1, 'testing', 'lua rocks')
space:delete(1)
-- check delete
box.process(box.net.box.SELECT, box.pack('iiiip', space.n, 0, 0, 2^31, {1}))
box.process(box.net.box.CALL, box.pack('iii', space.n, 0, 0))

space:insert(1, 'test box delete')
space:delete(1)
space:insert(1, 'test box delete')
space:delete(1)
space:insert(1684234849, 'test box delete')
space:delete(1684234849)
space:insert(1684234849, 'test box delete')
space:delete(1684234849)
space:insert(1684234849, 'test box.select()')
space:replace(1684234849, 'hello', 'world')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1667655012, 'goodbye', 'universe')
space:replace(1684234849)
space:delete(1684234849)
space:delete(1667655012)
space:insert(1953719668, 'old', 1684234849)
-- test that insert produces a duplicate key error
space:insert(1953719668, 'old', 1684234849)
space:update(1953719668, '=p=p', 0, 1936941424, 1, 'new')
space:update(1234567890, '+p', 2, 1)
space:update(1936941424, '+p', 2, 1)
space:update(1936941424, '-p', 2, 1)
space:update(1936941424, '-p', 2, 1)
space:update(1936941424, '+p', 2, 1)
space:delete(1936941424)
box.cfg.reload()
-- must be read-only

space:insert(1953719668)
space:insert(1684234849)
space:delete(1953719668)
space:delete(1684234849)
space:insert(1953719668, 'hello world')
space:update(1953719668, '=p', 1, 'bye, world')
space:delete(1953719668)
-- test tuple iterators
t = space:insert(1953719668)
t = space:replace(1953719668, 'another field')
t = space:replace(1953719668, 'another field', 'one more')
space:truncate()
-- test passing arguments in and out created fiber

--# setopt delimiter ';'
function y()
    box.fiber.detach('started')
    space = box.space['tweedledum']
    while true do
        space:replace(1953719668, os.time())
        box.fiber.sleep(0.001)
    end
end;
f = box.fiber.create(y);
box.fiber.resume(f);
box.fiber.sleep(0.002);
box.fiber.cancel(f);
box.fiber.resume(f);
for k = 1, 1000, 1 do
    box.fiber.create(
        function()
            box.fiber.detach()
        end
    )
end;
--# setopt delimiter ''

collectgarbage('collect')
-- check that these newly created fibers are garbage collected
box.fiber.find(900)
box.fiber.find(910)
box.fiber.find(920)
space:drop()
box.fiber.find()
box.fiber.find('test')
--  https://github.com/tarantool/tarantool/issues/131
--  box.fiber.resume(box.fiber.cancel()) -- hang
f = box.fiber.create(function() box.fiber.cancel(box.fiber.self()) end)
box.fiber.resume(f)
f = nil
-- https://github.com/tarantool/tarantool/issues/119
ftest = function() box.fiber.sleep(0.01 * math.random() ) return true end

--# setopt delimiter ';'
for i = 1, 10 do
    result = {}
    for j = 1, 300 do
        box.fiber.resume(box.fiber.create(function()
            box.fiber.detach()
            table.insert(result, ftest())
        end))
    end
    while #result < 300 do box.fiber.sleep(0.01) end
end;
--# setopt delimiter ''
#result

--# setopt delimiter ''
