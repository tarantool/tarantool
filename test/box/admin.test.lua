--# stop server default
--# start server default

space = box.schema.create_space('tweedledum', { id = 0 })
space:create_index('primary', 'hash', { parts = { 0, 'num' }})

box.stat()
help()
box.cfg()
box.stat()
box.insert(0, 1, 'tuple')
box.snapshot()
box.delete(0, 1)
--# setopt delimiter ';'

function check_type(arg, typeof)
    if arg == nil then
        return false
    elseif type(arg) == typeof then
        return true
    else
        return false
    end
end;
function test_box_info()
    local tmp = box.info()
    local num = {'pid', 'lsn', 'snapshot_pid', 'recovery_last_update', 'recovery_lag', 'uptime', 'logger_pid'}
    local buildstr = {'flags', 'target', 'compiler', 'options'}
    local str = {'version', 'status', 'config'}
    failed = {}
    for k, v in ipairs(num) do
        if check_type(tmp[v], 'number') == false then
            table.insert(failed, 'box.info().'..v)
        end
    end
    for k, v in ipairs(str) do
        if check_type(tmp[v], 'string') == false then
            table.insert(failed, 'box.info().'..v)
        end
    end
    for k, v in ipairs(buildstr) do
        if check_type(tmp.build[v], 'string') == false then
            table.insert(failed, 'box.info().build.'..v)
        end
    end
    if #failed == 0 then
        return 'box.info() is ok.'
    else
        return 'box.info() is not ok.', 'failed: ', failed
end;

function test_slab(tbl)
    if type(tbl.items) == 'number' then
        tbl.items = nil
    end
    if type(tbl.bytes_used) == 'number' then
        tbl.bytes_used = nil
    end
    if type(tbl.item_size) == 'number' then
        tbl.item_size = nil
    end
    if type(tbl.slabs) == 'number' then
        tbl.slabs = nil
    end
    if type(tbl.bytes_free) == 'number' then
        tbl.bytes_free = nil
    end
    if #tbl > 0 then
        return false
    else
        return true
    end
end;

function test_box_slab_info()
    local tmp = box.slab.info()

    for name, tbl in ipairs(tmp.slabs) do
        if test_slab(tbl) == true then
            tmp[name] = nil
    end
    if #tmp.slabs == 0 then
        tmp.slabs = nil
    end
    if type(tmp.arena_size) == 'number' then
        tmp.arena_size = nil
    end
    if type(tmp.arena_used) == 'number' then
        tmp.arena_used = nil
    end
    if #tmp > 0 then
        return tmp
    else
        return "box.slab.info() is ok"
end;

function test_fiber(tbl)
    if type(tbl.fid) == 'number' then
        tbl.fid = nil
    end
    if type(tbl.csw) == 'number' then
        tbl.csw = nil
    end
    if type(tbl.backtrace) == 'table' and #tbl.backtrace > 0 then
        tbl.backtrace = nil
    end
    if #tbl > 0 then
        return false
    else
        return true
    end
end;

function test_box_fiber_info()
    local tmp = box.fiber.info()
    for name, tbl in ipairs(tmp) do
        if test_fiber(tbl) == true then
            tmp[name] = nil
        end
    end
    if #tmp > 0 then
        return tmp
    else
        return "box.fiber.info() is ok"
    end
end;

test_box_info();
test_box_slab_info();
test_box_fiber_info();
