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
box.process(17, box.pack('iiiiiip', space.n, 0, 0, 2^31, 1, 1, 1))
box.process(22, box.pack('iii', space.n, 0, 0))

space:insert(1, 'test box delete')
space:delete('\1\0\0\0')
space:insert(1, 'test box delete')
space:delete(1)
space:insert('abcd', 'test box delete')
space:delete('abcd')
space:insert('abcd', 'test box delete')
space:delete('abcd')
space:insert('abcd', 'test box.select()')
space:replace('abcd', 'hello', 'world')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('defc', 'goodbye', 'universe')
space:replace('abcd')
space:delete('abcd')
space:delete('defc')
space:insert('test', 'old', 'abcd')
-- test that insert produces a duplicate key error
space:insert('test', 'old', 'abcd')
space:update('test', '=p=p', 0, 'pass', 1, 'new')
space:update('miss', '+p', 2, '\1\0\0\0')
space:update('pass', '+p', 2, '\1\0\0\0')
space:update('pass', '-p', 2, '\1\0\0\0')
space:update('pass', '-p', 2, '\1\0\0\0')
space:update('pass', '+p', 2, 1)
space:delete('pass')
box.cfg.reload()
-- must be read-only

space:insert('test')
space:insert('abcd')
space:delete('test')
space:delete('abcd')
space:insert('test', 'hello world')
space:update('test', '=p', 1, 'bye, world')
space:delete('test')
-- test tuple iterators
t = space:insert('test')
t = space:replace('test', 'another field')
t = space:replace('test', 'another field', 'one more')
space:truncate()
-- test passing arguments in and out created fiber

--# setopt delimiter ';'
function y()
    box.fiber.detach('started')
    space = box.space['tweedledum']
    while true do
        space:replace('test', os.time())
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
