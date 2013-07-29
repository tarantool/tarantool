remote = box.net.box.new('localhost', box.cfg.primary_port, '0.5');
type(remote);
remote:ping();
remote:ping();
box.net.box.ping(remote);
box.insert(0, 123, 'test1', 'test2');
box.select(0, 0, 123);
tuple = remote:select(0, 0, 123);
remote:call('box.select', '0', '0', 123);
tuple;
type(tuple);
#tuple;

box.update(0, 123, '=p', 1, 'test1-updated');
remote:update(0, 123, '=p', 2, 'test2-updated');

box.insert(0, 123, 'test1', 'test2');
remote:insert(0, 123, 'test1', 'test2');

remote:insert(0, 345, 'test1', 'test2');
remote:select(0, 0, 345);
remote:call('box.select', '0', '0', 345);
box.select(0, 0, 345);

remote:replace(0, 345, 'test1-replaced', 'test2-replaced');
box.select(0, 0, 345);
remote:select_limit(0, 0, 0, 1000, 345);

box.select_range(0, 0, 1000);
remote:select_range(0, 0, 1000);
box.select(0, 0, 345);
remote:select(0, 0, 345);
remote:timeout(0.5):select(0, 0, 345);

remote:call('box.fiber.sleep', '.01');
remote:timeout(0.01):call('box.fiber.sleep', '10');

pstart = box.time();
parallel = {};
function parallel_foo(id)
    box.fiber.sleep(math.random() * .05)
    return id
end;
parallel_foo('abc');
for i = 1, 20 do
    box.fiber.resume(
        box.fiber.create(
            function()
                box.fiber.detach()
                local s = string.format('%07d', i)
                local so = remote:call('parallel_foo', s)
                table.insert(parallel, tostring(s == so[0]))
            end
        )
    )
end;
for i = 1, 20 do
    if #parallel == 20 then
        break
    end
    box.fiber.sleep(0.1)
end;
unpack(parallel);
#parallel;
box.time() - pstart < 0.5;

remote:close();
remote:close();
remote:ping();

box.space[0]:truncate();
-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
