env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')

space = box.schema.space.create('tweedledum')
index = space:create_index('primary')

help()
cfg_filter(box.cfg)
space:insert{1, 'tuple'}
box.snapshot()
space:delete{1}

test_run:cmd("setopt delimiter ';'")
function check_type(arg, typeof)
    return type(arg) == typeof
end;

function test_box_info()
    local tmp = box.info()
    local num = {'pid', 'uptime'}
    local str = {'version', 'status' }
    local failed = {}
    if check_type(tmp.replication, 'table') == false then
        table.insert(failed, 'box.info().replication')
    else
        tmp.replication = nil
    end
    for k, v in ipairs(num) do
        if check_type(tmp[v], 'number') == false then
            table.insert(failed, 'box.info().'..v)
        else
            tmp[v] = nil
        end
    end
    for k, v in ipairs(str) do
        if check_type(tmp[v], 'string') == false then
            table.insert(failed, 'box.info().'..v)
        else
            tmp[v] = nil
        end
    end
    if #tmp > 0 or #failed > 0 then
        return 'box.info() is not ok.', 'failed: ', failed, tmp
    else
        return 'box.info() is ok.'
    end
end;

function test_slab(tbl)
    local num = {'item_size', 'item_count', 'slab_size', 'slab_count', 'mem_used', 'mem_free'}
    local failed = {}
    for k, v in ipairs(num) do
        if check_type(tbl[v], 'number') == false then
            table.insert(failed, 'box.slab.info().<slab_size>.'..v)
        else
            tbl[v] = nil
        end
    end
    if #tbl > 0 or #failed > 0 then
        return false, failed
    else
        return true, {}
    end
end;

function test_box_slab_info()
    local tmp = box.slab.info()
    local tmp_slabs = box.slab.stats()
    local cdata = {'arena_size', 'arena_used'}
    local failed = {}
    if type(tmp_slabs) == 'table' then
        for name, tbl in ipairs(tmp_slabs) do
            local bl, fld = test_slab(tbl)
            if bl == true then
                tmp[name] = nil
            else
                for k, v in ipairs(fld) do
                    table.insert(failed, v)
                end
            end
        end
    else
        table.insert(failed, 'box.slab.info().slabs is not ok')
    end
    if #tmp_slabs == 0 then
        tmp_slabs = nil
    end
    for k, v in ipairs(cdata) do
        if check_type(tmp[v], 'number') == false then
            table.insert(failed, 'box.slab.info().'..v)
        else
            tmp[v] = nil
        end
    end
    if #tmp > 0 or #failed > 0 then
        return "box.slab.info() is not ok", tmp, failed
    else
        return "box.slab.info() is ok"
    end
end;

function test_fiber(tbl)
    local num = {'fid', 'csw'}
    for k, v in ipairs(num) do
        if check_type(tmp[v], 'number') == false then
            table.insert(failed, "require('fiber').info().<fiber_name>."..v)
        else
            tmp[v] = nil
        end
    end
    if type(tbl.backtrace) == 'table' and #tbl.backtrace > 0 then
        tbl.backtrace = nil
    else
        table.insert(failed, 'backtrace')
    end
    if #tbl > 0 or #failed > 0 then
        return false, failed
    else
        return true, {}
    end
end;

function test_box_fiber_info()
    local tmp = require('fiber').info()
    local failed = {}
    for name, tbl in ipairs(tmp) do
        local bl, fld = test_fiber(tbl)
        if bl == true then
            tmp[name] = nil
        else
            for k, v in ipairs(fld) do
                table.insert(failed, v)
            end
        end
    end
    if #tmp > 0 or #failed > 0 then
        return "require('fiber').info is not ok. failed: ", tmp, failed
    else
        return "require('fiber').info() is ok"
    end
end;

test_box_info();
test_box_slab_info();
test_box_fiber_info();
space:drop();
test_run:cmd("setopt delimiter ''");
