-- setopt delim ';'
-- A test case for a race condition between ev_schedule
-- and wal_schedule fiber schedulers.
-- The same fiber should not be scheduled by ev_schedule (e.g.
-- due to cancellation) if it is within th wal_schedule queue.
-- The test case is dependent on rows_per_wal, since this is when
-- we reopen the .xlog file and thus wal_scheduler takes a long
-- pause;
box.cfg.rows_per_wal;
box.space[0]:insert(1, 'testing', 'lua rocks');
box.space[0]:delete(1);
box.space[0]:insert(1, 'testing', 'lua rocks');
box.space[0]:delete(1);
-- check delete;
box.process(17, box.pack('iiiiiip', 0, 0, 0, 2^31, 1, 1, 1));
box.process(22, box.pack('iii', 0, 0, 0));

box.space[0]:insert(1, 'test box delete');
box.delete('0', '\1\0\0\0');
box.space[0]:insert(1, 'test box delete');
box.delete(0, 1);
box.space[0]:insert('abcd', 'test box delete');
box.delete('0', 'abcd');
box.space[0]:insert('abcd', 'test box delete');
box.delete(0, 'abcd');
box.space[0]:insert('abcd', 'test box.select()');
box.replace('0', 'abcd', 'hello', 'world');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'defc', 'goodbye', 'universe');
box.replace('0', 'abcd');
box.delete('0', 'abcd');
box.delete('0', 'defc');
box.insert('0', 'test', 'old', 'abcd');
-- test that insert produces a duplicate key error;
box.insert('0', 'test', 'old', 'abcd');
box.update('0', 'test', '=p=p', 0, 'pass', 1, 'new');
box.update('0', 'miss', '+p', 2, '\1\0\0\0');
box.update('0', 'pass', '+p', 2, '\1\0\0\0');
box.update('0', 'pass', '-p', 2, '\1\0\0\0');
box.update('0', 'pass', '-p', 2, '\1\0\0\0');
box.update(0, 'pass', '+p', 2, 1);
box.delete('0', 'pass');
reload configuration;
-- must be read-only;

box.insert(0, 'test');
box.insert(0, 'abcd');
box.delete(0, 'test');
box.delete(0, 'abcd');
box.space[0]:insert('test', 'hello world');
box.space[0]:update('test', '=p', 1, 'bye, world');
box.space[0]:delete('test');
-- test tuple iterators;
t = box.space[0]:insert('test');
t = box.space[0]:replace('test', 'another field');
t = box.space[0]:replace('test', 'another field', 'one more');
box.space[0]:truncate();
-- test passing arguments in and out created fiber;
function y()
    print('started')
    box.fiber.detach()
    while true do
        box.replace(0, 'test', os.time())
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
collectgarbage('collect');
-- check that these newly created fibers are garbage collected;
box.fiber.find(900);
box.fiber.find(910);
box.fiber.find(920);
box.space[0]:truncate();
-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
